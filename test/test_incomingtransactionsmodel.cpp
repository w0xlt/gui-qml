// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <coins.h>
#include <interfaces/handler.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <qml/models/incomingtransactionsmodel.h>
#include <script/script.h>
#include <univalue.h>
#include <util/time.h>

#include <atomic>
#include <map>
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
    // Mempool transactions only have outputs in range.
    tx.vout.resize(outputs, CTxOut{1'000, CScript{} << OP_TRUE});
    return MakeTransactionRef(std::move(tx));
}

CTransactionRef Spend(const COutPoint& coin, CAmount value)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(coin);
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(std::move(tx));
}

//! A node whose saved mempool is loaded once loaded is set.
struct LoadingNode {
    MockNode node;
    std::atomic<bool> loaded{false};
    std::atomic<bool> waited{false};
    std::shared_ptr<interfaces::Chain::Notifications> notifications;
    //! Coins the node can find, set up before transactions spend them.
    std::map<COutPoint, Coin> coins;

    COutPoint fund(CAmount value)
    {
        const COutPoint outpoint{Txid::FromUint256(uint256{static_cast<uint8_t>(coins.size() + 1)}), 0};
        coins.emplace(outpoint, Coin{CTxOut{value, CScript{} << OP_TRUE}, /*nHeightIn=*/100, /*fCoinBaseIn=*/false});
        return outpoint;
    }

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
            [this](std::map<COutPoint, Coin>& lookup) {
                for (auto& [outpoint, coin] : lookup) {
                    const auto found{coins.find(outpoint)};
                    if (found == coins.end()) {
                        coin.Clear();
                    } else {
                        coin = found->second;
                    }
                }
            },
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
    void listsTheLatestTransactions();
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

void IncomingTransactionsModelTests::listsTheLatestTransactions()
{
    LoadingNode loading;
    // Set up the coins before the worker starts looking them up.
    std::vector<COutPoint> funded;
    for (int i = 0; i < 9; ++i) funded.push_back(loading.fund(100'000));
    auto model{loading.model()};
    RecentTransactionsModel& recent{*model->recentTransactions()};
    model->start();

    // From the saved mempool.
    loading.notifications->transactionAddedToMempool(Spend(funded[0], 99'000));
    loading.loaded = true;
    QTRY_VERIFY_WITH_TIMEOUT(model->recordingSince() > 0, ASYNC_TIMEOUT_MS * 3);

    const CTransactionRef first{Spend(funded[1], 97'000)};
    loading.notifications->transactionAddedToMempool(first);
    QTRY_COMPARE_WITH_TIMEOUT(recent.count(), 1, ASYNC_TIMEOUT_MS);
    const QModelIndex row{recent.index(0)};
    QCOMPARE(recent.data(row, RecentTransactionsModel::TxidRole).toString(), QString::fromStdString(first->GetHash().ToString()));
    QCOMPARE(recent.data(row, RecentTransactionsModel::ValueRole).toLongLong(), qint64{97'000});
    QCOMPARE(recent.data(row, RecentTransactionsModel::FeeRateRole).toDouble(), 3'000.0 / GetVirtualTransactionSize(*first));

    // The newest come first, and only six are kept.
    std::vector<CTransactionRef> sent{first};
    for (size_t i = 2; i < funded.size(); ++i) {
        sent.push_back(Spend(funded[i], 90'000));
        loading.notifications->transactionAddedToMempool(sent.back());
    }
    // A coin the node can't find leaves the fee unknown.
    const CTransactionRef unknown{Spend(COutPoint{Txid::FromUint256(uint256::ONE), 7}, 1'000)};
    loading.notifications->transactionAddedToMempool(unknown);
    QTRY_COMPARE_WITH_TIMEOUT(recent.data(recent.index(0), RecentTransactionsModel::TxidRole).toString(),
                              QString::fromStdString(unknown->GetHash().ToString()), ASYNC_TIMEOUT_MS * 3);
    QCOMPARE(recent.count(), 6);
    QCOMPARE(recent.data(recent.index(0), RecentTransactionsModel::FeeRateRole).toDouble(), -1.0);
    QCOMPARE(recent.data(recent.index(1), RecentTransactionsModel::TxidRole).toString(),
             QString::fromStdString(sent.back()->GetHash().ToString()));
    QCOMPARE(recent.data(recent.index(1), RecentTransactionsModel::FeeRateRole).toDouble(),
             10'000.0 / GetVirtualTransactionSize(*sent.back()));
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(IncomingTransactionsModelTests)
#else
QTEST_MAIN(IncomingTransactionsModelTests)
#endif
#include "test_incomingtransactionsmodel.moc"
