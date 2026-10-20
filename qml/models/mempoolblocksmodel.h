// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H
#define BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H

#include <qml/models/mempoolblocks.h>

#include <QAbstractListModel>

#include <vector>

namespace interfaces {
class Node;
}

class MempoolBlocksWorker;
class QThread;

//! Projected next blocks built from the node's mempool, nearest block first.
class MempoolBlocksModel : public QAbstractListModel
{
    Q_OBJECT
    // Snapshotting the mempool holds its lock, so it only runs while a view
    // showing the blocks is active.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

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

    explicit MempoolBlocksModel(interfaces::Node& node, int min_refresh_interval_ms = 2000, QObject* parent = nullptr);
    ~MempoolBlocksModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    bool loaded() const { return m_loaded; }
    int count() const { return static_cast<int>(m_blocks.size()); }

    void beginShutdown();

    //! Apply a snapshot taken at the given tip. Blocks mined since the last
    //! snapshot are removed from the front first, so views can animate them
    //! leaving toward the chain.
    void applySnapshot(int tip_height, std::vector<mempool_blocks::ProjectedBlock> blocks);

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
};

#endif // BITCOIN_QML_MODELS_MEMPOOLBLOCKSMODEL_H
