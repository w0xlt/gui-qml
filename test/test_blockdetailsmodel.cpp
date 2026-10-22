// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/stubnode.h>
#include <test/qt_test_registry.h>

#include <qml/models/blockdetailsmodel.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <uint256.h>
#include <univalue.h>

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <QSignalSpy>

using block_details::ParseBlock;
using block_details::ParseMedianFeeRate;

namespace {
constexpr int ASYNC_TIMEOUT_MS{1'000};

uint256 Hash(char c) { return *uint256::FromHex(std::string(64, c)); }

struct TestBlock {
    int height;
    uint256 previous_hash;
    int64_t time;
    int transactions;
    int64_t size;
    int64_t median_fee_rate;
    //! Its block and undo data were pruned.
    bool pruned{false};
};

//! A chain of blocks tests set, read with RPCs from the model's worker thread.
class ChainNode : public StubNode
{
public:
    UniValue executeRpc(const std::string& method, const UniValue& params, const std::string&) override
    {
        std::lock_guard lock{m_mutex};
        ++m_calls[method];
        const auto it{m_blocks.find(*uint256::FromHex(params[0].get_str()))};
        if (it == m_blocks.end()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        const TestBlock& block{it->second};
        if (method == "getblock" || method == "getblockheader") {
            if (method == "getblock" && block.pruned) throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");
            UniValue result{UniValue::VOBJ};
            result.pushKV("height", block.height);
            if (!block.previous_hash.IsNull()) result.pushKV("previousblockhash", block.previous_hash.GetHex());
            result.pushKV("time", block.time);
            result.pushKV("nTx", block.transactions);
            if (method == "getblock") result.pushKV("size", block.size);
            return result;
        }
        if (method == "getblockstats") {
            if (block.pruned) throw JSONRPCError(RPC_MISC_ERROR, "Undo data not available (pruned data)");
            UniValue percentiles{UniValue::VARR};
            for (int i{0}; i < 5; ++i) percentiles.push_back(block.median_fee_rate + i - 2);
            UniValue result{UniValue::VOBJ};
            result.pushKV("feerate_percentiles", std::move(percentiles));
            return result;
        }
        throw std::runtime_error("unexpected method " + method);
    }

    void addBlock(const uint256& hash, TestBlock block)
    {
        std::lock_guard lock{m_mutex};
        m_blocks[hash] = std::move(block);
    }
    int calls(const std::string& method)
    {
        std::lock_guard lock{m_mutex};
        return m_calls[method];
    }

private:
    std::mutex m_mutex;
    std::map<uint256, TestBlock> m_blocks;
    std::map<std::string, int> m_calls;
};

//! Blocks 99 to 103, about ten minutes apart, except 101 is timestamped
//! before 100, and 103 at the same time as 102.
class Fixture
{
public:
    Fixture()
    {
        node.addBlock(Hash('a'), {.height = 99, .previous_hash = {}, .time = 10'000, .transactions = 1, .size = 250, .median_fee_rate = 0});
        node.addBlock(Hash('b'), {.height = 100, .previous_hash = Hash('a'), .time = 10'600, .transactions = 3'114, .size = 1'210'000, .median_fee_rate = 2});
        node.addBlock(Hash('c'), {.height = 101, .previous_hash = Hash('b'), .time = 10'500, .transactions = 1'000, .size = 400'000, .median_fee_rate = 5});
        node.addBlock(Hash('d'), {.height = 102, .previous_hash = Hash('c'), .time = 11'800, .transactions = 1, .size = 250, .median_fee_rate = 0});
        node.addBlock(Hash('e'), {.height = 103, .previous_hash = Hash('d'), .time = 11'800, .transactions = 2, .size = 500, .median_fee_rate = 1});
    }

    BlockDetailsModel::BlockFinder finder()
    {
        return [this](int64_t time, int ordinal) -> std::optional<uint256> {
            ++finds;
            for (const auto& [hash, block_time] : std::vector<std::pair<uint256, int64_t>>{
                     {Hash('a'), 10'000}, {Hash('b'), 10'600}, {Hash('c'), 10'500}, {Hash('d'), 11'800}, {Hash('e'), 11'800}}) {
                if (block_time == time && ordinal-- == 0) return hash;
            }
            return std::nullopt;
        };
    }

    ChainNode node;
    std::atomic<int> finds{0};
};
} // namespace

class BlockDetailsModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesBlocks();
    void malformedResultsThrow();
    void describesTheBlock();
    void readsEachBlockOnce();
    void prunedBlocksKeepTheirHeaders();
    void outOfOrderTimestampsHaveNoInterval();
    void selectsBlocksSharingATimestamp();
    void keepsOnlyTheLatestSelection();
    void selectingNoBlockClears();
    void shutsDownWhileWaiting();
};

void BlockDetailsModelTests::parsesBlocks()
{
    UniValue block{UniValue::VOBJ};
    block.pushKV("height", 100);
    block.pushKV("previousblockhash", Hash('a').GetHex());
    block.pushKV("time", 10'600);
    block.pushKV("nTx", 3'114);
    block.pushKV("size", 1'210'000);
    QCOMPARE(ParseBlock(block), (block_details::Block{
        .height = 100,
        .previous_block_hash = Hash('a').GetHex(),
        .time = 10'600,
        .previous_time = std::nullopt,
        .transaction_count = 3'114,
        .size = 1'210'000,
        .median_fee_rate = std::nullopt,
    }));

    // A header has no size, and the genesis block no previous block.
    UniValue header{UniValue::VOBJ};
    header.pushKV("height", 0);
    header.pushKV("time", 1'296'688'602);
    header.pushKV("nTx", 1);
    const block_details::Block genesis{ParseBlock(header)};
    QVERIFY(genesis.previous_block_hash.empty());
    QVERIFY(!genesis.size);

    UniValue stats{UniValue::VOBJ};
    UniValue percentiles{UniValue::VARR};
    for (int rate : {1, 1, 2, 4, 9}) percentiles.push_back(rate);
    stats.pushKV("feerate_percentiles", std::move(percentiles));
    QCOMPARE(ParseMedianFeeRate(stats), 2.0);
}

void BlockDetailsModelTests::malformedResultsThrow()
{
    UniValue block{UniValue::VOBJ};
    block.pushKV("height", "100");
    block.pushKV("time", 10'600);
    block.pushKV("nTx", 1);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ParseBlock(block));

    UniValue stats{UniValue::VOBJ};
    stats.pushKV("feerate_percentiles", UniValue{UniValue::VARR});
    QVERIFY_THROWS_EXCEPTION(std::exception, ParseMedianFeeRate(stats));
}

void BlockDetailsModelTests::describesTheBlock()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    QVERIFY(!model.available());
    QCOMPARE(model.size(), -1);

    QSignalSpy changed{&model, &BlockDetailsModel::blockChanged};
    model.select(10'600);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.height(), 100);
    QCOMPARE(model.time(), 10'600'000);
    QCOMPARE(model.transactionCount(), 3'114);
    QCOMPARE(model.size(), 1'210'000);
    QCOMPARE(model.medianFeeRate(), 2.0);
    // Found ten minutes after the block it builds on.
    QCOMPARE(model.interval(), 600);

    model.clear();
    QVERIFY(!model.available());
    QCOMPARE(changed.count(), 2);

    // A block without transactions besides the coinbase has no fee rates.
    model.select(11'800);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.height(), 102);
    QCOMPARE(model.medianFeeRate(), -1.0);
    QCOMPARE(fixture.node.calls("getblockstats"), 1);
    model.beginShutdown();
}

void BlockDetailsModelTests::readsEachBlockOnce()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    model.select(10'600);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 100, ASYNC_TIMEOUT_MS);
    model.select(10'500);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 101, ASYNC_TIMEOUT_MS);
    model.select(10'600);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 100, ASYNC_TIMEOUT_MS);

    QCOMPARE(fixture.finds.load(), 3);
    QCOMPARE(fixture.node.calls("getblock"), 2);
    QCOMPARE(fixture.node.calls("getblockstats"), 2);
    model.beginShutdown();
}

void BlockDetailsModelTests::prunedBlocksKeepTheirHeaders()
{
    Fixture fixture;
    fixture.node.addBlock(Hash('b'), {.height = 100, .previous_hash = Hash('a'), .time = 10'600, .transactions = 3'114, .size = 1'210'000, .median_fee_rate = 2, .pruned = true});
    BlockDetailsModel model{fixture.node, fixture.finder()};
    model.select(10'600);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.height(), 100);
    QCOMPARE(model.transactionCount(), 3'114);
    QCOMPARE(model.size(), -1);
    QCOMPARE(model.medianFeeRate(), -1.0);
    QCOMPARE(model.interval(), 600);
    QCOMPARE(fixture.node.calls("getblockstats"), 0);
    model.beginShutdown();
}

void BlockDetailsModelTests::outOfOrderTimestampsHaveNoInterval()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    // Block 101 is timestamped before block 100, which it builds on.
    model.select(10'500);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.height(), 101);
    QCOMPARE(model.interval(), -1);

    // The genesis block builds on none.
    model.select(10'000);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 99, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.interval(), -1);
    model.beginShutdown();
}

void BlockDetailsModelTests::selectsBlocksSharingATimestamp()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    model.select(11'800, 1);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 103, ASYNC_TIMEOUT_MS);
    // Found at the same time as the block it builds on.
    QCOMPARE(model.interval(), -1);
    model.select(11'800);
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 102, ASYNC_TIMEOUT_MS);
    model.beginShutdown();
}

void BlockDetailsModelTests::keepsOnlyTheLatestSelection()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    QSignalSpy changed{&model, &BlockDetailsModel::blockChanged};
    model.select(10'600);
    model.select(11'800);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.height(), 102);

    // A selection cleared before it is read is not shown.
    model.select(10'600);
    model.clear();
    QTest::qWait(50);
    QVERIFY(!model.available());
    QCOMPARE(changed.count(), 2);
    model.beginShutdown();
}

void BlockDetailsModelTests::selectingNoBlockClears()
{
    Fixture fixture;
    BlockDetailsModel model{fixture.node, fixture.finder()};
    model.select(10'600);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);

    // E.g. after a reorganization.
    model.select(20'000);
    QTRY_VERIFY_WITH_TIMEOUT(!model.available(), ASYNC_TIMEOUT_MS);
    model.beginShutdown();
}

void BlockDetailsModelTests::shutsDownWhileWaiting()
{
    Fixture fixture;
    {
        // Without a selection, there is no worker to join.
        BlockDetailsModel model{fixture.node, fixture.finder()};
        QSignalSpy drained{&model, &BlockDetailsModel::drained};
        model.beginShutdown();
        QCOMPARE(drained.count(), 1);
        model.select(10'600);
        QTest::qWait(50);
        QVERIFY(!model.available());
    }

    BlockDetailsModel model{fixture.node, fixture.finder()};
    model.select(10'600);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QSignalSpy drained{&model, &BlockDetailsModel::drained};
    model.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, ASYNC_TIMEOUT_MS);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockDetailsModelTests)
#else
QTEST_MAIN(BlockDetailsModelTests)
#endif
#include "test_blockdetailsmodel.moc"
