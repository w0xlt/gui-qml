// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BUILDINGBLOCKMODEL_H
#define BITCOIN_QML_MODELS_BUILDINGBLOCKMODEL_H

#include <consensus/amount.h>

#include <QObject>
#include <QSet>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QThread;
class UniValue;

namespace interfaces {
class Node;
}

namespace building_block {

//! A transaction of a block template, in the order the template mines it.
struct TemplateTransaction {
    CAmount fee{0};
    int64_t weight{0};
};

struct BuildingBlock {
    int height{0};
    //! The block it builds on.
    std::string previous_block_hash;
    int transaction_count{0};
    //! Lowest and highest rates, in sat/vB, its transactions are mined at.
    //! Unset without transactions.
    std::optional<double> min_fee_rate;
    std::optional<double> max_fee_rate;

    bool operator==(const BuildingBlock&) const = default;
};

struct Tip {
    std::string hash;
    int transaction_count{0};
    //! Block timestamp in seconds since the epoch.
    int64_t time{0};

    bool operator==(const Tip&) const = default;
};

//! Rates in sat/vB the transactions are mined at, one per chunk, highest
//! first. A transaction paid for by a later one (CPFP) is mined together with
//! it, so the transactions are grouped the way Core chunks a linearization:
//! each one joins the chunk before it while that raises the chunk's rate.
std::vector<double> ChunkFeeRates(const std::vector<TemplateTransaction>& transactions);

//! Throw if the results are malformed.
BuildingBlock ParseBlockTemplate(const UniValue& block_template);
Tip ParseBlockHeader(const UniValue& header);

} // namespace building_block

//! The block the node would mine next, from its own block template, and the
//! block it builds on.
class BuildingBlockModel : public QObject
{
    Q_OBJECT
    //! Whether a view showing the block is active, see setViewerActive().
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    //! Whether the properties below describe a template, e.g. not while the
    //! node syncs or has no peers.
    Q_PROPERTY(bool available READ available NOTIFY buildingBlockChanged)
    Q_PROPERTY(int height READ height NOTIFY buildingBlockChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY buildingBlockChanged)
    //! In sat/vB, or -1 without transactions.
    Q_PROPERTY(double minFeeRate READ minFeeRate NOTIFY buildingBlockChanged)
    Q_PROPERTY(double maxFeeRate READ maxFeeRate NOTIFY buildingBlockChanged)
    //! The block it builds on, or -1 and 0 until known.
    Q_PROPERTY(int tipTransactionCount READ tipTransactionCount NOTIFY buildingBlockChanged)
    //! In milliseconds since the epoch.
    Q_PROPERTY(qint64 tipTime READ tipTime NOTIFY buildingBlockChanged)

public:
    explicit BuildingBlockModel(interfaces::Node& node, int refresh_interval_ms = 5000, QObject* parent = nullptr);
    ~BuildingBlockModel() override;

    bool active() const { return !m_active_viewers.isEmpty(); }
    bool available() const { return m_building_block.has_value(); }
    int height() const { return m_building_block ? m_building_block->height : 0; }
    int transactionCount() const { return m_building_block ? m_building_block->transaction_count : 0; }
    double minFeeRate() const { return m_building_block ? m_building_block->min_fee_rate.value_or(-1) : -1; }
    double maxFeeRate() const { return m_building_block ? m_building_block->max_fee_rate.value_or(-1) : -1; }
    int tipTransactionCount() const { return m_tip ? m_tip->transaction_count : -1; }
    qint64 tipTime() const { return m_tip ? m_tip->time * 1000 : 0; }

    //! Building a template takes the node's locks, so it is only refreshed
    //! while at least one view showing it is active. A viewer that is
    //! destroyed stops counting.
    Q_INVOKABLE void setViewerActive(QObject* viewer, bool active);
    void beginShutdown();

public Q_SLOTS:
    //! Start refreshing once the node's chain state is ready.
    void start();
    //! Refresh soon, e.g. because a block was connected.
    void refresh();

Q_SIGNALS:
    void drained();
    void activeChanged();
    void buildingBlockChanged();

private:
    struct State;

    void setResult(std::optional<building_block::BuildingBlock> building_block, std::optional<building_block::Tip> tip);
    void stopWorker();

    interfaces::Node& m_node;
    const int m_refresh_interval_ms;
    std::shared_ptr<State> m_state;
    QThread* m_worker_thread{nullptr};
    QSet<QObject*> m_active_viewers;
    std::optional<building_block::BuildingBlock> m_building_block;
    std::optional<building_block::Tip> m_tip;
    bool m_started{false};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_MODELS_BUILDINGBLOCKMODEL_H
