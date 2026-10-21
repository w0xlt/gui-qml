// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/recentreplacementsmodel.h>
#include <qml/asyncjoin.h>
#include <qml/models/mempoolblocks.h>

#include <kernel/chain.h>
#include <kernel/mempool_removal_reason.h>
#include <kernel/types.h>
#include <policy/policy.h>
#include <sync.h>
#include <util/rbf.h>
#include <util/threadnames.h>

#include <algorithm>
#include <condition_variable>
#include <set>
#include <utility>
#include <variant>

#include <QString>
#include <QThread>

namespace replacements {

void ReplacementList::add(const std::vector<Txid>& replaced, Replacement replacement)
{
    std::erase_if(m_replacements, [&](const Replacement& shown) {
        return std::find(replaced.begin(), replaced.end(), shown.txid) != replaced.end();
    });
    m_replacements.push_front(std::move(replacement));
    if (m_replacements.size() > MAX_REPLACEMENTS) m_replacements.pop_back();
}

bool ReplacementList::markMined(const Txid& txid)
{
    for (Replacement& replacement : m_replacements) {
        if (replacement.txid != txid) continue;
        replacement.mined = true;
        return true;
    }
    return false;
}

} // namespace replacements

namespace {
// Replaced transactions waiting for their replacement, such as descendants
// evicted with them, which never get one.
constexpr size_t MAX_PENDING_REPLACED{100};

struct ReplacedEvent {
    std::vector<CTransactionRef> replaced;
    CTransactionRef replacement;
};

struct MinedEvent {
    Txid txid;
};

using Event = std::variant<ReplacedEvent, MinedEvent>;

double FeeRate(const std::optional<CAmount>& fee, int64_t vsize)
{
    return fee && vsize > 0 ? static_cast<double>(*fee) / vsize : -1.0;
}
} // namespace

struct RecentReplacementsModel::State {
    Mutex mutex;
    std::condition_variable cv;
    bool stopping GUARDED_BY(mutex){false};
    std::deque<Event> events GUARDED_BY(mutex);
    //! Replacements shown, to notice when they are mined.
    std::set<Txid> shown GUARDED_BY(mutex);

    void push(Event event) EXCLUSIVE_LOCKS_REQUIRED(!mutex)
    {
        LOCK(mutex);
        if (stopping) return;
        events.push_back(std::move(event));
        cv.notify_one();
    }
};

namespace {
//! Pairs replaced transactions with their replacement. Core removes the
//! replaced transactions, with their descendants, before it adds the one
//! replacing them, and the replacement spends an input of each.
class ReplacementNotifications : public interfaces::Chain::Notifications
{
public:
    using Push = std::function<void(Event)>;
    using Shown = std::function<std::set<Txid>()>;

    ReplacementNotifications(Push push, Shown shown) : m_push{std::move(push)}, m_shown{std::move(shown)} {}

    void transactionRemovedFromMempool(const CTransactionRef& tx, MemPoolRemovalReason reason) override
    {
        if (reason != MemPoolRemovalReason::REPLACED) return;
        m_pending.push_back(tx);
        if (m_pending.size() > MAX_PENDING_REPLACED) m_pending.pop_front();
    }

    // Transactions leaving the mempool for a block aren't notified as
    // removed, so mined replacements are found in the block.
    void blockConnected(const kernel::ChainstateRole& role, const interfaces::BlockInfo& block) override
    {
        if (role.historical || !block.data) return;
        const std::set<Txid> shown{m_shown()};
        if (shown.empty()) return;
        for (const CTransactionRef& tx : block.data->vtx) {
            if (shown.contains(tx->GetHash())) m_push(MinedEvent{tx->GetHash()});
        }
    }

    void transactionAddedToMempool(const CTransactionRef& tx) override
    {
        if (m_pending.empty()) return;
        std::set<COutPoint> spent;
        for (const CTxIn& input : tx->vin) spent.insert(input.prevout);
        ReplacedEvent event{.replaced = {}, .replacement = tx};
        std::erase_if(m_pending, [&](const CTransactionRef& replaced) {
            const bool conflicts{std::any_of(replaced->vin.begin(), replaced->vin.end(), [&](const CTxIn& input) {
                return spent.contains(input.prevout);
            })};
            if (conflicts) event.replaced.push_back(replaced);
            return conflicts;
        });
        if (!event.replaced.empty()) m_push(std::move(event));
    }

private:
    Push m_push;
    Shown m_shown;
    // Only touched from Core's notification thread.
    std::deque<CTransactionRef> m_pending;
};
} // namespace

RecentReplacementsModel::RecentReplacementsModel(HandleNotificationsFn handle_notifications, FindCoinsFn find_coins, QObject* parent)
    : QAbstractListModel{parent}
    , m_handle_notifications{std::move(handle_notifications)}
    , m_find_coins{std::move(find_coins)}
    , m_state{std::make_shared<State>()}
{
}

RecentReplacementsModel::~RecentReplacementsModel()
{
    // After beginShutdown(), JoinThreadAsync() owns the thread.
    if (!m_worker_thread || m_stopping) return;
    stopWorker();
    m_worker_thread->wait();
    delete m_worker_thread;
}

int RecentReplacementsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return count();
}

QVariant RecentReplacementsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const replacements::Replacement& replacement{m_replacements[index.row()]};
    switch (role) {
    case TxidRole:
        return QString::fromStdString(replacement.txid.ToString());
    case OldFeeRateRole:
        return FeeRate(replacement.old_fee, replacement.old_vsize);
    case NewFeeRateRole:
        return FeeRate(replacement.new_fee, replacement.new_vsize);
    case FullRbfRole:
        return replacement.full_rbf;
    case MinedRole:
        return replacement.mined;
    }
    return {};
}

QHash<int, QByteArray> RecentReplacementsModel::roleNames() const
{
    return {
        {TxidRole, "txid"},
        {OldFeeRateRole, "oldFeeRate"},
        {NewFeeRateRole, "newFeeRate"},
        {FullRbfRole, "fullRbf"},
        {MinedRole, "mined"},
    };
}

void RecentReplacementsModel::setReplacements(std::vector<replacements::Replacement> replacements)
{
    const int previous_count{count()};
    beginResetModel();
    m_replacements = std::move(replacements);
    endResetModel();
    if (count() != previous_count) Q_EMIT countChanged();
}

void RecentReplacementsModel::start()
{
    if (m_started || m_stopping) return;
    m_started = true;

    m_handler = m_handle_notifications(std::make_shared<ReplacementNotifications>(
        [state = m_state](Event event) { state->push(std::move(event)); },
        [state = m_state] {
            LOCK(state->mutex);
            return state->shown;
        }));

    // Fees need coin lookups, which take Core's locks, so they are worked out
    // here rather than on Core's notification thread.
    m_worker_thread = QThread::create([this, state = m_state] {
        util::ThreadRename("qml-replacements");
        replacements::ReplacementList list;
        while (true) {
            Event event;
            {
                WAIT_LOCK(state->mutex, lock);
                state->cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping || !state->events.empty(); });
                if (state->stopping) return;
                event = std::move(state->events.front());
                state->events.pop_front();
            }
            if (const auto* mined{std::get_if<MinedEvent>(&event)}) {
                if (!list.markMined(mined->txid)) continue;
            } else {
                const ReplacedEvent& replaced{std::get<ReplacedEvent>(event)};
                std::map<COutPoint, Coin> coins;
                for (const CTxIn& input : replaced.replacement->vin) coins.emplace(input.prevout, Coin{});
                for (const CTransactionRef& tx : replaced.replaced) {
                    for (const CTxIn& input : tx->vin) coins.emplace(input.prevout, Coin{});
                }
                m_find_coins(coins);

                replacements::Replacement replacement;
                replacement.txid = replaced.replacement->GetHash();
                replacement.new_fee = mempool_blocks::FeeFromCoins(*replaced.replacement, coins);
                replacement.new_vsize = GetVirtualTransactionSize(*replaced.replacement);
                replacement.old_fee = CAmount{0};
                std::vector<Txid> replaced_txids;
                for (const CTransactionRef& tx : replaced.replaced) {
                    const std::optional<CAmount> fee{mempool_blocks::FeeFromCoins(*tx, coins)};
                    replacement.old_fee = fee && replacement.old_fee ? std::optional{*replacement.old_fee + *fee} : std::nullopt;
                    replacement.old_vsize += GetVirtualTransactionSize(*tx);
                    replacement.full_rbf |= !SignalsOptInRBF(*tx);
                    replaced_txids.push_back(tx->GetHash());
                }
                list.add(replaced_txids, std::move(replacement));
            }

            std::vector<replacements::Replacement> shown{list.replacements().begin(), list.replacements().end()};
            {
                LOCK(state->mutex);
                state->shown.clear();
                for (const replacements::Replacement& replacement : shown) state->shown.insert(replacement.txid);
            }
            QMetaObject::invokeMethod(this, [this, shown = std::move(shown)]() mutable {
                if (!m_stopping) setReplacements(std::move(shown));
            }, Qt::QueuedConnection);
        }
    });
    m_worker_thread->setObjectName(QStringLiteral("recent-replacements"));
    m_worker_thread->start();
}

void RecentReplacementsModel::stopWorker()
{
    LOCK(m_state->mutex);
    m_state->stopping = true;
    m_state->cv.notify_all();
}

void RecentReplacementsModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    // Notifications may still arrive after this, but are dropped.
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
