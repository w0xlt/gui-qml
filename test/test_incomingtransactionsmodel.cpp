// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <interfaces/handler.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <qml/models/incomingtransactionsmodel.h>
#include <univalue.h>
#include <util/time.h>

#include <atomic>
#include <memory>
#include <string>

#include <QSignalSpy>

using incoming_transactions::Rate;

namespace {
constexpr int TEST_SAMPLE_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};
constexpr int64_t START{1'700'000'000};

CTransactionRef Tx(int outputs)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(outputs);
    return MakeTransactionRef(std::move(tx));
}

//! A node whose saved mempool is loaded once loaded is set.
struct LoadingNode {
    MockNode node;
    std::atomic<bool> loaded{false};
    std::atomic<bool> waited{false};
    std::shared_ptr<interfaces::Chain::Notifications> notifications;

    LoadingNode()
    {
        node.execute_rpc_fn = [this](const std::string& method, const UniValue&, const std::string&) {
            if (method != "getmempoolinfo") throw std::runtime_error("unexpected method " + method);
            UniValue info{UniValue::VOBJ};
            info.pushKV("loaded", loaded.load());
            return info;
        };
    }

    std::unique_ptr<IncomingTransactionsModel> model()
    {
        return std::make_unique<IncomingTransactionsModel>(
            node,
            [this](std::shared_ptr<interfaces::Chain::Notifications> handler) {
                notifications = std::move(handler);
                return interfaces::MakeCleanupHandler([] {});
            },
            [this] { waited = true; },
            TEST_SAMPLE_INTERVAL_MS);
    }
};
} // namespace

class IncomingTransactionsModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { SetMockTime(0); }
    void averagesTheLast150Seconds();
    void firstSamplesAverageOverTheTimeCounted();
    void keepsTwoHoursOfSamples();
    void countsOnlyAfterTheSavedMempoolLoads();
    void shutsDownWhileTheMempoolLoads();
};

void IncomingTransactionsModelTests::averagesTheLast150Seconds()
{
    Rate rate{START};
    rate.add(START + 10, 1'000);
    rate.add(START + 100, 3'000);
    rate.add(START + 100, 1'500);
    rate.add(START + 200, 6'000);
    rate.sample(START + 200);
    QCOMPARE(rate.samples().back().time, START + 200);
    QCOMPARE(rate.samples().back().vbytes_per_second, (3'000 + 1'500 + 6'000) / 150.0);

    // Transactions are counted for 150 seconds.
    rate.sample(START + 249);
    QCOMPARE(rate.samples().back().vbytes_per_second, (3'000 + 1'500 + 6'000) / 150.0);
    rate.sample(START + 250);
    QCOMPARE(rate.samples().back().vbytes_per_second, 6'000 / 150.0);
    rate.sample(START + 400);
    QCOMPARE(rate.samples().back().vbytes_per_second, 0.0);
    QCOMPARE(rate.samples().size(), size_t{4});
}

void IncomingTransactionsModelTests::firstSamplesAverageOverTheTimeCounted()
{
    Rate rate{START};
    rate.add(START + 10, 6'000);
    rate.sample(START + 60);
    QCOMPARE(rate.samples().back().vbytes_per_second, 100.0);
    rate.sample(START);
    QCOMPARE(rate.samples().back().vbytes_per_second, 0.0);
}

void IncomingTransactionsModelTests::keepsTwoHoursOfSamples()
{
    Rate rate{START};
    for (int minute = 1; minute <= 130; ++minute) {
        rate.sample(START + minute * 60);
    }
    QCOMPARE(rate.samples().size(), incoming_transactions::MAX_SAMPLES);
    QCOMPARE(rate.samples().front().time, START + 11 * 60);
}

void IncomingTransactionsModelTests::countsOnlyAfterTheSavedMempoolLoads()
{
    SetMockTime(START);
    LoadingNode loading;
    auto model{loading.model()};
    QSignalSpy samples{model.get(), &IncomingTransactionsModel::samplesChanged};
    model->start();
    QVERIFY(loading.notifications);

    // A transaction from the saved mempool.
    loading.notifications->transactionAddedToMempool(Tx(50));
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 5);
    QCOMPARE(model->recordingSince(), qint64{0});
    QCOMPARE(samples.size(), 0);
    QVERIFY(!loading.waited);

    loading.loaded = true;
    QTRY_COMPARE_WITH_TIMEOUT(model->recordingSince(), qint64{START * 1000}, ASYNC_TIMEOUT_MS * 3);
    QVERIFY(loading.waited);

    const CTransactionRef tx{Tx(2)};
    loading.notifications->transactionAddedToMempool(tx);
    SetMockTime(START + 60);
    QTRY_VERIFY_WITH_TIMEOUT(!model->times().isEmpty() && model->times().back() == (START + 60) * 1000.0, ASYNC_TIMEOUT_MS);
    QCOMPARE(model->rates().back(), GetVirtualTransactionSize(*tx) / 60.0);
}

void IncomingTransactionsModelTests::shutsDownWhileTheMempoolLoads()
{
    LoadingNode loading;
    auto model{loading.model()};
    model->start();
    QSignalSpy drained{model.get(), &IncomingTransactionsModel::drained};
    model->beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model->recordingSince(), qint64{0});

    // Late notifications are dropped.
    loading.notifications->transactionAddedToMempool(Tx(1));
    loading.loaded = true;
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 5);
    QCOMPARE(model->recordingSince(), qint64{0});
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(IncomingTransactionsModelTests)
#else
QTEST_MAIN(IncomingTransactionsModelTests)
#endif
#include "test_incomingtransactionsmodel.moc"
