// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/mempoolblocksmodel.h>
#include <qml/asyncjoin.h>

#include <consensus/consensus.h>
#include <interfaces/node.h>
#include <univalue.h>
#include <util/threadnames.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <iterator>
#include <utility>

#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

namespace {
constexpr int MAX_REFRESH_INTERVAL_MS{30'000};
// Snapshots hold the mempool lock, so a slow one delays the next one by this
// factor to keep the lock mostly free on large mempools.
constexpr int REFRESH_COST_FACTOR{10};

struct MempoolBlocksSnapshot {
    quint64 generation{0};
    int tip_height{0};
    std::vector<mempool_blocks::ProjectedBlock> blocks;
};
} // namespace

class MempoolBlocksWorker : public QObject
{
public:
    using PublishFn = std::function<void(MempoolBlocksSnapshot)>;

    MempoolBlocksWorker(interfaces::Node& node, int min_refresh_interval_ms, PublishFn publish)
        : m_node{node}
        , m_min_refresh_interval_ms{std::max(1, min_refresh_interval_ms)}
        , m_publish{std::move(publish)}
        , m_timer{new QTimer(this)}
    {
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this] {
            fetch();
        });
    }

    void start()
    {
        util::ThreadRename("qml-mempoolblk");
        m_started = true;
        scheduleSoon();
    }

    void stop()
    {
        m_started = false;
        m_timer->stop();
    }

    void setActive(bool active, quint64 generation)
    {
        m_active = active;
        m_generation = generation;
        if (!m_active) {
            m_timer->stop();
            return;
        }
        scheduleSoon();
    }

    void requestRefresh()
    {
        scheduleSoon();
    }

private:
    //! Refresh now, unless the last snapshot is very recent, e.g. while blocks
    //! connect quickly during sync.
    void scheduleSoon()
    {
        if (!m_started || !m_active) return;
        const qint64 delay{m_last_fetch.isValid() ? std::max<qint64>(0, m_min_refresh_interval_ms - m_last_fetch.elapsed()) : 0};
        if (m_timer->isActive() && m_timer->remainingTime() <= delay) return;
        m_timer->start(static_cast<int>(delay));
    }

    void fetch()
    {
        if (!m_started || !m_active) return;
        m_last_fetch.start();
        try {
            MempoolBlocksSnapshot snapshot;
            snapshot.generation = m_generation;
            snapshot.tip_height = m_node.getNumBlocks();
            UniValue params{UniValue::VARR};
            params.push_back(/*verbose=*/true);
            const UniValue mempool{m_node.executeRpc("getrawmempool", params, /*uri=*/"")};
            snapshot.blocks = mempool_blocks::ProjectBlocks(mempool_blocks::ParseRawMempool(mempool));
            m_publish(std::move(snapshot));
        } catch (const UniValue&) {
            // RPC errors, e.g. while the node shuts down. The next refresh retries.
        } catch (const std::exception&) {
        }
        const qint64 cost_ms{m_last_fetch.elapsed()};
        m_timer->start(static_cast<int>(std::clamp<qint64>(cost_ms * REFRESH_COST_FACTOR,
                                                           m_min_refresh_interval_ms,
                                                           std::max(m_min_refresh_interval_ms, MAX_REFRESH_INTERVAL_MS))));
    }

    interfaces::Node& m_node;
    const int m_min_refresh_interval_ms;
    PublishFn m_publish;
    QTimer* m_timer;
    QElapsedTimer m_last_fetch;
    bool m_started{false};
    bool m_active{false};
    quint64 m_generation{0};
};

MempoolBlocksModel::MempoolBlocksModel(interfaces::Node& node, int min_refresh_interval_ms, QObject* parent)
    : QAbstractListModel{parent}
    , m_worker_thread{new QThread(this)}
{
    m_worker_thread->setObjectName(QStringLiteral("mempool-blocks"));
    m_worker = new MempoolBlocksWorker(node, min_refresh_interval_ms, [this](MempoolBlocksSnapshot snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot)]() mutable {
            if (m_stopping || !m_active || snapshot.generation != m_activation_generation) return;
            applySnapshot(snapshot.tip_height, std::move(snapshot.blocks));
        }, Qt::QueuedConnection);
    });
    m_worker->moveToThread(m_worker_thread);
    connect(m_worker_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker_thread->start();
}

MempoolBlocksModel::~MempoolBlocksModel()
{
    if (!m_worker_thread) return;
    m_worker_thread->quit();
    m_worker_thread->wait();
    m_worker = nullptr;
}

int MempoolBlocksModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return count();
}

QVariant MempoolBlocksModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};

    const mempool_blocks::ProjectedBlock& block{m_blocks[index.row()]};
    switch (role) {
    case HeightRole:
        return m_tip_height + index.row() + 1;
    case MedianFeeRateRole:
        return block.median_fee_rate;
    case MinFeeRateRole:
        return block.fee_range.empty() ? 0.0 : block.fee_range.front();
    case MaxFeeRateRole:
        return block.fee_range.empty() ? 0.0 : block.fee_range.back();
    case FeeRangeRole: {
        QVariantList fee_range;
        fee_range.reserve(static_cast<qsizetype>(block.fee_range.size()));
        for (const double fee_rate : block.fee_range) {
            fee_range.append(fee_rate);
        }
        return fee_range;
    }
    case TotalFeesRole:
        return QVariant::fromValue<qint64>(block.total_fees);
    case TransactionCountRole:
        return block.tx_count;
    case VSizeRole:
        return QVariant::fromValue<qint64>(block.weight / WITNESS_SCALE_FACTOR);
    }
    return {};
}

QHash<int, QByteArray> MempoolBlocksModel::roleNames() const
{
    return {
        {HeightRole, "height"},
        {MedianFeeRateRole, "medianFeeRate"},
        {MinFeeRateRole, "minFeeRate"},
        {MaxFeeRateRole, "maxFeeRate"},
        {FeeRangeRole, "feeRange"},
        {TotalFeesRole, "totalFees"},
        {TransactionCountRole, "transactionCount"},
        {VSizeRole, "vsize"},
    };
}

void MempoolBlocksModel::applySnapshot(int tip_height, std::vector<mempool_blocks::ProjectedBlock> blocks)
{
    const int previous_count{count()};

    if (m_tip_height >= 0 && tip_height > m_tip_height && !m_blocks.empty()) {
        const int mined{std::min(tip_height - m_tip_height, count())};
        beginRemoveRows({}, 0, mined - 1);
        m_blocks.erase(m_blocks.begin(), m_blocks.begin() + mined);
        endRemoveRows();
    }
    m_tip_height = tip_height;

    const int new_count{static_cast<int>(blocks.size())};
    if (new_count < count()) {
        beginRemoveRows({}, new_count, count() - 1);
        m_blocks.resize(new_count);
        endRemoveRows();
    }

    const int updated{count()};
    std::move(blocks.begin(), blocks.begin() + updated, m_blocks.begin());
    if (updated > 0) {
        Q_EMIT dataChanged(index(0), index(updated - 1));
    }

    if (new_count > updated) {
        beginInsertRows({}, updated, new_count - 1);
        std::move(blocks.begin() + updated, blocks.end(), std::back_inserter(m_blocks));
        endInsertRows();
    }

    if (count() != previous_count) Q_EMIT countChanged();
    if (!m_loaded) {
        m_loaded = true;
        Q_EMIT loadedChanged();
    }
}

void MempoolBlocksModel::setActive(bool active)
{
    if (m_stopping) return;
    if (m_active == active) return;

    m_active = active;
    ++m_activation_generation;
    Q_EMIT activeChanged();

    QMetaObject::invokeMethod(m_worker, [worker = m_worker, active, generation = m_activation_generation] {
        worker->setActive(active, generation);
    }, Qt::QueuedConnection);
}

void MempoolBlocksModel::start()
{
    if (m_stopping || m_started) return;
    m_started = true;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->start();
    }, Qt::QueuedConnection);
}

void MempoolBlocksModel::refresh()
{
    if (m_stopping || !m_started) return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->requestRefresh();
    }, Qt::QueuedConnection);
}

void MempoolBlocksModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    if (!m_worker_thread) {
        Q_EMIT drained();
        return;
    }
    JoinThreadAsync(m_worker_thread, this, [this] {
        m_worker_thread = nullptr;
        m_worker = nullptr;
        Q_EMIT drained();
    });
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, thread = m_worker_thread] {
        worker->stop();
        QMetaObject::invokeMethod(thread, &QThread::quit, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
