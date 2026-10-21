// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/difficultyadjustment.h>

#include <consensus/params.h>

#include <algorithm>
#include <cmath>

namespace difficulty_adjustment {
namespace {
// A retarget changes the difficulty by at most x4 or /4.
constexpr double MAX_CHANGE{300.0};
constexpr double MIN_CHANGE{-75.0};
constexpr int64_t MIN_DIFFICULTY_BLOCK_SECONDS{1200};
constexpr int MAX_HALVINGS{64};
constexpr CAmount INITIAL_SUBSIDY{50 * COIN};

double ClampChange(double change)
{
    return std::clamp(change, MIN_CHANGE, MAX_CHANGE);
}

//! Change in percent if blocks keep coming every block_seconds.
double ChangeFor(double block_seconds)
{
    if (block_seconds == 0) return MAX_CHANGE;
    return ClampChange((TARGET_BLOCK_SPACING / block_seconds - 1) * 100);
}
} // namespace

ChainRules RulesFor(const Consensus::Params& params)
{
    return {
        .halving_interval = params.nSubsidyHalvingInterval,
        .retargets = !params.fPowNoRetargeting,
        .min_difficulty_blocks = params.fPowAllowMinDifficultyBlocks,
    };
}

double RetargetChange(double old_difficulty, double new_difficulty)
{
    if (old_difficulty <= 0) return 0.0;
    return ClampChange((new_difficulty / old_difficulty - 1) * 100);
}

DifficultyAdjustment Estimate(const EpochState& epoch, int64_t now_seconds, const ChainRules& rules)
{
    DifficultyAdjustment adjustment;
    const int64_t elapsed{std::max<int64_t>(0, now_seconds - epoch.epoch_start_time)};
    const int blocks_in_epoch{epoch.tip_height % RETARGET_INTERVAL};
    adjustment.progress_percent = static_cast<double>(blocks_in_epoch) / RETARGET_INTERVAL * 100;
    adjustment.remaining_blocks = RETARGET_INTERVAL - blocks_in_epoch;
    adjustment.next_retarget_height = epoch.tip_height + adjustment.remaining_blocks;
    adjustment.expected_blocks = static_cast<double>(elapsed) / TARGET_BLOCK_SPACING;
    adjustment.previous_retarget = rules.retargets ? epoch.previous_retarget : 0.0;
    adjustment.previous_time = epoch.epoch_start_time;

    // Once the last block of the period is in, its time is what counts.
    const int64_t timespan{(blocks_in_epoch == RETARGET_INTERVAL - 1 ? epoch.tip_time : now_seconds) - epoch.epoch_start_time};
    double time_avg{blocks_in_epoch ? static_cast<double>(elapsed) / blocks_in_epoch : TARGET_BLOCK_SPACING};
    double adjusted_time_avg{time_avg};
    if (epoch.quarter_epoch_time && blocks_in_epoch < QUARTER_EPOCH_BLOCKS) {
        // Too few blocks in this period: add the last ones of the previous
        // period, scaled to the current difficulty.
        const double last_epoch_time{static_cast<double>(epoch.epoch_start_time - *epoch.quarter_epoch_time)};
        const double adjusted_span{elapsed + last_epoch_time * (1 + epoch.previous_retarget / 100)};
        adjusted_time_avg = adjusted_span / QUARTER_EPOCH_BLOCKS;
        adjustment.difficulty_change = ChangeFor(adjusted_span / (QUARTER_EPOCH_BLOCKS + 1));
    } else {
        adjustment.difficulty_change = ChangeFor(static_cast<double>(timespan) / (blocks_in_epoch + 1));
    }
    if (!rules.retargets) adjustment.difficulty_change = 0.0;
    if (rules.min_difficulty_blocks) {
        time_avg = std::min<double>(time_avg, MIN_DIFFICULTY_BLOCK_SECONDS);
    }

    adjustment.time_avg = static_cast<int64_t>(std::floor(time_avg * 1000));
    adjustment.adjusted_time_avg = static_cast<int64_t>(std::floor(adjusted_time_avg * 1000));
    adjustment.remaining_time = adjustment.remaining_blocks * adjustment.adjusted_time_avg;
    adjustment.estimated_retarget_date = adjustment.remaining_time + now_seconds * 1000;
    return adjustment;
}

Halving NextHalving(int tip_height, int64_t now_seconds, const ChainRules& rules)
{
    Halving halving;
    const int interval{std::max(1, rules.halving_interval)};
    halving.remaining_blocks = interval - tip_height % interval;
    halving.progress_percent = static_cast<double>(interval - halving.remaining_blocks) / interval * 100;
    const int halvings{tip_height / interval + 1};
    halving.next_subsidy = halvings >= MAX_HALVINGS ? 0 : INITIAL_SUBSIDY >> halvings;
    halving.estimated_date = (now_seconds + int64_t{halving.remaining_blocks} * TARGET_BLOCK_SPACING) * 1000;
    return halving;
}

} // namespace difficulty_adjustment
