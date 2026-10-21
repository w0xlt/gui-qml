// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <common/args.h>
#include <qml/models/difficultyadjustment.h>
#include <util/chaintype.h>

#include <optional>
#include <vector>

using difficulty_adjustment::ChainRules;
using difficulty_adjustment::DifficultyAdjustment;
using difficulty_adjustment::EpochState;

class DifficultyAdjustmentTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void estimatesLikeMempoolSpace();
    void capsAverageBlockTimeOnTestNetworks();
    void regtestNeverRetargets();
    void retargetChangeIsLimited();
    void countsDownToTheHalving();
    void readsRulesFromChainParams();
};

void DifficultyAdjustmentTests::estimatesLikeMempoolSpace()
{
    // mempool.space's calcDifficultyAdjustment() test vectors, with the
    // results it expects.
    struct Vector {
        const char* name;
        EpochState epoch;
        int64_t now;
        DifficultyAdjustment expected;
    };
    const std::vector<Vector> vectors{
        {"normal adjustment",
         {.tip_height = 830'027, .tip_time = 0, .epoch_start_time = 1'706'888'526, .previous_retarget = 7.333505241141637, .quarter_epoch_time = 1'707'403'385},
         1'707'691'381,
         {.progress_percent = 71.97420634920636, .difficulty_change = 8.512745140778843, .estimated_retarget_date = 1'708'004'001'715,
          .remaining_blocks = 565, .remaining_time = 312'620'715, .previous_retarget = 7.333505241141637, .previous_time = 1'706'888'526,
          .next_retarget_height = 830'592, .time_avg = 553'311, .adjusted_time_avg = 553'311, .expected_blocks = 1338.0916666666667}},
        {"within quarter-epoch overlap",
         {.tip_height = 750'134, .tip_time = 0, .epoch_start_time = 1'660'820'820, .previous_retarget = 0.6280047707459726, .quarter_epoch_time = 1'660'619'814},
         1'660'917'833,
         {.progress_percent = 9.027777777777777, .difficulty_change = 1.0420538959004633, .estimated_retarget_date = 1'662'009'048'328,
          .remaining_blocks = 1'834, .remaining_time = 1'091'215'328, .previous_retarget = 0.6280047707459726, .previous_time = 1'660'820'820,
          .next_retarget_height = 751'968, .time_avg = 533'038, .adjusted_time_avg = 594'992, .expected_blocks = 161.68833333333333}},
        {"lock-in",
         {.tip_height = 788'255, .tip_time = 1'683'212'066, .epoch_start_time = 1'681'984'653, .previous_retarget = 1.7220298879531821, .quarter_epoch_time = 1'660'619'814},
         1'683'212'049,
         {.progress_percent = 99.95039682539682, .difficulty_change = -1.4512637555574193, .estimated_retarget_date = 1'683'212'658'129,
          .remaining_blocks = 1, .remaining_time = 609'129, .previous_retarget = 1.7220298879531821, .previous_time = 1'681'984'653,
          .next_retarget_height = 788'256, .time_avg = 609'129, .adjusted_time_avg = 609'129, .expected_blocks = 2045.66}},
    };
    for (const Vector& vector : vectors) {
        const DifficultyAdjustment result{difficulty_adjustment::Estimate(vector.epoch, vector.now, ChainRules{})};
        const DifficultyAdjustment& expected{vector.expected};
        QVERIFY2(qFuzzyCompare(result.progress_percent, expected.progress_percent), vector.name);
        QVERIFY2(qFuzzyCompare(result.difficulty_change, expected.difficulty_change), vector.name);
        QVERIFY2(result.estimated_retarget_date == expected.estimated_retarget_date, vector.name);
        QVERIFY2(result.remaining_blocks == expected.remaining_blocks, vector.name);
        QVERIFY2(result.remaining_time == expected.remaining_time, vector.name);
        QVERIFY2(result.previous_retarget == expected.previous_retarget, vector.name);
        QVERIFY2(result.previous_time == expected.previous_time, vector.name);
        QVERIFY2(result.next_retarget_height == expected.next_retarget_height, vector.name);
        QVERIFY2(result.time_avg == expected.time_avg, vector.name);
        QVERIFY2(result.adjusted_time_avg == expected.adjusted_time_avg, vector.name);
        QVERIFY2(qFuzzyCompare(result.expected_blocks, expected.expected_blocks), vector.name);
    }
}

void DifficultyAdjustmentTests::capsAverageBlockTimeOnTestNetworks()
{
    // 100 blocks in 50 hours, 30 minutes each.
    const EpochState epoch{.tip_height = 2'016 + 100, .tip_time = 0, .epoch_start_time = 0, .previous_retarget = 0, .quarter_epoch_time = std::nullopt};
    const int64_t now{100 * 1'800};
    QCOMPARE(difficulty_adjustment::Estimate(epoch, now, ChainRules{}).time_avg, int64_t{1'800'000});
    const DifficultyAdjustment testnet{difficulty_adjustment::Estimate(epoch, now, ChainRules{.min_difficulty_blocks = true})};
    QCOMPARE(testnet.time_avg, int64_t{1'200'000});
    // The estimate itself is unchanged.
    QCOMPARE(testnet.adjusted_time_avg, int64_t{1'800'000});
    QCOMPARE(testnet.difficulty_change, difficulty_adjustment::Estimate(epoch, now, ChainRules{}).difficulty_change);
}

void DifficultyAdjustmentTests::regtestNeverRetargets()
{
    const EpochState epoch{.tip_height = 2'016 + 100, .tip_time = 0, .epoch_start_time = 0, .previous_retarget = 12.5, .quarter_epoch_time = std::nullopt};
    const ChainRules regtest{.halving_interval = 150, .retargets = false, .min_difficulty_blocks = true};
    const DifficultyAdjustment adjustment{difficulty_adjustment::Estimate(epoch, /*now_seconds=*/10, regtest)};
    QCOMPARE(adjustment.difficulty_change, 0.0);
    QCOMPARE(adjustment.previous_retarget, 0.0);
    QCOMPARE(adjustment.remaining_blocks, 1'916);
}

void DifficultyAdjustmentTests::retargetChangeIsLimited()
{
    // Same cases as mempool.space's calcBitsDifference() tests.
    QCOMPARE(difficulty_adjustment::RetargetChange(100, 200), 100.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(100, 400), 300.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(100, 800), 300.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(200, 100), -50.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(400, 100), -75.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(800, 100), -75.0);
    QCOMPARE(difficulty_adjustment::RetargetChange(0, 100), 0.0);
}

void DifficultyAdjustmentTests::countsDownToTheHalving()
{
    const int64_t now{1'700'000'000};
    const difficulty_adjustment::Halving last_block{difficulty_adjustment::NextHalving(839'999, now, ChainRules{})};
    QCOMPARE(last_block.remaining_blocks, 1);
    QCOMPARE(last_block.next_subsidy, CAmount{312'500'000});
    QCOMPARE(last_block.estimated_date, (now + 600) * 1000);

    const difficulty_adjustment::Halving after{difficulty_adjustment::NextHalving(840'000, now, ChainRules{})};
    QCOMPARE(after.remaining_blocks, 210'000);
    QCOMPARE(after.next_subsidy, CAmount{156'250'000});
    QCOMPARE(after.progress_percent, 0.0);

    const difficulty_adjustment::Halving regtest{difficulty_adjustment::NextHalving(325, now, ChainRules{.halving_interval = 150})};
    QCOMPARE(regtest.remaining_blocks, 125);
    QCOMPARE(regtest.next_subsidy, CAmount{625'000'000});
    QCOMPARE(regtest.progress_percent, 25.0 / 150 * 100);

    QCOMPARE(difficulty_adjustment::NextHalving(64 * 150, now, ChainRules{.halving_interval = 150}).next_subsidy, CAmount{0});
}

void DifficultyAdjustmentTests::readsRulesFromChainParams()
{
    const ArgsManager args;
    const ChainRules main{difficulty_adjustment::RulesFor(CreateChainParams(args, ChainType::MAIN)->GetConsensus())};
    QCOMPARE(main.halving_interval, 210'000);
    QVERIFY(main.retargets);
    QVERIFY(!main.min_difficulty_blocks);

    const ChainRules regtest{difficulty_adjustment::RulesFor(CreateChainParams(args, ChainType::REGTEST)->GetConsensus())};
    QCOMPARE(regtest.halving_interval, 150);
    QVERIFY(!regtest.retargets);
    QVERIFY(regtest.min_difficulty_blocks);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(DifficultyAdjustmentTests)
#else
QTEST_MAIN(DifficultyAdjustmentTests)
#endif
#include "test_difficultyadjustment.moc"
