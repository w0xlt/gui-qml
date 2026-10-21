// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/incomingtransactionsmodel.h>
#include <qml/asyncjoin.h>
#include <qml/models/mempoolblocks.h>

#include <interfaces/node.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <sync.h>
#include <univalue.h>
#include <util/threadnames.h>
#include <util/time.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <optional>

#include <QString>
#include <QThread>
#include <QTimer>

namespace incoming_transactions {

void Rate::add(int64_t time, int64_t vsize)
{
    if (!m_recent.empty() && m_recent.back().first == time) {
        m_recent.back().second += vsize;
    } else {
        m_recent.emplace_back(time, vsize);
    }
}

void Rate::sample(int64_t time)
{
    while (!m_recent.empty() && m_recent.front().first <= time - WINDOW_SECONDS) {
        m_recent.pop_front();
    }
    int64_t vsize{0};
    for (const auto& [second, second_vsize] : m_recent) {
        if (second <= time) vsize += second_vsize;
    }
    const int64_t counted{std::clamp<int64_t>(time - m_start_time, 1, WINDOW_SECONDS)};
    m_samples.push_back({time, static_cast<double>(vsize) / counted});
    if (m_samples.size() > MAX_SAMPLES) m_samples.pop_front();
}

} // namespace incoming_transactions

namespace {
// Transactions entering the mempool between the saved mempool loading and
// counting starting are missed, so the load is checked often.
constexpr auto MEMPOOL_LOAD_POLL_INTERVAL{std::chrono::milliseconds{100}};
// Bursts of transactions are listed together, like mempool.space's updates.
constexpr auto RECENT_TRANSACTIONS_INTERVAL{std::chrono::seconds{1}};

int64_t NowSeconds()
{
    return TicksSinceEpoch<std::chrono::seconds>(NodeClock::now());
}

bool MempoolLoaded(interfaces::Node& node)
{
    try {
        return node.executeRpc("getmempoolinfo", UniValue{UniValue::VARR}, /*uri=*/"")["loaded"].get_bool();
    } catch (const UniValue&) {
        // RPC errors, e.g. while the node shuts down.
    } catch (const std::exception&) {
    }
    return false;
}
} // namespace

struct IncomingTransactionsModel::State {
    Mutex mutex;
    std::condition_variable cv;
    bool stopping GUARDED_BY(mutex){false};
    //! Set once transactions are counted.
    std::optional<incoming_transactions::Rate> rate GUARDED_BY(mutex);
    //! The latest transactions counted, newest first, and whether they
    //! changed since they were last listed.
    std::deque<CTransactionRef> latest GUARDED_BY(mutex);
    bool latest_changed GUARDED_BY(mutex){false};
};

namespace {
class IncomingTransactionsNotifications : public interfaces::Chain::Notifications
{
public:
    explicit IncomingTransactionsNotifications(std::function<void(const CTransactionRef&)> add) : m_add{std::move(add)} {}

    void transactionAddedToMempool(const CTransactionRef& tx) override
    {
        m_add(tx);
    }

private:
    std::function<void(const CTransactionRef&)> m_add;
};

double FeeRate(const std::optional<CAmount>& fee, int64_t vsize)
{
    return fee && vsize > 0 ? static_cast<double>(*fee) / vsize : -1.0;
}
} // namespace

int RecentTransactionsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return count();
}

QVariant RecentTransactionsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const incoming_transactions::RecentTransaction& transaction{m_transactions[index.row()]};
    switch (role) {
    case TxidRole:
        return QString::fromStdString(transaction.txid.ToString());
    case ValueRole:
        return QVariant::fromValue<qint64>(transaction.value);
    case FeeRateRole:
        return FeeRate(transaction.fee, transaction.vsize);
    }
    return {};
}

QHash<int, QByteArray> RecentTransactionsModel::roleNames() const
{
    return {
        {TxidRole, "txid"},
        {ValueRole, "value"},
        {FeeRateRole, "feeRate"},
    };
}

void RecentTransactionsModel::setTransactions(std::vector<incoming_transactions::RecentTransaction> transactions)
{
    const int previous_count{count()};
    beginResetModel();
    m_transactions = std::move(transactions);
    endResetModel();
    if (count() != previous_count) Q_EMIT countChanged();
}

IncomingTransactionsModel::IncomingTransactionsModel(interfaces::Node& node, HandleNotificationsFn handle_notifications,
                                                     WaitForNotificationsFn wait_for_notifications, FindCoinsFn find_coins,
                                                     int sample_interval_ms, QObject* parent)
    : QObject{parent}
    , m_node{node}
    , m_handle_notifications{std::move(handle_notifications)}
    , m_wait_for_notifications{std::move(wait_for_notifications)}
    , m_find_coins{std::move(find_coins)}
    , m_state{std::make_shared<State>()}
    , m_sample_timer{new QTimer(this)}
{
    m_sample_timer->setInterval(std::max(1, sample_interval_ms));
    connect(m_sample_timer, &QTimer::timeout, this, &IncomingTransactionsModel::sample);
}

IncomingTransactionsModel::~IncomingTransactionsModel()
{
    // After beginShutdown(), JoinThreadAsync() owns the thread.
    if (!m_worker_thread || m_stopping) return;
    stopWorker();
    m_worker_thread->wait();
    delete m_worker_thread;
}

void IncomingTransactionsModel::start()
{
    if (m_started || m_stopping) return;
    m_started = true;

    // Transactions are dropped until counting starts, so none are missed
    // between the mempool loading and the handler being registered.
    m_handler = m_handle_notifications(std::make_shared<IncomingTransactionsNotifications>([state = m_state](const CTransactionRef& tx) {
        const int64_t vsize{GetVirtualTransactionSize(*tx)};
        LOCK(state->mutex);
        if (!state->rate) return;
        state->rate->add(NowSeconds(), vsize);
        state->latest.push_front(tx);
        if (state->latest.size() > incoming_transactions::MAX_RECENT_TRANSACTIONS) state->latest.pop_back();
        state->latest_changed = true;
        state->cv.notify_all();
    }));

    m_worker_thread = QThread::create([this, state = m_state] {
        util::ThreadRename("qml-incomingtx");
        // Loading the saved mempool notifies each of its transactions.
        while (!MempoolLoaded(m_node)) {
            WAIT_LOCK(state->mutex, lock);
            if (state->cv.wait_for(lock, MEMPOOL_LOAD_POLL_INTERVAL, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping; })) return;
        }
        // Some of those notifications may still be queued.
        m_wait_for_notifications();
        const int64_t start_time{NowSeconds()};
        {
            LOCK(state->mutex);
            if (state->stopping) return;
            state->rate.emplace(start_time);
        }
        QMetaObject::invokeMethod(this, [this, start_time] { startRecording(start_time); }, Qt::QueuedConnection);

        // List the latest transactions with their fees, which need coin
        // lookups that take Core's locks, so they aren't worked out on Core's
        // notification thread.
        std::vector<incoming_transactions::RecentTransaction> listed;
        while (true) {
            std::vector<CTransactionRef> latest;
            {
                WAIT_LOCK(state->mutex, lock);
                state->cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping || state->latest_changed; });
                if (state->stopping) return;
                latest.assign(state->latest.begin(), state->latest.end());
                state->latest_changed = false;
            }
            const auto was_listed{[&](const Txid& txid) {
                return std::find_if(listed.begin(), listed.end(), [&](const auto& transaction) { return transaction.txid == txid; });
            }};
            std::map<COutPoint, Coin> coins;
            for (const CTransactionRef& tx : latest) {
                if (was_listed(tx->GetHash()) != listed.end()) continue;
                for (const CTxIn& input : tx->vin) coins.emplace(input.prevout, Coin{});
            }
            if (!coins.empty()) m_find_coins(coins);
            std::vector<incoming_transactions::RecentTransaction> transactions;
            for (const CTransactionRef& tx : latest) {
                if (const auto known{was_listed(tx->GetHash())}; known != listed.end()) {
                    transactions.push_back(*known);
                    continue;
                }
                transactions.push_back({
                    .txid = tx->GetHash(),
                    .value = tx->GetValueOut(),
                    .fee = mempool_blocks::FeeFromCoins(*tx, coins),
                    .vsize = GetVirtualTransactionSize(*tx),
                });
            }
            listed = transactions;
            QMetaObject::invokeMethod(this, [this, transactions = std::move(transactions)]() mutable {
                if (!m_stopping) m_recent_transactions.setTransactions(std::move(transactions));
            }, Qt::QueuedConnection);

            WAIT_LOCK(state->mutex, lock);
            if (state->cv.wait_for(lock, RECENT_TRANSACTIONS_INTERVAL, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping; })) return;
        }
    });
    m_worker_thread->setObjectName(QStringLiteral("incoming-transactions"));
    m_worker_thread->start();
}

void IncomingTransactionsModel::startRecording(int64_t start_time)
{
    if (m_stopping) return;
    m_recording_since = start_time * 1000;
    Q_EMIT recordingSinceChanged();
    m_sample_timer->start();
}

void IncomingTransactionsModel::sample()
{
    std::deque<incoming_transactions::Sample> samples;
    {
        LOCK(m_state->mutex);
        if (!m_state->rate) return;
        m_state->rate->sample(NowSeconds());
        samples = m_state->rate->samples();
    }
    m_times.clear();
    m_rates.clear();
    for (const incoming_transactions::Sample& sample : samples) {
        m_times.append(static_cast<qreal>(sample.time) * 1000);
        m_rates.append(sample.vbytes_per_second);
    }
    Q_EMIT samplesChanged();
}

void IncomingTransactionsModel::stopWorker()
{
    LOCK(m_state->mutex);
    m_state->stopping = true;
    m_state->cv.notify_all();
}

void IncomingTransactionsModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_sample_timer->stop();
    // Notifications may still arrive after this, but only touch the state.
    m_handler.reset();
    if (!m_worker_thread) {
        Q_EMIT drained();
        return;
    }
    stopWorker();
    JoinThreadAsync(m_worker_thread, this, [this] {
        m_worker_thread = nullptr;
        Q_EMIT drained();
    });
}
