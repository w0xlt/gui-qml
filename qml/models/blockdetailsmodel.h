// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BLOCKDETAILSMODEL_H
#define BITCOIN_QML_MODELS_BLOCKDETAILSMODEL_H

#include <uint256.h>

#include <QObject>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class QThread;
class UniValue;

namespace interfaces {
class Chain;
class Node;
}

namespace block_details {

struct Block {
    int height{0};
    //! Empty for the genesis block.
    std::string previous_block_hash;
    //! Timestamps in seconds since the epoch, of it and of the block it
    //! builds on.
    int64_t time{0};
    std::optional<int64_t> previous_time;
    int transaction_count{0};
    //! In bytes. Unset if the block was pruned.
    std::optional<int64_t> size;
    //! In sat/vB. Unset without transactions besides the coinbase, or if the
    //! block was pruned.
    std::optional<double> median_fee_rate;

    bool operator==(const Block&) const = default;
};

//! From getblock, or from getblockheader without the size. Throw if the
//! result is malformed.
Block ParseBlock(const UniValue& block);
//! The 50th percentile of getblockstats' feerate_percentiles. Throw if the
//! result is malformed.
double ParseMedianFeeRate(const UniValue& stats);

} // namespace block_details

//! Details of the block pointed at on the block clock, read on a worker
//! thread.
class BlockDetailsModel : public QObject
{
    Q_OBJECT
    //! Whether the properties below describe the selected block.
    Q_PROPERTY(bool available READ available NOTIFY blockChanged)
    Q_PROPERTY(int height READ height NOTIFY blockChanged)
    //! In milliseconds since the epoch.
    Q_PROPERTY(qint64 time READ time NOTIFY blockChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY blockChanged)
    //! In bytes, or -1 if the block was pruned.
    Q_PROPERTY(qint64 size READ size NOTIFY blockChanged)
    //! In sat/vB, or -1.
    Q_PROPERTY(double medianFeeRate READ medianFeeRate NOTIFY blockChanged)
    //! Seconds since the block it builds on, or -1 if that is unknown or
    //! timestamped later.
    Q_PROPERTY(qint64 interval READ interval NOTIFY blockChanged)

public:
    //! Hash of a block of the active chain timestamped at the time, the lowest
    //! after skipping as many as the ordinal.
    using BlockFinder = std::function<std::optional<uint256>(int64_t time, int ordinal)>;

    BlockDetailsModel(interfaces::Node& node, BlockFinder find_block, QObject* parent = nullptr);
    ~BlockDetailsModel() override;

    bool available() const { return m_block.has_value(); }
    int height() const { return m_block ? m_block->height : 0; }
    qint64 time() const { return m_block ? m_block->time * 1000 : 0; }
    int transactionCount() const { return m_block ? m_block->transaction_count : 0; }
    qint64 size() const { return m_block ? m_block->size.value_or(-1) : -1; }
    double medianFeeRate() const { return m_block ? m_block->median_fee_rate.value_or(-1) : -1; }
    qint64 interval() const;

    //! Select the block timestamped at the time, in seconds since the epoch,
    //! or a higher one with the same timestamp. The properties keep describing
    //! the previous block until its details are read.
    Q_INVOKABLE void select(qint64 time, int ordinal = 0);
    Q_INVOKABLE void clear();
    void beginShutdown();

Q_SIGNALS:
    void drained();
    void blockChanged();

private:
    struct State;

    void startWorker();
    void stopWorker();
    void setBlock(std::optional<block_details::Block> block);

    interfaces::Node& m_node;
    const BlockFinder m_find_block;
    std::shared_ptr<State> m_state;
    QThread* m_worker_thread{nullptr};
    //! Identifies the latest select() or clear(), so earlier results are dropped.
    uint64_t m_request{0};
    std::optional<block_details::Block> m_block;
    bool m_stopping{false};
};

std::optional<uint256> FindBlockTimestampedAt(interfaces::Chain& chain, int64_t time, int ordinal);

#endif // BITCOIN_QML_MODELS_BLOCKDETAILSMODEL_H
