// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <core_io.h>
#include <qml/models/mempoolblocksmodel.h>
#include <univalue.h>
#include <util/time.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

#include <QSignalSpy>
#include <QThread>

using mempool_blocks::MinedBlock;
using mempool_blocks::ProjectedBlock;

namespace {
constexpr int TEST_REFRESH_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};

ProjectedBlock Block(int tx_count)
{
    ProjectedBlock block;
    block.weight = tx_count * 400;
    block.tx_count = tx_count;
    block.total_fees = tx_count * 1'000;
    block.median_fee_rate = 2.5;
    block.fee_range = {1, 1.5, 2, 2.5, 3, 3.5, 4};
    return block;
}

UniValue OneTxMempool()
{
    UniValue fees{UniValue::VOBJ};
    fees.pushKV("base", ValueFromAmount(1'000));
    fees.pushKV("modified", ValueFromAmount(1'000));
    fees.pushKV("chunk", ValueFromAmount(1'000));
    UniValue entry{UniValue::VOBJ};
    entry.pushKV("vsize", 250);
    entry.pushKV("ancestorcount", 1);
    entry.pushKV("chunkweight", 1'000);
    entry.pushKV("fees", std::move(fees));
    entry.pushKV("depends", UniValue{UniValue::VARR});
    UniValue mempool{UniValue::VOBJ};
    mempool.pushKV("aa", std::move(entry));
    return mempool;
}

int RowTxCount(const MempoolBlocksModel& model, int row)
{
    return model.data(model.index(row), MempoolBlocksModel::TransactionCountRole).toInt();
}

MinedBlock Mined(int height, const std::string& fork = "")
{
    MinedBlock block;
    block.hash = fork + "hash" + std::to_string(height);
    block.height = height;
    block.time = 1'700'000'000 + height * 600;
    block.tx_count = 10;
    block.total_fees = 5'000;
    block.weight = 400'000;
    block.median_fee_rate = 3;
    block.fee_range = {1, 2, 2.5, 3, 4, 5, 9};
    return block;
}

int64_t ChainTime(int height)
{
    return 1'700'000'000 + int64_t{height} * 600;
}

int MinedHeight(const MinedBlocksModel& model, int row)
{
    return model.data(model.index(row), MinedBlocksModel::HeightRole).toInt();
}

//! Answers the worker's RPCs for a chain of blocks up to the tip, one every 10
//! minutes with the difficulty rising by half each retarget, and a
//! one-transaction mempool with a minimum fee rate of 0.1 sat/vB. Stats of blocks below pruned_below are missing.
UniValue ChainRpc(const std::string& method, const UniValue& params, int tip, int pruned_below = 0)
{
    if (method == "getrawmempool") return OneTxMempool();
    if (method == "getmempoolinfo") {
        UniValue info{UniValue::VOBJ};
        info.pushKV("mempoolminfee", ValueFromAmount(100));
        return info;
    }
    if (method == "getblockhash") {
        const int height{params[0].getInt<int>()};
        if (height < 0 || height > tip) throw std::runtime_error("height out of range");
        return UniValue{"hash" + std::to_string(height)};
    }
    if (method == "getblockheader") {
        const int height{std::stoi(params[0].get_str().substr(4))};
        UniValue header{UniValue::VOBJ};
        header.pushKV("time", ChainTime(height));
        header.pushKV("difficulty", 1 + 0.5 * (height / difficulty_adjustment::RETARGET_INTERVAL));
        return header;
    }
    if (method == "getblockstats") {
        const int height{std::stoi(params[0].get_str().substr(4))};
        if (height < pruned_below) {
            UniValue error{UniValue::VOBJ};
            error.pushKV("code", -1);
            error.pushKV("message", "Block not available (pruned data)");
            throw error;
        }
        UniValue percentiles{UniValue::VARR};
        for (int fee_rate : {2, 3, 4, 5, 6}) percentiles.push_back(fee_rate);
        UniValue stats{UniValue::VOBJ};
        stats.pushKV("height", height);
        stats.pushKV("time", 1'700'000'000 + height * 600);
        stats.pushKV("txs", 10);
        stats.pushKV("totalfee", 5'000);
        stats.pushKV("total_weight", 400'000);
        stats.pushKV("feerate_percentiles", std::move(percentiles));
        stats.pushKV("minfeerate", 1);
        stats.pushKV("maxfeerate", 9);
        return stats;
    }
    throw std::runtime_error("unexpected method " + method);
}
} // namespace

class MempoolBlocksModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void exposesBlockRoles();
    void appliesSnapshotsAsRowChanges();
    void minedBlocksLeaveFromTheFront();
    void exposesRecommendedFees();
    void exposesMinedBlockRoles();
    void newMinedBlocksArriveAtTheFront();
    void readsEachMinedBlockOnce();
    void showsMinedBlocksUpToPrunedOnes();
    void estimatesRetargetAndHalvingFromHeaders();
    void noEstimatesWhileSyncing();
    void reportsWhetherTheChainRetargets();
    void fetchesOffGuiThreadOnlyWhileActiveAfterStart();
    void rpcErrorsKeepPreviousBlocks();
    void neverFetchesAfterShutdown();
};

void MempoolBlocksModelTests::exposesBlockRoles()
{
    MockNode node;
    MempoolBlocksModel model{node};
    model.applySnapshot(100, {Block(3)});

    const QModelIndex index{model.index(0)};
    QCOMPARE(model.data(index, MempoolBlocksModel::HeightRole).toInt(), 101);
    QCOMPARE(model.data(index, MempoolBlocksModel::MedianFeeRateRole).toDouble(), 2.5);
    QCOMPARE(model.data(index, MempoolBlocksModel::MinFeeRateRole).toDouble(), 1.0);
    QCOMPARE(model.data(index, MempoolBlocksModel::MaxFeeRateRole).toDouble(), 4.0);
    QCOMPARE(model.data(index, MempoolBlocksModel::FeeRangeRole).toList().size(), 7);
    QCOMPARE(model.data(index, MempoolBlocksModel::TotalFeesRole).toLongLong(), 3'000);
    QCOMPARE(model.data(index, MempoolBlocksModel::TransactionCountRole).toInt(), 3);
    QCOMPARE(model.data(index, MempoolBlocksModel::VSizeRole).toLongLong(), 300);
    QVERIFY(!model.data(model.index(1), MempoolBlocksModel::HeightRole).isValid());

    const QHash<int, QByteArray> roles{model.roleNames()};
    QCOMPARE(roles.value(MempoolBlocksModel::FeeRangeRole), QByteArray("feeRange"));
    QCOMPARE(roles.value(MempoolBlocksModel::VSizeRole), QByteArray("vsize"));
}

void MempoolBlocksModelTests::appliesSnapshotsAsRowChanges()
{
    MockNode node;
    MempoolBlocksModel model{node};
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    QSignalSpy loaded{&model, &MempoolBlocksModel::loadedChanged};
    QSignalSpy count{&model, &MempoolBlocksModel::countChanged};
    QVERIFY(!model.loaded());

    model.applySnapshot(100, {Block(1), Block(2), Block(3)});
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.loaded());
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(count.size(), 1);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted[0][1].toInt(), 0);
    QCOMPARE(inserted[0][2].toInt(), 2);

    // Same tip: rows update in place, extra rows go from the end.
    model.applySnapshot(100, {Block(4), Block(5)});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed[0][1].toInt(), 2);
    QCOMPARE(removed[0][2].toInt(), 2);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(RowTxCount(model, 0), 4);
    QCOMPARE(RowTxCount(model, 1), 5);

    model.applySnapshot(100, {Block(6), Block(7)});
    QCOMPARE(count.size(), 2);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(removed.size(), 1);
}

void MempoolBlocksModelTests::exposesRecommendedFees()
{
    MockNode node;
    MempoolBlocksModel model{node};
    QSignalSpy changed{&model, &MempoolBlocksModel::recommendedFeesChanged};
    const mempool_blocks::RecommendedFees fees{.fastest = 12, .half_hour = 8.5, .hour = 5, .economy = 2, .minimum = 1};
    model.applySnapshot(100, {Block(1)}, {}, fees);
    QCOMPARE(changed.size(), 1);
    const QVariantMap map{model.recommendedFees()};
    QCOMPARE(map.value(QStringLiteral("fastest")).toDouble(), 12.0);
    QCOMPARE(map.value(QStringLiteral("halfHour")).toDouble(), 8.5);
    QCOMPARE(map.value(QStringLiteral("hour")).toDouble(), 5.0);
    QCOMPARE(map.value(QStringLiteral("economy")).toDouble(), 2.0);
    QCOMPARE(map.value(QStringLiteral("minimum")).toDouble(), 1.0);

    model.applySnapshot(100, {Block(2)}, {}, fees);
    QCOMPARE(changed.size(), 1);
}

void MempoolBlocksModelTests::minedBlocksLeaveFromTheFront()
{
    MockNode node;
    MempoolBlocksModel model{node};
    model.applySnapshot(100, {Block(1), Block(2), Block(3)});
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};

    model.applySnapshot(101, {Block(2), Block(3), Block(4)});
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed[0][1].toInt(), 0);
    QCOMPARE(removed[0][2].toInt(), 0);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted[0][1].toInt(), 2);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), MempoolBlocksModel::HeightRole).toInt(), 102);
    QCOMPARE(RowTxCount(model, 0), 2);
    QCOMPARE(RowTxCount(model, 2), 4);

    // More blocks mined than shown: every row leaves.
    removed.clear();
    model.applySnapshot(110, {Block(9)});
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed[0][1].toInt(), 0);
    QCOMPARE(removed[0][2].toInt(), 2);
    QCOMPARE(model.rowCount(), 1);

    // A reorg to a lower tip updates in place.
    removed.clear();
    model.applySnapshot(105, {Block(8)});
    QCOMPARE(removed.size(), 0);
    QCOMPARE(RowTxCount(model, 0), 8);
}

void MempoolBlocksModelTests::exposesMinedBlockRoles()
{
    MockNode node;
    MempoolBlocksModel model{node};
    model.applySnapshot(100, {Block(1)}, {Mined(100), Mined(99)});
    const MinedBlocksModel& mined{*model.minedBlocks()};
    QCOMPARE(mined.rowCount(), 2);

    const QModelIndex index{mined.index(0)};
    QCOMPARE(mined.data(index, MinedBlocksModel::HeightRole).toInt(), 100);
    QCOMPARE(mined.data(index, MinedBlocksModel::TimeRole).toLongLong(), 1'700'060'000);
    QCOMPARE(mined.data(index, MinedBlocksModel::MedianFeeRateRole).toDouble(), 3.0);
    // The range shown starts at the 10th percentile.
    QCOMPARE(mined.data(index, MinedBlocksModel::MinFeeRateRole).toDouble(), 2.0);
    QCOMPARE(mined.data(index, MinedBlocksModel::MaxFeeRateRole).toDouble(), 9.0);
    QCOMPARE(mined.data(index, MinedBlocksModel::FeeRangeRole).toList().size(), 7);
    QCOMPARE(mined.data(index, MinedBlocksModel::TotalFeesRole).toLongLong(), 5'000);
    QCOMPARE(mined.data(index, MinedBlocksModel::TransactionCountRole).toInt(), 10);
    QCOMPARE(mined.data(index, MinedBlocksModel::VSizeRole).toLongLong(), 100'000);
    QVERIFY(!mined.data(mined.index(2), MinedBlocksModel::HeightRole).isValid());
    QCOMPARE(mined.roleNames().value(MinedBlocksModel::TimeRole), QByteArray("time"));
}

void MempoolBlocksModelTests::newMinedBlocksArriveAtTheFront()
{
    MinedBlocksModel model;
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    QSignalSpy count{&model, &MinedBlocksModel::countChanged};

    model.setBlocks({Mined(100), Mined(99), Mined(98)});
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted[0][2].toInt(), 2);
    QCOMPARE(count.size(), 1);

    // A new tip: it arrives at the front and the oldest block leaves.
    inserted.clear();
    model.setBlocks({Mined(101), Mined(100), Mined(99)});
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed[0][1].toInt(), 2);
    QCOMPARE(removed[0][2].toInt(), 2);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted[0][1].toInt(), 0);
    QCOMPARE(inserted[0][2].toInt(), 0);
    QCOMPARE(MinedHeight(model, 0), 101);
    QCOMPARE(MinedHeight(model, 2), 99);
    QCOMPARE(count.size(), 1);

    // Nothing new.
    inserted.clear();
    removed.clear();
    model.setBlocks({Mined(101), Mined(100), Mined(99)});
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(reset.size(), 0);

    // A reorg replaces the blocks.
    model.setBlocks({Mined(101, "fork"), Mined(100), Mined(99)});
    QCOMPARE(reset.size(), 1);
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(model.data(model.index(0), MinedBlocksModel::HeightRole).toInt(), 101);

    model.setBlocks({});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(count.size(), 2);
}

void MempoolBlocksModelTests::readsEachMinedBlockOnce()
{
    MockNode node;
    std::atomic<int> tip{100};
    std::atomic<int> stats_calls{0};
    node.get_num_blocks_fn = [&] { return tip.load(); };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) {
        if (method == "getblockstats") ++stats_calls;
        return ChainRpc(method, params, tip);
    };

    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    const MinedBlocksModel& mined{*model.minedBlocks()};
    model.setActive(true);
    model.start();
    QTRY_COMPARE_WITH_TIMEOUT(mined.rowCount(), int(mempool_blocks::MAX_MINED_BLOCKS), ASYNC_TIMEOUT_MS);
    QCOMPARE(MinedHeight(mined, 0), 100);
    QCOMPARE(MinedHeight(mined, 7), 93);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(stats_calls.load(), 8);

    tip = 101;
    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(MinedHeight(mined, 0), 101, ASYNC_TIMEOUT_MS);
    QCOMPARE(MinedHeight(mined, 7), 94);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(stats_calls.load(), 9);
}

void MempoolBlocksModelTests::showsMinedBlocksUpToPrunedOnes()
{
    MockNode node;
    node.get_num_blocks_fn = [] { return 100; };
    node.execute_rpc_fn = [](const std::string& method, const UniValue& params, const std::string&) {
        return ChainRpc(method, params, /*tip=*/100, /*pruned_below=*/97);
    };

    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.start();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.minedBlocks()->rowCount(), 4);
    QCOMPARE(MinedHeight(*model.minedBlocks(), 3), 97);
}

void MempoolBlocksModelTests::estimatesRetargetAndHalvingFromHeaders()
{
    constexpr int TIP{4'100};
    const int64_t now{ChainTime(TIP) + 300};
    SetMockTime(now);
    MockNode node;
    node.get_num_blocks_fn = [] { return TIP; };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) {
        return ChainRpc(method, params, TIP);
    };
    const difficulty_adjustment::ChainRules rules{.halving_interval = 150};
    MempoolBlocksModel model{node, rules, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(!model.difficultyAdjustment().isEmpty(), ASYNC_TIMEOUT_MS);
    SetMockTime(0);

    // The period started at 4032 and the one before at 2016, with the
    // difficulty going from 1.5 to 2.
    const difficulty_adjustment::EpochState epoch{
        .tip_height = TIP,
        .tip_time = ChainTime(TIP),
        .epoch_start_time = ChainTime(4'032),
        .previous_retarget = 2 / 1.5 * 100 - 100,
        .quarter_epoch_time = ChainTime(TIP - 503),
    };
    const difficulty_adjustment::DifficultyAdjustment expected{difficulty_adjustment::Estimate(epoch, now, rules)};
    const QVariantMap adjustment{model.difficultyAdjustment()};
    QCOMPARE(adjustment.value(QStringLiteral("now")).toLongLong(), now * 1000);
    QCOMPARE(adjustment.value(QStringLiteral("remainingBlocks")).toInt(), 1'948);
    QCOMPARE(adjustment.value(QStringLiteral("nextRetargetHeight")).toInt(), 6'048);
    QCOMPARE(adjustment.value(QStringLiteral("previousRetarget")).toDouble(), epoch.previous_retarget);
    QCOMPARE(adjustment.value(QStringLiteral("difficultyChange")).toDouble(), expected.difficulty_change);
    QCOMPARE(adjustment.value(QStringLiteral("estimatedRetargetDate")).toLongLong(), expected.estimated_retarget_date);
    QCOMPARE(adjustment.value(QStringLiteral("timeAvg")).toLongLong(), expected.time_avg);

    const QVariantMap halving{model.halving()};
    QCOMPARE(halving.value(QStringLiteral("remainingBlocks")).toInt(), 100);
    QCOMPARE(halving.value(QStringLiteral("nextSubsidy")).toLongLong(), qint64{50 * COIN} >> 28);
    QCOMPARE(halving.value(QStringLiteral("estimatedDate")).toLongLong(), (now + 100 * 600) * 1000);
}

void MempoolBlocksModelTests::noEstimatesWhileSyncing()
{
    MockNode node;
    std::atomic<int> header_calls{0};
    node.get_num_blocks_fn = [] { return 100; };
    node.is_initial_block_download_fn = [] { return true; };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) {
        if (method == "getblockheader") ++header_calls;
        return ChainRpc(method, params, /*tip=*/100);
    };
    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(model.loaded(), ASYNC_TIMEOUT_MS);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 3);
    QVERIFY(model.difficultyAdjustment().isEmpty());
    QVERIFY(model.halving().isEmpty());
    QCOMPARE(header_calls.load(), 0);
}

void MempoolBlocksModelTests::reportsWhetherTheChainRetargets()
{
    MockNode node;
    for (const bool retargets : {true, false}) {
        MempoolBlocksModel model{node, {.retargets = retargets}};
        QSignalSpy changed{&model, &MempoolBlocksModel::chainEstimatesChanged};
        model.setChainEstimates(1'700'000'000, difficulty_adjustment::DifficultyAdjustment{}, difficulty_adjustment::Halving{});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(model.difficultyAdjustment().value(QStringLiteral("retargets")).toBool(), retargets);
        QCOMPARE(model.difficultyAdjustment().value(QStringLiteral("now")).toLongLong(), qint64{1'700'000'000'000});

        model.setChainEstimates(1'700'000'000, std::nullopt, std::nullopt);
        QCOMPARE(changed.size(), 2);
        QVERIFY(model.difficultyAdjustment().isEmpty());
        model.setChainEstimates(1'700'000'000, std::nullopt, std::nullopt);
        QCOMPARE(changed.size(), 2);
    }
}

void MempoolBlocksModelTests::fetchesOffGuiThreadOnlyWhileActiveAfterStart()
{
    MockNode node;
    std::atomic<int> calls{0};
    std::atomic<bool> fetched_on_gui_thread{false};
    std::atomic<bool> unexpected_request{false};
    QThread* const gui_thread{QThread::currentThread()};
    node.get_num_blocks_fn = [] { return 100; };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) {
        ++calls;
        if (QThread::currentThread() == gui_thread) fetched_on_gui_thread = true;
        if (method == "getrawmempool" && (params.size() != 1 || !params[0].get_bool())) unexpected_request = true;
        return ChainRpc(method, params, /*tip=*/100);
    };

    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.refresh();
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 3);
    QCOMPARE(calls.load(), 0);

    model.start();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.loaded());
    QCOMPARE(RowTxCount(model, 0), 1);
    QCOMPARE(model.data(model.index(0), MempoolBlocksModel::HeightRole).toInt(), 101);
    QCOMPARE(model.recommendedFees().value(QStringLiteral("fastest")).toDouble(), 1.0);
    QCOMPARE(model.recommendedFees().value(QStringLiteral("minimum")).toDouble(), 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(calls.load() >= 3, ASYNC_TIMEOUT_MS);
    QVERIFY(!fetched_on_gui_thread.load());
    QVERIFY(!unexpected_request.load());

    model.setActive(false);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 3);
    const int inactive_calls{calls.load()};
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(calls.load(), inactive_calls);

    model.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(calls.load() > inactive_calls, ASYNC_TIMEOUT_MS);
}

void MempoolBlocksModelTests::rpcErrorsKeepPreviousBlocks()
{
    MockNode node;
    std::atomic<int> calls{0};
    node.get_num_blocks_fn = [] { return 100; };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) -> UniValue {
        if (method != "getrawmempool") return ChainRpc(method, params, /*tip=*/100);
        const int call{++calls};
        if (call == 1) return OneTxMempool();
        if (call % 2 == 0) throw std::runtime_error("unexpected result");
        UniValue error{UniValue::VOBJ};
        error.pushKV("code", -28);
        error.pushKV("message", "Loading");
        throw error;
    };

    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(calls.load() >= 4, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.rowCount(), 1);
}

void MempoolBlocksModelTests::neverFetchesAfterShutdown()
{
    MockNode node;
    std::atomic<int> calls{0};
    node.get_num_blocks_fn = [] { return 100; };
    node.execute_rpc_fn = [&](const std::string& method, const UniValue& params, const std::string&) {
        ++calls;
        return ChainRpc(method, params, /*tip=*/100);
    };

    MempoolBlocksModel model{node, {}, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(calls.load() >= 1, ASYNC_TIMEOUT_MS);

    QSignalSpy drained{&model, &MempoolBlocksModel::drained};
    model.beginShutdown();
    model.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, ASYNC_TIMEOUT_MS);

    const int stopped_calls{calls.load()};
    model.setActive(false);
    model.setActive(true);
    model.start();
    model.refresh();
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(calls.load(), stopped_calls);
    QCOMPARE(drained.size(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(MempoolBlocksModelTests)
#else
QTEST_MAIN(MempoolBlocksModelTests)
#endif
#include "test_mempoolblocksmodel.moc"
