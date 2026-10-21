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
#include <string>
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
    std::vector<mempool_blocks::MinedBlock> mined_blocks;
    mempool_blocks::RecommendedFees recommended_fees;
};

QVariantList FeeRangeList(const std::vector<double>& fee_range)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(fee_range.size()));
    for (const double fee_rate : fee_range) {
        list.append(fee_rate);
    }
    return list;
}
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
        qint64 mempool_cost_ms{0};
        try {
            MempoolBlocksSnapshot snapshot;
            snapshot.generation = m_generation;
            snapshot.tip_height = m_node.getNumBlocks();
            updateMinedBlocks(snapshot.tip_height);
            snapshot.mined_blocks = m_mined_blocks;

            QElapsedTimer mempool_timer;
            mempool_timer.start();
            UniValue params{UniValue::VARR};
            params.push_back(/*verbose=*/true);
            const UniValue mempool{m_node.executeRpc("getrawmempool", params, /*uri=*/"")};
            snapshot.blocks = mempool_blocks::ProjectBlocks(mempool_blocks::ParseRawMempool(mempool));
            const UniValue mempool_info{m_node.executeRpc("getmempoolinfo", UniValue{UniValue::VARR}, /*uri=*/"")};
            snapshot.recommended_fees = mempool_blocks::RecommendFees(snapshot.blocks, mempool_blocks::ParseMempoolMinFee(mempool_info));
            mempool_cost_ms = mempool_timer.elapsed();
            m_publish(std::move(snapshot));
        } catch (const UniValue&) {
            // RPC errors, e.g. while the node shuts down. The next refresh retries.
        } catch (const std::exception&) {
        }
        m_timer->start(static_cast<int>(std::clamp<qint64>(mempool_cost_ms * REFRESH_COST_FACTOR,
                                                           m_min_refresh_interval_ms,
                                                           std::max(m_min_refresh_interval_ms, MAX_REFRESH_INTERVAL_MS))));
    }

    std::string blockHash(int height)
    {
        UniValue params{UniValue::VARR};
        params.push_back(height);
        return m_node.executeRpc("getblockhash", params, /*uri=*/"").get_str();
    }

    //! Refresh the recent blocks when the tip changed, reusing the stats of
    //! blocks seen before. Reading stats loads each block and its undo data
    //! from disk, so after a slow refresh the next one waits, e.g. while
    //! blocks connect quickly during sync.
    void updateMinedBlocks(int tip_height)
    {
        if (tip_height < 0) return;
        if (m_last_mined_fetch.isValid() && m_last_mined_fetch.elapsed() < m_mined_retry_ms) return;
        std::string hash;
        try {
            hash = blockHash(tip_height);
        } catch (const UniValue&) {
            return;
        } catch (const std::exception&) {
            return;
        }
        if (!m_mined_blocks.empty() && m_mined_blocks.front().hash == hash) return;

        m_last_mined_fetch.start();
        std::vector<mempool_blocks::MinedBlock> mined_blocks;
        try {
            for (int height = tip_height; height >= 0 && mined_blocks.size() < mempool_blocks::MAX_MINED_BLOCKS; --height) {
                if (height < tip_height) hash = blockHash(height);
                const auto seen{std::find_if(m_mined_blocks.begin(), m_mined_blocks.end(), [&](const auto& block) {
                    return block.hash == hash;
                })};
                if (seen != m_mined_blocks.end()) {
                    mined_blocks.push_back(*seen);
                    continue;
                }
                UniValue fields{UniValue::VARR};
                for (const std::string& field : mempool_blocks::BlockStatsFields()) {
                    fields.push_back(field);
                }
                UniValue params{UniValue::VARR};
                params.push_back(hash);
                params.push_back(std::move(fields));
                mined_blocks.push_back(mempool_blocks::ParseBlockStats(m_node.executeRpc("getblockstats", params, /*uri=*/""), hash));
            }
        } catch (const UniValue&) {
            // Older blocks may be pruned. Show the ones read so far.
        } catch (const std::exception&) {
        }
        m_mined_blocks = std::move(mined_blocks);
        m_mined_retry_ms = std::min<qint64>(m_last_mined_fetch.elapsed() * REFRESH_COST_FACTOR, MAX_REFRESH_INTERVAL_MS);
    }

    interfaces::Node& m_node;
    const int m_min_refresh_interval_ms;
    PublishFn m_publish;
    QTimer* m_timer;
    QElapsedTimer m_last_fetch;
    QElapsedTimer m_last_mined_fetch;
    qint64 m_mined_retry_ms{0};
    std::vector<mempool_blocks::MinedBlock> m_mined_blocks;
    bool m_started{false};
    bool m_active{false};
    quint64 m_generation{0};
};

int MinedBlocksModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return count();
}

QVariant MinedBlocksModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};

    const mempool_blocks::MinedBlock& block{m_blocks[index.row()]};
    switch (role) {
    case HeightRole:
        return block.height;
    case TimeRole:
        return QVariant::fromValue<qint64>(block.time);
    case MedianFeeRateRole:
        return block.median_fee_rate;
    case MinFeeRateRole:
        // Like mempool.space for blocks it only has `getblockstats` for, the
        // range starts at the 10th percentile.
        return block.fee_range.size() > 1 ? block.fee_range[1] : 0.0;
    case MaxFeeRateRole:
        return block.fee_range.empty() ? 0.0 : block.fee_range.back();
    case FeeRangeRole:
        return FeeRangeList(block.fee_range);
    case TotalFeesRole:
        return QVariant::fromValue<qint64>(block.total_fees);
    case TransactionCountRole:
        return block.tx_count;
    case VSizeRole:
        return QVariant::fromValue<qint64>(block.weight / WITNESS_SCALE_FACTOR);
    }
    return {};
}

QHash<int, QByteArray> MinedBlocksModel::roleNames() const
{
    return {
        {HeightRole, "height"},
        {TimeRole, "time"},
        {MedianFeeRateRole, "medianFeeRate"},
        {MinFeeRateRole, "minFeeRate"},
        {MaxFeeRateRole, "maxFeeRate"},
        {FeeRangeRole, "feeRange"},
        {TotalFeesRole, "totalFees"},
        {TransactionCountRole, "transactionCount"},
        {VSizeRole, "vsize"},
    };
}

void MinedBlocksModel::setBlocks(std::vector<mempool_blocks::MinedBlock> blocks)
{
    const auto same_hash{[](const auto& a, const auto& b) { return a.hash == b.hash; }};
    const int previous_count{count()};

    // New blocks on top of the previous tip, followed by the previous blocks.
    size_t added{0};
    if (!m_blocks.empty()) {
        while (added < blocks.size() && blocks[added].hash != m_blocks.front().hash) ++added;
    }
    const size_t kept{blocks.size() - added};
    const bool extends_tip{!m_blocks.empty() && added < blocks.size() && kept <= m_blocks.size() &&
                           std::equal(blocks.begin() + added, blocks.end(), m_blocks.begin(), same_hash)};

    if (extends_tip) {
        if (kept < m_blocks.size()) {
            beginRemoveRows({}, static_cast<int>(kept), count() - 1);
            m_blocks.resize(kept);
            endRemoveRows();
        }
        if (added > 0) {
            beginInsertRows({}, 0, static_cast<int>(added) - 1);
            m_blocks.insert(m_blocks.begin(), std::make_move_iterator(blocks.begin()),
                            std::make_move_iterator(blocks.begin() + added));
            endInsertRows();
        }
    } else if (m_blocks.empty()) {
        if (!blocks.empty()) {
            beginInsertRows({}, 0, static_cast<int>(blocks.size()) - 1);
            m_blocks = std::move(blocks);
            endInsertRows();
        }
    } else {
        // A reorg, or a jump past every block shown.
        beginResetModel();
        m_blocks = std::move(blocks);
        endResetModel();
    }

    if (count() != previous_count) Q_EMIT countChanged();
}

MempoolBlocksModel::MempoolBlocksModel(interfaces::Node& node, int min_refresh_interval_ms, QObject* parent)
    : QAbstractListModel{parent}
    , m_worker_thread{new QThread(this)}
{
    m_worker_thread->setObjectName(QStringLiteral("mempool-blocks"));
    m_worker = new MempoolBlocksWorker(node, min_refresh_interval_ms, [this](MempoolBlocksSnapshot snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot)]() mutable {
            if (m_stopping || !m_active || snapshot.generation != m_activation_generation) return;
            applySnapshot(snapshot.tip_height, std::move(snapshot.blocks), std::move(snapshot.mined_blocks),
                          snapshot.recommended_fees);
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
    case FeeRangeRole:
        return FeeRangeList(block.fee_range);
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

QVariantMap MempoolBlocksModel::recommendedFees() const
{
    return {
        {QStringLiteral("fastest"), m_recommended_fees.fastest},
        {QStringLiteral("halfHour"), m_recommended_fees.half_hour},
        {QStringLiteral("hour"), m_recommended_fees.hour},
        {QStringLiteral("economy"), m_recommended_fees.economy},
        {QStringLiteral("minimum"), m_recommended_fees.minimum},
    };
}

void MempoolBlocksModel::applySnapshot(int tip_height, std::vector<mempool_blocks::ProjectedBlock> blocks,
                                       std::vector<mempool_blocks::MinedBlock> mined_blocks,
                                       const mempool_blocks::RecommendedFees& recommended_fees)
{
    m_mined_blocks.setBlocks(std::move(mined_blocks));
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
    if (recommended_fees != m_recommended_fees) {
        m_recommended_fees = recommended_fees;
        Q_EMIT recommendedFeesChanged();
    }
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
