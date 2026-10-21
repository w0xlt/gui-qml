// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MEMPOOLBLOCKS_H
#define BITCOIN_QML_MODELS_MEMPOOLBLOCKS_H

#include <consensus/amount.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class UniValue;

namespace mempool_blocks {

//! Maximum number of projected blocks. The last one holds everything that
//! does not fit in the blocks before it.
static constexpr size_t MAX_PROJECTED_BLOCKS{8};
//! Number of most recent blocks shown next to the projected ones.
static constexpr size_t MAX_MINED_BLOCKS{8};

//! A mempool transaction, reduced to what block projection needs.
struct MempoolTx {
    //! Sigops-adjusted virtual size.
    int64_t vsize{0};
    //! Fee paid by the transaction itself.
    CAmount fee{0};
    //! Modified fee and sigops-adjusted weight of the cluster mempool chunk
    //! containing the transaction.
    CAmount chunk_fee{0};
    int32_t chunk_weight{0};
    int64_t ancestor_count{1};
    //! Indexes of in-mempool parents in the same transaction list.
    std::vector<size_t> parents;
};

struct ProjectedBlock {
    //! Sigops-adjusted weight of the included transactions.
    int64_t weight{0};
    int tx_count{0};
    CAmount total_fees{0};
    //! Fee rates in sat/vB, using each transaction's chunk fee rate so that
    //! child-pays-for-parent is accounted for.
    double median_fee_rate{0.0};
    //! min, p10, p25, p50, p75, p90, max
    std::vector<double> fee_range;
};

//! A block in the active chain, summarized by `getblockstats`.
struct MinedBlock {
    std::string hash;
    int height{0};
    //! Block header time, in seconds since the epoch.
    int64_t time{0};
    //! Including the coinbase.
    int tx_count{0};
    CAmount total_fees{0};
    //! Weight of the transactions other than the coinbase.
    int64_t weight{0};
    //! Fee rates in sat/vB. `getblockstats` rounds them down to whole
    //! numbers and does not account for child-pays-for-parent.
    double median_fee_rate{0.0};
    //! min, p10, p25, p50, p75, p90, max
    std::vector<double> fee_range;
};

//! Statistics that ParseBlockStats() reads, to pass to `getblockstats`.
std::vector<std::string> BlockStatsFields();

//! Parse the result of `getrawmempool true`. Throws std::runtime_error or
//! std::exception subclasses on malformed input.
std::vector<MempoolTx> ParseRawMempool(const UniValue& verbose_mempool);

//! Parse the result of `getblockstats` for the block with the given hash.
//! Throws std::exception subclasses on malformed input.
MinedBlock ParseBlockStats(const UniValue& stats, std::string hash);

//! Cut the mempool into blocks in mining order: chunks by descending fee
//! rate, skipping the rest of a cluster in a block once one of its chunks no
//! longer fits, like Core's block assembler.
std::vector<ProjectedBlock> ProjectBlocks(const std::vector<MempoolTx>& txs, size_t max_blocks = MAX_PROJECTED_BLOCKS);

} // namespace mempool_blocks

#endif // BITCOIN_QML_MODELS_MEMPOOLBLOCKS_H
