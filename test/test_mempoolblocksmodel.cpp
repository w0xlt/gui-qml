// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <core_io.h>
#include <qml/models/mempoolblocksmodel.h>
#include <univalue.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

#include <QSignalSpy>
#include <QThread>

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
} // namespace

class MempoolBlocksModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void exposesBlockRoles();
    void appliesSnapshotsAsRowChanges();
    void minedBlocksLeaveFromTheFront();
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
        if (method != "getrawmempool" || params.size() != 1 || !params[0].get_bool()) unexpected_request = true;
        return OneTxMempool();
    };

    MempoolBlocksModel model{node, TEST_REFRESH_INTERVAL_MS};
    model.setActive(true);
    model.refresh();
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 3);
    QCOMPARE(calls.load(), 0);

    model.start();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.loaded());
    QCOMPARE(RowTxCount(model, 0), 1);
    QCOMPARE(model.data(model.index(0), MempoolBlocksModel::HeightRole).toInt(), 101);
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
    node.execute_rpc_fn = [&](const std::string&, const UniValue&, const std::string&) -> UniValue {
        const int call{++calls};
        if (call == 1) return OneTxMempool();
        if (call % 2 == 0) throw std::runtime_error("unexpected result");
        UniValue error{UniValue::VOBJ};
        error.pushKV("code", -28);
        error.pushKV("message", "Loading");
        throw error;
    };

    MempoolBlocksModel model{node, TEST_REFRESH_INTERVAL_MS};
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
    node.execute_rpc_fn = [&](const std::string&, const UniValue&, const std::string&) {
        ++calls;
        return OneTxMempool();
    };

    MempoolBlocksModel model{node, TEST_REFRESH_INTERVAL_MS};
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
