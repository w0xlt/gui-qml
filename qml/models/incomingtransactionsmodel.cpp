// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/incomingtransactionsmodel.h>
#include <qml/asyncjoin.h>

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
    std::condition_variable stop_cv;
    bool stopping GUARDED_BY(mutex){false};
    //! Set once transactions are counted.
    std::optional<incoming_transactions::Rate> rate GUARDED_BY(mutex);
};

namespace {
class IncomingTransactionsNotifications : public interfaces::Chain::Notifications
{
public:
    explicit IncomingTransactionsNotifications(std::function<void(int64_t)> add) : m_add{std::move(add)} {}

    void transactionAddedToMempool(const CTransactionRef& tx) override
    {
        m_add(GetVirtualTransactionSize(*tx));
    }

private:
    std::function<void(int64_t)> m_add;
};
} // namespace

IncomingTransactionsModel::IncomingTransactionsModel(interfaces::Node& node, HandleNotificationsFn handle_notifications,
                                                     WaitForNotificationsFn wait_for_notifications, int sample_interval_ms,
                                                     QObject* parent)
    : QObject{parent}
    , m_node{node}
    , m_handle_notifications{std::move(handle_notifications)}
    , m_wait_for_notifications{std::move(wait_for_notifications)}
    , m_state{std::make_shared<State>()}
    , m_sample_timer{new QTimer(this)}
{
    m_sample_timer->setInterval(std::max(1, sample_interval_ms));
    connect(m_sample_timer, &QTimer::timeout, this, &IncomingTransactionsModel::sample);
}

IncomingTransactionsModel::~IncomingTransactionsModel()
{
    // After beginShutdown(), JoinThreadAsync() owns the thread.
    if (!m_load_thread || m_stopping) return;
    stopLoadThread();
    m_load_thread->wait();
    delete m_load_thread;
}

void IncomingTransactionsModel::start()
{
    if (m_started || m_stopping) return;
    m_started = true;

    // Transactions are dropped until counting starts, so none are missed
    // between the mempool loading and the handler being registered.
    m_handler = m_handle_notifications(std::make_shared<IncomingTransactionsNotifications>([state = m_state](int64_t vsize) {
        LOCK(state->mutex);
        if (state->rate) state->rate->add(NowSeconds(), vsize);
    }));

    m_load_thread = QThread::create([this, state = m_state] {
        util::ThreadRename("qml-incomingtx");
        // Loading the saved mempool notifies each of its transactions.
        while (!MempoolLoaded(m_node)) {
            WAIT_LOCK(state->mutex, lock);
            if (state->stop_cv.wait_for(lock, MEMPOOL_LOAD_POLL_INTERVAL, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping; })) return;
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
    });
    m_load_thread->setObjectName(QStringLiteral("incoming-transactions"));
    m_load_thread->start();
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

void IncomingTransactionsModel::stopLoadThread()
{
    LOCK(m_state->mutex);
    m_state->stopping = true;
    m_state->stop_cv.notify_all();
}

void IncomingTransactionsModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_sample_timer->stop();
    // Notifications may still arrive after this, but only touch the state.
    m_handler.reset();
    if (!m_load_thread) {
        Q_EMIT drained();
        return;
    }
    stopLoadThread();
    JoinThreadAsync(m_load_thread, this, [this] {
        m_load_thread = nullptr;
        Q_EMIT drained();
    });
}
