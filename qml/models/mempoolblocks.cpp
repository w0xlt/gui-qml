// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/mempoolblocks.h>

#include <consensus/consensus.h>
#include <policy/policy.h>
#include <univalue.h>
#include <util/feefrac.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mempool_blocks {
namespace {
// Same limits and give-up rule as BlockAssembler::addChunks().
constexpr int64_t BLOCK_MAX_WEIGHT{DEFAULT_BLOCK_MAX_WEIGHT};
constexpr int64_t BLOCK_RESERVED_WEIGHT{DEFAULT_BLOCK_RESERVED_WEIGHT};
constexpr int MAX_CONSECUTIVE_FAILURES{1000};
constexpr int64_t BLOCK_FULL_ENOUGH_WEIGHT_DELTA{4000};
// Recommended fee rates are rounded to 1 sat/kvB, the smallest non-zero
// minimum relay fee.
constexpr double FEE_RATE_INCREMENT{0.001};
// While not all miners accept fee rates below 1 sat/vB, mempool.space raises
// the two highest priorities by an offset and to a floor.
constexpr double PRIORITY_OFFSET{0.5};
constexpr double MIN_FASTEST_FEE{1.0};
constexpr double MIN_HALF_HOUR_FEE{0.5};

CAmount ParseAmount(const UniValue& value)
{
    CAmount amount{0};
    if (!value.isNum() || !ParseFixedPoint(value.getValStr(), 8, &amount)) {
        throw std::runtime_error("invalid amount");
    }
    return amount;
}

class DisjointSets
{
public:
    explicit DisjointSets(size_t size) : m_parent(size)
    {
        std::iota(m_parent.begin(), m_parent.end(), size_t{0});
    }

    size_t find(size_t item)
    {
        while (m_parent[item] != item) {
            m_parent[item] = m_parent[m_parent[item]];
            item = m_parent[item];
        }
        return item;
    }

    void join(size_t a, size_t b)
    {
        a = find(a);
        b = find(b);
        if (a != b) m_parent[std::max(a, b)] = std::min(a, b);
    }

private:
    std::vector<size_t> m_parent;
};

//! Transactions that are mined together: one cluster mempool chunk.
struct Chunk {
    FeePerWeight fee_rate;
    int64_t weight{0};
    int64_t min_ancestor_count{std::numeric_limits<int64_t>::max()};
    size_t cluster{0};
    std::vector<size_t> txs;
};

double ChunkFeeRate(const MempoolTx& tx)
{
    return tx.chunk_weight > 0 ? tx.chunk_fee * 4.0 / tx.chunk_weight : 0.0;
}

bool SameChunkFeeRate(const MempoolTx& a, const MempoolTx& b)
{
    return a.chunk_fee == b.chunk_fee && a.chunk_weight == b.chunk_weight;
}

std::vector<Chunk> BuildChunks(const std::vector<MempoolTx>& txs)
{
    DisjointSets clusters{txs.size()};
    DisjointSets chunks{txs.size()};
    for (size_t i = 0; i < txs.size(); ++i) {
        for (const size_t parent : txs[i].parents) {
            clusters.join(i, parent);
            // getrawmempool doesn't name chunks, but a parent and child that
            // report the same chunk fee and weight are mined together.
            if (SameChunkFeeRate(txs[i], txs[parent])) chunks.join(i, parent);
        }
    }

    std::unordered_map<size_t, size_t> chunk_by_root;
    std::vector<Chunk> result;
    for (size_t i = 0; i < txs.size(); ++i) {
        const auto [it, inserted] = chunk_by_root.try_emplace(chunks.find(i), result.size());
        if (inserted) {
            Chunk& chunk = result.emplace_back();
            chunk.fee_rate = FeePerWeight{txs[i].chunk_fee, txs[i].chunk_weight};
            chunk.cluster = clusters.find(i);
        }
        Chunk& chunk = result[it->second];
        chunk.weight += txs[i].vsize * WITNESS_SCALE_FACTOR;
        chunk.min_ancestor_count = std::min(chunk.min_ancestor_count, txs[i].ancestor_count);
        chunk.txs.push_back(i);
    }

    // Mining order. Within a cluster, chunk fee rates never increase, and an
    // equal-rate parent chunk has fewer ancestors than its children.
    std::stable_sort(result.begin(), result.end(), [](const Chunk& a, const Chunk& b) {
        if (ByRatio{a.fee_rate} != ByRatio{b.fee_rate}) return ByRatio{a.fee_rate} > ByRatio{b.fee_rate};
        return a.min_ancestor_count < b.min_ancestor_count;
    });
    return result;
}

struct BlockTx {
    int64_t weight{0};
    double fee_rate{0.0};
};

double Percentile(const std::vector<BlockTx>& sorted, int percentile)
{
    if (sorted.empty()) return 0.0;
    return sorted[(sorted.size() - 1) * percentile / 100].fee_rate;
}

//! Same statistics as mempool.space's calcEffectiveFeeStatistics(), so the
//! blocks read the same way.
void SetFeeStatistics(ProjectedBlock& block, const std::vector<BlockTx>& block_order)
{
    if (block_order.empty()) {
        block.fee_range.assign(7, 0.0);
        return;
    }

    std::vector<BlockTx> sorted{block_order};
    std::stable_sort(sorted.begin(), sorted.end(), [](const BlockTx& a, const BlockTx& b) {
        return a.fee_rate < b.fee_rate;
    });

    // The median is the average rate of the middle 0.25% of the block, with
    // unused space counted as zero fee at the bottom. An overfull last block
    // is measured on its own size.
    const int64_t block_weight{std::max(int64_t{MAX_BLOCK_WEIGHT}, block.weight)};
    const int64_t half_width{block_weight / 800};
    const int64_t left_bound{block_weight / 2 - half_width};
    const int64_t right_bound{block_weight / 2 + half_width};
    int64_t weight_count{block_weight - block.weight};
    double median_fees{0.0};
    int64_t median_weight{0};
    for (size_t i = 0; i < sorted.size() && weight_count < right_bound; ++i) {
        const int64_t left{weight_count};
        const int64_t right{weight_count + sorted[i].weight};
        if (right > left_bound) {
            const int64_t weight{std::min(right, right_bound) - std::max(left, left_bound)};
            median_fees += sorted[i].fee_rate * weight;
            median_weight += weight;
        }
        weight_count = right;
    }
    block.median_fee_rate = median_weight > 0 ? median_fees / median_weight : 0.0;

    // Trim outliers: the lowest rate is the lower of the 1st percentile and
    // the last 2% of the block, the highest the higher of the 99th percentile
    // and the first 2% of the block.
    const size_t count{block_order.size()};
    double min_rate{Percentile(sorted, 1)};
    for (size_t i = (count * 49 + 49) / 50; i < count; ++i) {
        min_rate = std::min(min_rate, block_order[i].fee_rate);
    }
    double max_rate{Percentile(sorted, 99)};
    for (size_t i = 0; i < count / 50; ++i) {
        max_rate = std::max(max_rate, block_order[i].fee_rate);
    }

    block.fee_range = {
        min_rate,
        Percentile(sorted, 10),
        Percentile(sorted, 25),
        Percentile(sorted, 50),
        Percentile(sorted, 75),
        Percentile(sorted, 90),
        max_rate,
    };
}
double RoundUpToIncrement(double fee_rate)
{
    return std::ceil(fee_rate / FEE_RATE_INCREMENT) * FEE_RATE_INCREMENT;
}

double RoundToIncrement(double fee_rate)
{
    return std::round(fee_rate / FEE_RATE_INCREMENT) * FEE_RATE_INCREMENT;
}

//! mempool.space's optimizeMedianFee(): the rate to get into a block, averaged
//! with the rate of the priority above it, and at least min_fee. A block less
//! than half full needs no more than the minimum, and a last block that is not
//! quite full needs proportionally less.
double OptimizeMedianFee(const ProjectedBlock& block, bool has_next_block, std::optional<double> previous_fee, double min_fee)
{
    const double vsize{static_cast<double>(block.weight) / WITNESS_SCALE_FACTOR};
    const double use_fee{previous_fee ? (block.median_fee_rate + *previous_fee) / 2 : block.median_fee_rate};
    if (vsize <= 500'000 || block.median_fee_rate < min_fee) return min_fee;
    if (vsize <= 950'000 && !has_next_block) {
        const double multiplier{(vsize - 500'000) / 500'000};
        return std::max(RoundToIncrement(use_fee * multiplier), min_fee);
    }
    return std::max(RoundUpToIncrement(use_fee), min_fee);
}
} // namespace

std::vector<MempoolTx> ParseRawMempool(const UniValue& verbose_mempool)
{
    const std::vector<std::string>& txids{verbose_mempool.get_obj().getKeys()};
    const std::vector<UniValue>& entries{verbose_mempool.getValues()};

    std::unordered_map<std::string, size_t> index_by_txid;
    index_by_txid.reserve(txids.size());
    for (size_t i = 0; i < txids.size(); ++i) {
        index_by_txid.emplace(txids[i], i);
    }

    std::vector<MempoolTx> txs(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const UniValue& entry{entries[i].get_obj()};
        const UniValue& fees{entry["fees"].get_obj()};
        MempoolTx& tx{txs[i]};
        tx.vsize = entry["vsize"].getInt<int64_t>();
        tx.fee = ParseAmount(fees["base"]);
        tx.ancestor_count = entry["ancestorcount"].getInt<int64_t>();
        if (const UniValue& chunk_weight{entry["chunkweight"]}; !chunk_weight.isNull()) {
            tx.chunk_weight = chunk_weight.getInt<int32_t>();
            tx.chunk_fee = ParseAmount(fees["chunk"]);
        } else {
            tx.chunk_weight = static_cast<int32_t>(tx.vsize * WITNESS_SCALE_FACTOR);
            tx.chunk_fee = ParseAmount(fees["modified"]);
        }
        for (const UniValue& parent : entry["depends"].get_array().getValues()) {
            const auto it{index_by_txid.find(parent.get_str())};
            if (it != index_by_txid.end()) tx.parents.push_back(it->second);
        }
    }
    return txs;
}

std::vector<std::string> BlockStatsFields()
{
    return {"height", "time", "txs", "totalfee", "total_weight", "feerate_percentiles", "minfeerate", "maxfeerate"};
}

MinedBlock ParseBlockStats(const UniValue& stats, std::string hash)
{
    MinedBlock block;
    block.hash = std::move(hash);
    block.height = stats["height"].getInt<int>();
    block.time = stats["time"].getInt<int64_t>();
    block.tx_count = stats["txs"].getInt<int>();
    block.total_fees = stats["totalfee"].getInt<CAmount>();
    block.weight = stats["total_weight"].getInt<int64_t>();

    const std::vector<UniValue>& percentiles{stats["feerate_percentiles"].get_array().getValues()};
    if (percentiles.size() != 5) throw std::runtime_error("invalid fee rate percentiles in block stats");
    block.fee_range.push_back(stats["minfeerate"].get_real());
    for (const UniValue& percentile : percentiles) {
        block.fee_range.push_back(percentile.get_real());
    }
    block.fee_range.push_back(stats["maxfeerate"].get_real());
    block.median_fee_rate = block.fee_range[3];
    return block;
}

MempoolInfo ParseMempoolInfo(const UniValue& mempool_info)
{
    MempoolInfo info;
    info.min_fee = ParseAmount(mempool_info["mempoolminfee"]);
    info.min_relay_fee = ParseAmount(mempool_info["minrelaytxfee"]);
    info.transaction_count = mempool_info["size"].getInt<int64_t>();
    info.usage = mempool_info["usage"].getInt<int64_t>();
    info.max_usage = mempool_info["maxmempool"].getInt<int64_t>();
    return info;
}

RecommendedFees RecommendFees(const std::vector<ProjectedBlock>& blocks, CAmount mempool_min_fee)
{
    // One sat/kvB is the rounding increment, so the minimum needs no rounding.
    const double minimum{static_cast<double>(std::max<CAmount>(mempool_min_fee, 1)) / 1000};
    RecommendedFees fees{minimum, minimum, minimum, minimum, minimum};
    if (!blocks.empty()) {
        const double first{OptimizeMedianFee(blocks[0], blocks.size() > 1, std::nullopt, minimum)};
        const double second{blocks.size() > 1 ? OptimizeMedianFee(blocks[1], blocks.size() > 2, first, minimum) : minimum};
        const double third{blocks.size() > 2 ? OptimizeMedianFee(blocks[2], blocks.size() > 3, second, minimum) : minimum};
        fees.economy = std::min(2 * minimum, third);
        // Higher priorities never pay less.
        fees.hour = std::max(third, fees.economy);
        fees.half_hour = std::max(second, fees.hour);
        fees.fastest = std::max(first, fees.half_hour);
    }
    fees.fastest = std::max(fees.fastest + PRIORITY_OFFSET, MIN_FASTEST_FEE);
    fees.half_hour = std::max(fees.half_hour + PRIORITY_OFFSET / 2, MIN_HALF_HOUR_FEE);
    // To the nearest sat/kvB.
    for (double* fee_rate : {&fees.fastest, &fees.half_hour, &fees.hour, &fees.economy, &fees.minimum}) {
        *fee_rate = std::round(*fee_rate * 1000) / 1000;
    }
    return fees;
}

std::vector<ProjectedBlock> ProjectBlocks(const std::vector<MempoolTx>& txs, size_t max_blocks)
{
    std::vector<ProjectedBlock> blocks;
    if (txs.empty() || max_blocks == 0) return blocks;

    const std::vector<Chunk> chunks{BuildChunks(txs)};
    std::vector<bool> included(chunks.size(), false);
    // Clusters skipped while filling block N are marked with N + 1.
    std::vector<size_t> skipped_in_block(txs.size(), 0);
    size_t first_pending{0};

    while (first_pending < chunks.size() && blocks.size() < max_blocks) {
        const bool last_block{blocks.size() + 1 == max_blocks};
        const size_t block_number{blocks.size() + 1};
        ProjectedBlock block;
        std::vector<BlockTx> block_order;
        int64_t block_weight{BLOCK_RESERVED_WEIGHT};
        int consecutive_failures{0};

        for (size_t i = first_pending; i < chunks.size(); ++i) {
            if (included[i]) continue;
            const Chunk& chunk{chunks[i]};
            if (!last_block) {
                if (skipped_in_block[chunk.cluster] == block_number) continue;
                if (block_weight + chunk.weight >= BLOCK_MAX_WEIGHT) {
                    // Later chunks of this cluster depend on this one.
                    skipped_in_block[chunk.cluster] = block_number;
                    if (++consecutive_failures > MAX_CONSECUTIVE_FAILURES &&
                        block_weight + BLOCK_FULL_ENOUGH_WEIGHT_DELTA > BLOCK_MAX_WEIGHT) {
                        break;
                    }
                    continue;
                }
            }

            included[i] = true;
            consecutive_failures = 0;
            block_weight += chunk.weight;
            for (const size_t tx_index : chunk.txs) {
                const MempoolTx& tx{txs[tx_index]};
                const int64_t tx_weight{tx.vsize * WITNESS_SCALE_FACTOR};
                block.weight += tx_weight;
                block.total_fees += tx.fee;
                ++block.tx_count;
                block_order.push_back(BlockTx{tx_weight, ChunkFeeRate(tx)});
            }
        }

        while (first_pending < chunks.size() && included[first_pending]) ++first_pending;
        if (block.tx_count == 0) break;
        SetFeeStatistics(block, block_order);
        blocks.push_back(std::move(block));
    }
    return blocks;
}

} // namespace mempool_blocks
