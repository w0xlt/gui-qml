// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/stubnode.h>
#include <test/qt_test_registry.h>

#include <qml/models/buildingblockmodel.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <univalue.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <QSignalSpy>

using building_block::ChunkFeeRates;
using building_block::ParseBlockHeader;
using building_block::ParseBlockTemplate;
using building_block::TemplateTransaction;

namespace {
constexpr int TEST_REFRESH_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};

std::string Hash(char c) { return std::string(64, c); }

UniValue BlockTemplate(int height, const std::string& previous_hash, const std::vector<TemplateTransaction>& transactions)
{
    UniValue template_transactions{UniValue::VARR};
    for (const TemplateTransaction& transaction : transactions) {
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("fee", transaction.fee);
        entry.pushKV("weight", transaction.weight);
        template_transactions.push_back(std::move(entry));
    }
    UniValue result{UniValue::VOBJ};
    result.pushKV("height", height);
    result.pushKV("previousblockhash", previous_hash);
    result.pushKV("transactions", std::move(template_transactions));
    return result;
}

//! A node whose block template, tip and RPC errors tests set. RPCs are called
//! from the model's worker thread.
class TemplateNode : public StubNode
{
public:
    UniValue executeRpc(const std::string& method, const UniValue& params, const std::string&) override
    {
        std::lock_guard lock{m_mutex};
        ++m_calls[method];
        if (method == "getblocktemplate") {
            m_rules.clear();
            for (const UniValue& rule : params[0]["rules"].getValues()) m_rules.push_back(rule.get_str());
            if (m_error) throw *m_error;
            return m_template;
        }
        if (method == "getblockheader") {
            UniValue header{UniValue::VOBJ};
            header.pushKV("hash", params[0].get_str());
            header.pushKV("nTx", m_tip_transactions);
            header.pushKV("time", m_tip_time);
            return header;
        }
        throw std::runtime_error("unexpected method " + method);
    }

    void setTemplate(UniValue block_template, int tip_transactions, int64_t tip_time)
    {
        std::lock_guard lock{m_mutex};
        m_template = std::move(block_template);
        m_tip_transactions = tip_transactions;
        m_tip_time = tip_time;
    }
    void setError(std::optional<UniValue> error)
    {
        std::lock_guard lock{m_mutex};
        m_error = std::move(error);
    }
    int calls(const std::string& method)
    {
        std::lock_guard lock{m_mutex};
        return m_calls[method];
    }
    std::vector<std::string> rules()
    {
        std::lock_guard lock{m_mutex};
        return m_rules;
    }

private:
    std::mutex m_mutex;
    UniValue m_template;
    int m_tip_transactions{0};
    int64_t m_tip_time{0};
    std::optional<UniValue> m_error;
    std::map<std::string, int> m_calls;
    std::vector<std::string> m_rules;
};
} // namespace

class BuildingBlockModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void chunksTransactionsLikeCore();
    void parsesTheBlockTemplate();
    void malformedResultsThrow();
    void refreshesOnlyWhileAViewerIsActive();
    void fetchesEachTipHeaderOnce();
    void errorsLeaveNoBuildingBlock();
    void destroyedViewersStopCounting();
    void shutsDownWhileWaiting();
};

void BuildingBlockModelTests::chunksTransactionsLikeCore()
{
    // A child paying for its parent is mined with it, at their combined rate.
    QCOMPARE(ChunkFeeRates({{0, 400}, {2'000, 400}}), (std::vector<double>{10.0}));
    // A child paying less is mined after it.
    QCOMPARE(ChunkFeeRates({{2'000, 400}, {0, 400}}), (std::vector<double>{20.0, 0.0}));
    // Transactions join the chunk before them while that raises its rate.
    QCOMPARE(ChunkFeeRates({{5'000, 400}, {0, 400}, {3'000, 400}}), (std::vector<double>{50.0, 15.0}));
    // Virtual sizes round the weight up.
    QCOMPARE(ChunkFeeRates({{1'410, 561}}), (std::vector<double>{10.0}));
    QVERIFY(ChunkFeeRates({}).empty());
}

void BuildingBlockModelTests::parsesTheBlockTemplate()
{
    const auto block{ParseBlockTemplate(BlockTemplate(101, Hash('a'), {{1'000, 400}, {300, 400}}))};
    QCOMPARE(block.height, 101);
    QCOMPARE(block.previous_block_hash, Hash('a'));
    QCOMPARE(block.transaction_count, 2);
    QCOMPARE(block.max_fee_rate, std::optional<double>{10.0});
    QCOMPARE(block.min_fee_rate, std::optional<double>{3.0});

    const auto empty{ParseBlockTemplate(BlockTemplate(101, Hash('a'), {}))};
    QCOMPARE(empty.transaction_count, 0);
    QVERIFY(!empty.min_fee_rate);
    QVERIFY(!empty.max_fee_rate);

    UniValue header{UniValue::VOBJ};
    header.pushKV("hash", Hash('a'));
    header.pushKV("nTx", 3'412);
    header.pushKV("time", 1'700'000'000);
    const auto tip{ParseBlockHeader(header)};
    QCOMPARE(tip.hash, Hash('a'));
    QCOMPARE(tip.transaction_count, 3'412);
    QCOMPARE(tip.time, int64_t{1'700'000'000});
}

void BuildingBlockModelTests::malformedResultsThrow()
{
    UniValue block_template{BlockTemplate(101, Hash('a'), {})};
    block_template.pushKV("transactions", UniValue{UniValue::VOBJ});
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ParseBlockTemplate(block_template));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ParseBlockTemplate(UniValue{UniValue::VOBJ}));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ParseBlockHeader(UniValue{UniValue::VOBJ}));
}

void BuildingBlockModelTests::refreshesOnlyWhileAViewerIsActive()
{
    TemplateNode node;
    node.setTemplate(BlockTemplate(201, Hash('b'), {{2'000, 400}, {500, 400}}), 3'412, 1'700'000'000);
    BuildingBlockModel model{node, TEST_REFRESH_INTERVAL_MS};
    model.start();
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(node.calls("getblocktemplate"), 0);
    QVERIFY(!model.available());

    QObject viewer;
    QSignalSpy active{&model, &BuildingBlockModel::activeChanged};
    model.setViewerActive(&viewer, true);
    QVERIFY(model.active());
    QCOMPARE(active.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(node.rules(), (std::vector<std::string>{"segwit", "signet"}));
    QCOMPARE(model.height(), 201);
    QCOMPARE(model.transactionCount(), 2);
    QCOMPARE(model.maxFeeRate(), 20.0);
    QCOMPARE(model.minFeeRate(), 5.0);
    QCOMPARE(model.tipTransactionCount(), 3'412);
    QCOMPARE(model.tipTime(), qint64{1'700'000'000'000});

    // It keeps refreshing while the view is active, and stops after.
    QTRY_VERIFY_WITH_TIMEOUT(node.calls("getblocktemplate") >= 3, ASYNC_TIMEOUT_MS);
    model.setViewerActive(&viewer, false);
    QVERIFY(!model.active());
    QCOMPARE(active.size(), 2);
    // A refresh may still be running.
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    const int calls{node.calls("getblocktemplate")};
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(node.calls("getblocktemplate"), calls);
}

void BuildingBlockModelTests::fetchesEachTipHeaderOnce()
{
    TemplateNode node;
    node.setTemplate(BlockTemplate(201, Hash('b'), {{2'000, 400}}), 3'412, 1'700'000'000);
    // Only activation and refresh() refresh it within the test.
    BuildingBlockModel model{node, /*refresh_interval_ms=*/60'000};
    model.start();
    QObject viewer;
    model.setViewerActive(&viewer, true);
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);

    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(node.calls("getblocktemplate"), 2, ASYNC_TIMEOUT_MS);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS);
    QCOMPARE(node.calls("getblockheader"), 1);

    // A block was connected, and the template is empty.
    node.setTemplate(BlockTemplate(202, Hash('c'), {}), 2'000, 1'700'000'600);
    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(model.height(), 202, ASYNC_TIMEOUT_MS);
    QCOMPARE(node.calls("getblockheader"), 2);
    QCOMPARE(model.transactionCount(), 0);
    QCOMPARE(model.minFeeRate(), -1.0);
    QCOMPARE(model.maxFeeRate(), -1.0);
    QCOMPARE(model.tipTransactionCount(), 2'000);
    QCOMPARE(model.tipTime(), qint64{1'700'000'600'000});
}

void BuildingBlockModelTests::errorsLeaveNoBuildingBlock()
{
    TemplateNode node;
    node.setTemplate(BlockTemplate(201, Hash('b'), {}), 1, 1'700'000'000);
    node.setError(JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "in initial sync"));
    BuildingBlockModel model{node, /*refresh_interval_ms=*/60'000};
    model.start();
    QObject viewer;
    model.setViewerActive(&viewer, true);
    QTRY_COMPARE_WITH_TIMEOUT(node.calls("getblocktemplate"), 1, ASYNC_TIMEOUT_MS);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS);
    QVERIFY(!model.available());

    node.setError(std::nullopt);
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.tipTransactionCount(), 1);

    node.setError(JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "not connected"));
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.available(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.tipTransactionCount(), -1);
    QCOMPARE(model.tipTime(), qint64{0});
}

void BuildingBlockModelTests::destroyedViewersStopCounting()
{
    TemplateNode node;
    BuildingBlockModel model{node};
    auto viewer{std::make_unique<QObject>()};
    QObject other;
    model.setViewerActive(viewer.get(), true);
    model.setViewerActive(&other, true);
    model.setViewerActive(&other, false);
    QVERIFY(model.active());
    viewer.reset();
    QVERIFY(!model.active());
}

void BuildingBlockModelTests::shutsDownWhileWaiting()
{
    TemplateNode node;
    BuildingBlockModel model{node, TEST_REFRESH_INTERVAL_MS};
    model.start();
    QSignalSpy drained{&model, &BuildingBlockModel::drained};
    model.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, ASYNC_TIMEOUT_MS);

    QObject viewer;
    model.setViewerActive(&viewer, true);
    QTest::qWait(TEST_REFRESH_INTERVAL_MS * 5);
    QCOMPARE(node.calls("getblocktemplate"), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BuildingBlockModelTests)
#else
QTEST_MAIN(BuildingBlockModelTests)
#endif
#include "test_buildingblockmodel.moc"
