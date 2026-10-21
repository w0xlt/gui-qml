// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H
#define BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H

#include <qml/models/difficultyadjustment.h>
#include <qml/models/mempoolblocks.h>

#include <QAbstractListModel>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace interfaces {
class Node;
}

class MempoolBlocksWorker;
class QThread;

//! The most recent blocks in the active chain, newest first.
class MinedBlocksModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        HeightRole = Qt::UserRole + 1,
        TimeRole,
        MedianFeeRateRole,
        MinFeeRateRole,
        MaxFeeRateRole,
        FeeRangeRole,
        TotalFeesRole,
        TransactionCountRole,
        VSizeRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_blocks.size()); }

    //! Replace the blocks. Blocks connected on top of the previous tip are
    //! inserted at the front, so views can animate them arriving.
    void setBlocks(std::vector<mempool_blocks::MinedBlock> blocks);

Q_SIGNALS:
    void countChanged();

private:
    std::vector<mempool_blocks::MinedBlock> m_blocks;
};

//! Projected next blocks built from the node's mempool, nearest block first.
class MempoolBlocksModel : public QAbstractListModel
{
    Q_OBJECT
    // Snapshotting the mempool holds its lock, so it only runs while a view
    // showing the blocks is active.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    //! Fee rates in sat/vB to pay for each priority, keyed fastest, halfHour,
    //! hour, economy and minimum.
    Q_PROPERTY(QVariantMap recommendedFees READ recommendedFees NOTIFY recommendedFeesChanged)
    //! Estimates of the next difficulty retarget and halving, with the time
    //! in milliseconds they were made at as now, and whether the chain
    //! retargets at all. Empty while the node syncs.
    Q_PROPERTY(QVariantMap difficultyAdjustment READ difficultyAdjustment NOTIFY chainEstimatesChanged)
    Q_PROPERTY(QVariantMap halving READ halving NOTIFY chainEstimatesChanged)
    //! The mempool's minimum and minimum relay fee rates in sat/vB, and its
    //! transaction count and memory usage and limit in bytes, keyed minFee,
    //! minRelayFee, transactionCount, usage and maxUsage.
    Q_PROPERTY(QVariantMap mempoolInfo READ mempoolInfo NOTIFY mempoolInfoChanged)

public:
    enum Roles {
        HeightRole = Qt::UserRole + 1,
        MedianFeeRateRole,
        MinFeeRateRole,
        MaxFeeRateRole,
        FeeRangeRole,
        TotalFeesRole,
        TransactionCountRole,
        VSizeRole,
    };

    explicit MempoolBlocksModel(interfaces::Node& node, difficulty_adjustment::ChainRules chain_rules = {},
                                int min_refresh_interval_ms = 2000, QObject* parent = nullptr);
    ~MempoolBlocksModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    bool loaded() const { return m_loaded; }
    int count() const { return static_cast<int>(m_blocks.size()); }
    QVariantMap recommendedFees() const;
    QVariantMap difficultyAdjustment() const;
    QVariantMap halving() const;
    QVariantMap mempoolInfo() const;
    //! Recent blocks, refreshed together with the projected ones.
    MinedBlocksModel* minedBlocks() { return &m_mined_blocks; }

    void beginShutdown();

    //! Apply a snapshot taken at the given tip. Blocks mined since the last
    //! snapshot are removed from the front first, so views can animate them
    //! leaving toward the chain.
    void applySnapshot(int tip_height, std::vector<mempool_blocks::ProjectedBlock> blocks,
                       std::vector<mempool_blocks::MinedBlock> mined_blocks = {},
                       const mempool_blocks::RecommendedFees& recommended_fees = {});
    void setMempoolInfo(const mempool_blocks::MempoolInfo& info);
    //! Apply estimates made at now_seconds, or none.
    void setChainEstimates(int64_t now_seconds,
                           std::optional<difficulty_adjustment::DifficultyAdjustment> difficulty_adjustment,
                           std::optional<difficulty_adjustment::Halving> halving);

public Q_SLOTS:
    void setActive(bool active);
    //! Start refreshing once the node's chain state is ready.
    void start();
    //! Refresh soon, e.g. because a block was connected.
    void refresh();

Q_SIGNALS:
    void drained();
    void activeChanged();
    void loadedChanged();
    void countChanged();
    void recommendedFeesChanged();
    void chainEstimatesChanged();
    void mempoolInfoChanged();

private:
    MempoolBlocksWorker* m_worker{nullptr};
    QThread* m_worker_thread{nullptr};
    bool m_active{false};
    bool m_started{false};
    bool m_stopping{false};
    bool m_loaded{false};
    quint64 m_activation_generation{0};
    int m_tip_height{-1};
    std::vector<mempool_blocks::ProjectedBlock> m_blocks;
    mempool_blocks::RecommendedFees m_recommended_fees;
    const difficulty_adjustment::ChainRules m_chain_rules;
    int64_t m_estimates_time{0};
    std::optional<difficulty_adjustment::DifficultyAdjustment> m_difficulty_adjustment;
    std::optional<difficulty_adjustment::Halving> m_halving;
    std::optional<mempool_blocks::MempoolInfo> m_mempool_info;
    MinedBlocksModel m_mined_blocks;
};

#endif // BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H
