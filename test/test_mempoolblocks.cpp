// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/qt_test_registry.h>

#include <core_io.h>
#include <qml/models/mempoolblocks.h>
#include <univalue.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using mempool_blocks::MempoolTx;
using mempool_blocks::MinedBlock;
using mempool_blocks::ProjectBlocks;
using mempool_blocks::ProjectedBlock;

namespace {
constexpr int64_t LARGE_TX_VSIZE{100'000};

MempoolTx Tx(int64_t vsize, CAmount fee, std::vector<size_t> parents = {})
{
    MempoolTx tx;
    tx.vsize = vsize;
    tx.fee = fee;
    tx.chunk_fee = fee;
    tx.chunk_weight = static_cast<int32_t>(vsize * 4);
    tx.ancestor_count = 1 + static_cast<int64_t>(parents.size());
    tx.parents = std::move(parents);
    return tx;
}

//! Transactions of LARGE_TX_VSIZE, nine of which fill a block.
std::vector<MempoolTx> LargeTxs(int count)
{
    std::vector<MempoolTx> txs;
    for (int i = 0; i < count; ++i) {
        txs.push_back(Tx(LARGE_TX_VSIZE, (100 - i) * LARGE_TX_VSIZE));
    }
    return txs;
}

UniValue Entry(int64_t vsize, CAmount base, CAmount modified, std::vector<std::string> depends)
{
    UniValue fees{UniValue::VOBJ};
    fees.pushKV("base", ValueFromAmount(base));
    fees.pushKV("modified", ValueFromAmount(modified));
    UniValue parents{UniValue::VARR};
    for (const std::string& parent : depends) {
        parents.push_back(parent);
    }
    UniValue entry{UniValue::VOBJ};
    entry.pushKV("vsize", vsize);
    entry.pushKV("ancestorcount", static_cast<int64_t>(depends.size() + 1));
    entry.pushKV("fees", std::move(fees));
    entry.pushKV("depends", std::move(parents));
    return entry;
}

UniValue WithChunk(UniValue entry, CAmount chunk_fee, int32_t chunk_weight)
{
    entry.pushKV("chunkweight", chunk_weight);
    UniValue fees{entry["fees"]};
    fees.pushKV("chunk", ValueFromAmount(chunk_fee));
    entry.pushKV("fees", std::move(fees));
    return entry;
}

std::vector<int> TxCounts(const std::vector<ProjectedBlock>& blocks)
{
    std::vector<int> counts;
    for (const ProjectedBlock& block : blocks) {
        counts.push_back(block.tx_count);
    }
    return counts;
}
} // namespace

class MempoolBlocksTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesVerboseMempool();
    void rejectsMalformedMempool();
    void emptyMempoolHasNoBlocks();
    void cutsBlocksAtWeightLimit();
    void lastBlockHoldsTheRest();
    void childPaysForParentRaisesChunk();
    void skipsRestOfClusterWhenChunkDoesNotFit();
    void medianIsWeightedAndCountsUnusedSpace();
    void parsesBlockStats();
    void rejectsMalformedBlockStats();
};

void MempoolBlocksTests::parsesVerboseMempool()
{
    UniValue mempool{UniValue::VOBJ};
    mempool.pushKV("aa", WithChunk(Entry(200, 1'000, 1'000, {}), 5'000, 1'000));
    mempool.pushKV("bb", WithChunk(Entry(50, 4'000, 4'000, {"aa", "ff"}), 5'000, 1'000));
    mempool.pushKV("cc", Entry(100, 300, 700, {}));

    const std::vector<MempoolTx> txs{mempool_blocks::ParseRawMempool(mempool)};
    QCOMPARE(txs.size(), size_t{3});

    QCOMPARE(txs[0].vsize, int64_t{200});
    QCOMPARE(txs[0].fee, CAmount{1'000});
    QCOMPARE(txs[0].chunk_fee, CAmount{5'000});
    QCOMPARE(txs[0].chunk_weight, int32_t{1'000});
    QVERIFY(txs[0].parents.empty());

    // Parents outside the snapshot are ignored.
    QCOMPARE(txs[1].parents, std::vector<size_t>{0});
    QCOMPARE(txs[1].ancestor_count, int64_t{3});

    // Without cluster mempool fields, a transaction is its own chunk.
    QCOMPARE(txs[2].chunk_fee, CAmount{700});
    QCOMPARE(txs[2].chunk_weight, int32_t{400});
}

void MempoolBlocksTests::rejectsMalformedMempool()
{
    UniValue mempool{UniValue::VOBJ};
    UniValue entry{UniValue::VOBJ};
    entry.pushKV("vsize", 100);
    mempool.pushKV("aa", std::move(entry));

    bool threw{false};
    try {
        mempool_blocks::ParseRawMempool(mempool);
    } catch (const std::exception&) {
        threw = true;
    }
    QVERIFY(threw);
}

void MempoolBlocksTests::emptyMempoolHasNoBlocks()
{
    QVERIFY(ProjectBlocks({}).empty());
    QVERIFY(ProjectBlocks(LargeTxs(3), /*max_blocks=*/0).empty());
}

void MempoolBlocksTests::cutsBlocksAtWeightLimit()
{
    // A block holds 3,992,000 WU after the coinbase reservation: nine 400,000 WU
    // transactions fit, a tenth doesn't.
    const std::vector<ProjectedBlock> blocks{ProjectBlocks(LargeTxs(25))};
    QCOMPARE(TxCounts(blocks), (std::vector<int>{9, 9, 7}));

    const ProjectedBlock& next{blocks.front()};
    QCOMPARE(next.weight, int64_t{9 * LARGE_TX_VSIZE * 4});
    QCOMPARE(next.total_fees, CAmount{(100 + 92) * 9 / 2 * LARGE_TX_VSIZE});
    QCOMPARE(next.fee_range.size(), size_t{7});
    QCOMPARE(next.fee_range.front(), 92.0);
    // The top rate is trimmed to the 99th percentile, like mempool.space.
    QCOMPARE(next.fee_range.back(), 99.0);
    QCOMPARE(blocks.back().fee_range.front(), 76.0);
}

void MempoolBlocksTests::lastBlockHoldsTheRest()
{
    const std::vector<ProjectedBlock> blocks{ProjectBlocks(LargeTxs(25), /*max_blocks=*/2)};
    QCOMPARE(TxCounts(blocks), (std::vector<int>{9, 16}));
    QCOMPARE(blocks.back().weight, int64_t{16 * LARGE_TX_VSIZE * 4});
}

void MempoolBlocksTests::childPaysForParentRaisesChunk()
{
    // Nine fillers leave room for 98,000 vB. The parent pays 1 sat/vB, but its
    // child lifts their chunk to 10 sat/vB, ahead of a 5 sat/vB transaction
    // that then no longer fits.
    std::vector<MempoolTx> txs{LargeTxs(9)};
    txs.push_back(Tx(10'000, 50'000));
    const size_t parent{txs.size()};
    txs.push_back(Tx(50'000, 50'000));
    txs.push_back(Tx(40'000, 850'000, {parent}));
    for (size_t i = parent; i < txs.size(); ++i) {
        txs[i].chunk_fee = 900'000;
        txs[i].chunk_weight = 90'000 * 4;
    }

    const std::vector<ProjectedBlock> blocks{ProjectBlocks(txs)};
    QCOMPARE(TxCounts(blocks), (std::vector<int>{11, 1}));
    QCOMPARE(blocks[0].fee_range.front(), 10.0);
    QCOMPARE(blocks[0].total_fees, CAmount{(100 + 92) * 9 / 2 * LARGE_TX_VSIZE + 900'000});
    QCOMPARE(blocks[1].fee_range.front(), 5.0);
}

void MempoolBlocksTests::skipsRestOfClusterWhenChunkDoesNotFit()
{
    // After nine fillers, the 20 sat/vB parent doesn't fit. Its 10 sat/vB
    // child would, but can't be mined before it. An unrelated 5 sat/vB
    // transaction takes the space instead.
    std::vector<MempoolTx> txs{LargeTxs(9)};
    const size_t parent{txs.size()};
    txs.push_back(Tx(99'000, 1'980'000));
    txs.push_back(Tx(1'000, 10'000, {parent}));
    txs.push_back(Tx(1'000, 5'000));

    const std::vector<ProjectedBlock> blocks{ProjectBlocks(txs)};
    QCOMPARE(TxCounts(blocks), (std::vector<int>{10, 2}));
    QCOMPARE(blocks[0].fee_range.front(), 5.0);
    QCOMPARE(blocks[1].total_fees, CAmount{1'990'000});
}

void MempoolBlocksTests::medianIsWeightedAndCountsUnusedSpace()
{
    // 400,000 WU of unused space, then 1,600,000 WU at 10 sat/vB: the middle of
    // the block falls half on 10 and half on 20 sat/vB.
    std::vector<MempoolTx> txs;
    for (int i = 0; i < 4; ++i) txs.push_back(Tx(LARGE_TX_VSIZE, 10 * LARGE_TX_VSIZE));
    for (int i = 0; i < 5; ++i) txs.push_back(Tx(LARGE_TX_VSIZE, 20 * LARGE_TX_VSIZE));
    const std::vector<ProjectedBlock> full{ProjectBlocks(txs)};
    QCOMPARE(full.size(), size_t{1});
    QCOMPARE(full[0].median_fee_rate, 15.0);
    QCOMPARE(full[0].fee_range, (std::vector<double>{10, 10, 10, 20, 20, 20, 20}));

    // A nearly empty block has a zero median, with its range from the
    // transaction itself.
    const std::vector<ProjectedBlock> sparse{ProjectBlocks({Tx(200, 600)})};
    QCOMPARE(sparse.size(), size_t{1});
    QCOMPARE(sparse[0].median_fee_rate, 0.0);
    QCOMPARE(sparse[0].fee_range, (std::vector<double>(7, 3.0)));
}

UniValue BlockStats(std::vector<int> percentiles)
{
    UniValue fee_rates{UniValue::VARR};
    for (const int fee_rate : percentiles) fee_rates.push_back(fee_rate);
    UniValue stats{UniValue::VOBJ};
    stats.pushKV("height", 840'000);
    stats.pushKV("time", 1'713'571'767);
    stats.pushKV("txs", 3'050);
    stats.pushKV("totalfee", 3'762'206);
    stats.pushKV("total_weight", 3'993'000);
    stats.pushKV("feerate_percentiles", std::move(fee_rates));
    stats.pushKV("minfeerate", 1);
    stats.pushKV("maxfeerate", 2'000);
    return stats;
}

void MempoolBlocksTests::parsesBlockStats()
{
    const MinedBlock block{mempool_blocks::ParseBlockStats(BlockStats({4, 9, 12, 20, 40}), "abc")};
    QCOMPARE(block.hash, std::string{"abc"});
    QCOMPARE(block.height, 840'000);
    QCOMPARE(block.time, int64_t{1'713'571'767});
    QCOMPARE(block.tx_count, 3'050);
    QCOMPARE(block.total_fees, CAmount{3'762'206});
    QCOMPARE(block.weight, int64_t{3'993'000});
    QCOMPARE(block.median_fee_rate, 12.0);
    QCOMPARE(block.fee_range, (std::vector<double>{1, 4, 9, 12, 20, 40, 2'000}));

    // Every field read is requested.
    const std::vector<std::string> fields{mempool_blocks::BlockStatsFields()};
    const UniValue stats{BlockStats({1, 2, 3, 4, 5})};
    for (const std::string& key : stats.getKeys()) {
        QVERIFY2(std::find(fields.begin(), fields.end(), key) != fields.end(), key.c_str());
    }
}

void MempoolBlocksTests::rejectsMalformedBlockStats()
{
    for (UniValue stats : {BlockStats({1, 2, 3, 4}), UniValue{UniValue::VOBJ}}) {
        bool threw{false};
        try {
            mempool_blocks::ParseBlockStats(stats, "abc");
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(MempoolBlocksTests)
#else
QTEST_MAIN(MempoolBlocksTests)
#endif
#include "test_mempoolblocks.moc"
