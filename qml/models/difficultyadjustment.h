// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DIFFICULTYADJUSTMENT_H
#define BITCOIN_QML_MODELS_DIFFICULTYADJUSTMENT_H

#include <consensus/amount.h>

#include <cstdint>
#include <optional>

namespace Consensus {
struct Params;
}

namespace difficulty_adjustment {

//! Blocks between difficulty retargets, on every network.
static constexpr int RETARGET_INTERVAL{2016};
//! Seconds the difficulty targets between blocks, on every network.
static constexpr int64_t TARGET_BLOCK_SPACING{600};
//! Early in a period, the estimate also uses the blocks since this many
//! blocks below the tip.
static constexpr int QUARTER_EPOCH_BLOCKS{503};

//! Consensus rules the estimates depend on.
struct ChainRules {
    int halving_interval{210'000};
    //! False on regtest, where the difficulty never changes.
    bool retargets{true};
    //! Test networks allow a minimum difficulty block after 20 minutes
    //! without a block, so blocks are rarely further apart than that.
    bool min_difficulty_blocks{false};
};

ChainRules RulesFor(const Consensus::Params& params);

//! What the estimates start from, read from block headers.
struct EpochState {
    int tip_height{0};
    //! Header times, in seconds since the epoch.
    int64_t tip_time{0};
    //! Time of the first block of the current retarget period.
    int64_t epoch_start_time{0};
    //! Change of the last retarget in percent, 0 in the first period.
    double previous_retarget{0.0};
    //! Time of the block 503 blocks below the tip, if there is one.
    std::optional<int64_t> quarter_epoch_time;
};

//! Same fields as mempool.space's DifficultyAdjustment, without the testnet
//! time offset its projected blocks use.
struct DifficultyAdjustment {
    double progress_percent{0.0};
    //! Estimated change of the next retarget, in percent.
    double difficulty_change{0.0};
    //! In milliseconds since the epoch.
    int64_t estimated_retarget_date{0};
    int remaining_blocks{0};
    //! Durations in milliseconds.
    int64_t remaining_time{0};
    double previous_retarget{0.0};
    //! In seconds since the epoch.
    int64_t previous_time{0};
    int next_retarget_height{0};
    int64_t time_avg{0};
    //! Expected time between blocks, with the hashrate of the last 504
    //! blocks early in a period.
    int64_t adjusted_time_avg{0};
    double expected_blocks{0.0};
};

struct Halving {
    int remaining_blocks{0};
    //! Subsidy of the blocks after the next halving.
    CAmount next_subsidy{0};
    //! How far the current halving period is, in percent.
    double progress_percent{0.0};
    //! In milliseconds since the epoch, at one block every 10 minutes.
    int64_t estimated_date{0};
};

//! Change in percent from one retarget period's difficulty to the next,
//! limited to the protocol's x4 and /4.
double RetargetChange(double old_difficulty, double new_difficulty);

//! Same estimate as mempool.space's calcDifficultyAdjustment(), as of
//! now_seconds.
DifficultyAdjustment Estimate(const EpochState& epoch, int64_t now_seconds, const ChainRules& rules);

Halving NextHalving(int tip_height, int64_t now_seconds, const ChainRules& rules);

} // namespace difficulty_adjustment

#endif // BITCOIN_QML_MODELS_DIFFICULTYADJUSTMENT_H
