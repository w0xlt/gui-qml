// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/qt_test_registry.h>

#include <interfaces/handler.h>
#include <kernel/chain.h>
#include <kernel/mempool_removal_reason.h>
#include <kernel/types.h>
#include <primitives/block.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <qml/models/recentreplacementsmodel.h>
#include <script/script.h>

#include <map>
#include <memory>
#include <vector>

#include <QSignalSpy>

using replacements::Replacement;
using replacements::ReplacementList;

namespace {
constexpr int ASYNC_TIMEOUT_MS{1'000};
// Doesn't signal replaceability (BIP125).
constexpr uint32_t FINAL{CTxIn::SEQUENCE_FINAL};
constexpr uint32_t REPLACEABLE{CTxIn::MAX_SEQUENCE_NONFINAL - 1};

CTransactionRef Tx(const std::vector<COutPoint>& inputs, CAmount output_value, uint32_t sequence = FINAL)
{
    CMutableTransaction tx;
    for (const COutPoint& input : inputs) tx.vin.emplace_back(input, CScript{}, sequence);
    tx.vout.emplace_back(output_value, CScript{} << OP_TRUE);
    return MakeTransactionRef(std::move(tx));
}

Replacement Shown(const CTransactionRef& tx)
{
    Replacement replacement;
    replacement.txid = tx->GetHash();
    return replacement;
}

//! A chain where outputs of the funding transactions can be spent, and the
//! notifications the model registers for.
struct Chain {
    std::map<COutPoint, Coin> coins;
    std::shared_ptr<interfaces::Chain::Notifications> notifications;

    COutPoint fund(CAmount value)
    {
        const CTransactionRef funding{Tx({COutPoint{Txid::FromUint256(uint256{static_cast<uint8_t>(coins.size() + 1)}), 0}}, value)};
        const COutPoint outpoint{funding->GetHash(), 0};
        coins.emplace(outpoint, Coin{funding->vout[0], /*nHeightIn=*/100, /*fCoinBaseIn=*/false});
        return outpoint;
    }

    std::unique_ptr<RecentReplacementsModel> model()
    {
        return std::make_unique<RecentReplacementsModel>(
            [this](std::shared_ptr<interfaces::Chain::Notifications> handler) {
                notifications = std::move(handler);
                return interfaces::MakeCleanupHandler([] {});
            },
            [this](std::map<COutPoint, Coin>& lookup) {
                for (auto& [outpoint, coin] : lookup) {
                    const auto found{coins.find(outpoint)};
                    if (found == coins.end()) {
                        coin.Clear();
                    } else {
                        coin = found->second;
                    }
                }
            });
    }

    void mine(const CTransactionRef& tx, bool historical = false)
    {
        CBlock block;
        block.vtx.push_back(tx);
        const uint256 hash{block.GetHash()};
        interfaces::BlockInfo info{hash};
        info.data = &block;
        notifications->blockConnected(kernel::ChainstateRole{.validated = true, .historical = historical}, info);
    }

    void replace(const std::vector<CTransactionRef>& replaced, const CTransactionRef& replacement)
    {
        for (const CTransactionRef& tx : replaced) {
            notifications->transactionRemovedFromMempool(tx, MemPoolRemovalReason::REPLACED);
        }
        notifications->transactionAddedToMempool(replacement);
    }
};

double FeeRate(CAmount fee, const CTransactionRef& tx)
{
    return static_cast<double>(fee) / GetVirtualTransactionSize(*tx);
}

QVariant Row(const RecentReplacementsModel& model, int row, RecentReplacementsModel::Roles role)
{
    return model.data(model.index(row), role);
}
} // namespace

class RecentReplacementsModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listKeepsTheLatestReplacements();
    void showsReplacementsWithTheirFeeRates();
    void feeIsUnknownWithoutTheSpentCoins();
    void ignoresTransactionsThatReplaceNothing();
    void stopsOnShutdown();
};

void RecentReplacementsModelTests::listKeepsTheLatestReplacements()
{
    Chain chain;
    const COutPoint coin{chain.fund(100'000)};
    const CTransactionRef a{Tx({coin}, 99'000)};
    const CTransactionRef b{Tx({coin}, 98'000)};
    const CTransactionRef c{Tx({coin}, 97'000)};

    ReplacementList list;
    list.add({a->GetHash()}, Shown(b));
    // Replacing b again shows only the newer replacement.
    list.add({b->GetHash()}, Shown(c));
    QCOMPARE(list.replacements().size(), size_t{1});
    QCOMPARE(list.replacements().front().txid, c->GetHash());

    QVERIFY(list.markMined(c->GetHash()));
    QVERIFY(list.replacements().front().mined);
    QVERIFY(!list.markMined(a->GetHash()));

    for (CAmount value = 1; value <= 10; ++value) {
        const CTransactionRef old{Tx({chain.fund(value * 1'000)}, 0)};
        list.add({old->GetHash()}, Shown(Tx({old->vin[0].prevout}, 1)));
    }
    QCOMPARE(list.replacements().size(), replacements::MAX_REPLACEMENTS);
}

void RecentReplacementsModelTests::showsReplacementsWithTheirFeeRates()
{
    Chain chain;
    auto model{chain.model()};
    model->start();
    QVERIFY(chain.notifications);

    const COutPoint coin{chain.fund(100'000)};
    const CTransactionRef old_tx{Tx({coin}, 99'000)};
    // A child of the replaced transaction is evicted with it.
    const CTransactionRef child{Tx({COutPoint{old_tx->GetHash(), 0}}, 98'500)};
    const CTransactionRef new_tx{Tx({coin}, 97'000)};
    chain.replace({old_tx, child}, new_tx);

    QTRY_COMPARE_WITH_TIMEOUT(model->count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::TxidRole).toString(), QString::fromStdString(new_tx->GetHash().ToString()));
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::OldFeeRateRole).toDouble(), FeeRate(1'000, old_tx));
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::NewFeeRateRole).toDouble(), FeeRate(3'000, new_tx));
    QVERIFY(Row(*model, 0, RecentReplacementsModel::FullRbfRole).toBool());
    QVERIFY(!Row(*model, 0, RecentReplacementsModel::MinedRole).toBool());

    // Replaced again, by a transaction the previous one signaled for.
    const CTransactionRef signaling{Tx({coin}, 96'000, REPLACEABLE)};
    chain.replace({new_tx}, signaling);
    chain.notifications->transactionRemovedFromMempool(signaling, MemPoolRemovalReason::REPLACED);
    const CTransactionRef newest{Tx({coin}, 95'000)};
    chain.notifications->transactionAddedToMempool(newest);
    QTRY_COMPARE_WITH_TIMEOUT(Row(*model, 0, RecentReplacementsModel::TxidRole).toString(),
                              QString::fromStdString(newest->GetHash().ToString()), ASYNC_TIMEOUT_MS);
    QCOMPARE(model->count(), 1);
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::OldFeeRateRole).toDouble(), FeeRate(4'000, signaling));
    QVERIFY(!Row(*model, 0, RecentReplacementsModel::FullRbfRole).toBool());

    // Blocks of a background chainstate validating old blocks don't count.
    chain.mine(newest, /*historical=*/true);
    QTest::qWait(50);
    QVERIFY(!Row(*model, 0, RecentReplacementsModel::MinedRole).toBool());
    chain.mine(newest);
    QTRY_VERIFY_WITH_TIMEOUT(Row(*model, 0, RecentReplacementsModel::MinedRole).toBool(), ASYNC_TIMEOUT_MS);
}

void RecentReplacementsModelTests::feeIsUnknownWithoutTheSpentCoins()
{
    Chain chain;
    auto model{chain.model()};
    model->start();

    const COutPoint coin{chain.fund(100'000)};
    // Spends an output of a transaction that is gone too.
    const CTransactionRef old_tx{Tx({coin, COutPoint{Txid::FromUint256(uint256::ONE), 1}}, 99'000)};
    const CTransactionRef new_tx{Tx({coin}, 97'000)};
    chain.replace({old_tx}, new_tx);

    QTRY_COMPARE_WITH_TIMEOUT(model->count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::OldFeeRateRole).toDouble(), -1.0);
    QCOMPARE(Row(*model, 0, RecentReplacementsModel::NewFeeRateRole).toDouble(), FeeRate(3'000, new_tx));
}

void RecentReplacementsModelTests::ignoresTransactionsThatReplaceNothing()
{
    Chain chain;
    auto model{chain.model()};
    model->start();

    chain.notifications->transactionAddedToMempool(Tx({chain.fund(50'000)}, 49'000));
    chain.mine(Tx({chain.fund(50'000)}, 49'000));
    // Replaced, but the next transaction spends something else.
    chain.notifications->transactionRemovedFromMempool(Tx({chain.fund(50'000)}, 49'000), MemPoolRemovalReason::REPLACED);
    chain.notifications->transactionAddedToMempool(Tx({chain.fund(50'000)}, 49'000));
    QTest::qWait(50);
    QCOMPARE(model->count(), 0);
}

void RecentReplacementsModelTests::stopsOnShutdown()
{
    Chain chain;
    auto model{chain.model()};
    model->start();
    QSignalSpy drained{model.get(), &RecentReplacementsModel::drained};
    model->beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, ASYNC_TIMEOUT_MS);

    // Late notifications are dropped.
    const COutPoint coin{chain.fund(100'000)};
    chain.replace({Tx({coin}, 99'000)}, Tx({coin}, 97'000));
    QTest::qWait(50);
    QCOMPARE(model->count(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(RecentReplacementsModelTests)
#else
QTEST_MAIN(RecentReplacementsModelTests)
#endif
#include "test_recentreplacementsmodel.moc"
