// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockdetailsmodel.h>
#include <qml/asyncjoin.h>

#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <sync.h>
#include <univalue.h>
#include <util/threadnames.h>

#include <condition_variable>
#include <exception>
#include <map>
#include <utility>

#include <QMetaObject>
#include <QThread>

namespace block_details {

Block ParseBlock(const UniValue& block)
{
    Block result;
    result.height = block["height"].getInt<int>();
    if (const UniValue& previous{block["previousblockhash"]}; !previous.isNull()) {
        result.previous_block_hash = previous.get_str();
    }
    result.time = block["time"].getInt<int64_t>();
    result.transaction_count = block["nTx"].getInt<int>();
    if (const UniValue& size{block["size"]}; !size.isNull()) result.size = size.getInt<int64_t>();
    return result;
}

double ParseMedianFeeRate(const UniValue& stats)
{
    // At the 10th, 25th, 50th, 75th and 90th percentiles of the block's weight.
    return stats["feerate_percentiles"].get_array()[2].get_real();
}

} // namespace block_details

namespace {
using block_details::Block;

//! A period of the block clock has about 72 blocks.
constexpr size_t MAX_CACHED_BLOCKS{256};

UniValue Params(const std::string& hash)
{
    UniValue params{UniValue::VARR};
    params.push_back(hash);
    return params;
}

Block ReadBlock(interfaces::Node& node, const uint256& hash)
{
    Block block;
    bool pruned{false};
    try {
        UniValue params{Params(hash.GetHex())};
        params.push_back(1);
        block = block_details::ParseBlock(node.executeRpc("getblock", params, /*uri=*/""));
    } catch (const UniValue&) {
        // Pruned, but its header is kept.
        block = block_details::ParseBlock(node.executeRpc("getblockheader", Params(hash.GetHex()), /*uri=*/""));
        pruned = true;
    }
    if (!block.previous_block_hash.empty()) {
        block.previous_time = node.executeRpc("getblockheader", Params(block.previous_block_hash), /*uri=*/"")["time"].getInt<int64_t>();
    }
    if (!pruned && block.transaction_count > 1) {
        UniValue stats{UniValue::VARR};
        stats.push_back("feerate_percentiles");
        UniValue params{Params(hash.GetHex())};
        params.push_back(std::move(stats));
        try {
            block.median_fee_rate = block_details::ParseMedianFeeRate(node.executeRpc("getblockstats", params, /*uri=*/""));
        } catch (const UniValue&) {
            // Its undo data was pruned.
        }
    }
    return block;
}

} // namespace

struct BlockDetailsModel::State {
    struct Request {
        uint64_t id;
        int64_t time;
        int ordinal;
    };

    Mutex mutex;
    std::condition_variable cv;
    bool stopping GUARDED_BY(mutex){false};
    //! The latest selection, until the worker takes it.
    std::optional<Request> request GUARDED_BY(mutex);
};

BlockDetailsModel::BlockDetailsModel(interfaces::Node& node, BlockFinder find_block, QObject* parent)
    : QObject{parent}
    , m_node{node}
    , m_find_block{std::move(find_block)}
    , m_state{std::make_shared<State>()}
{
}

BlockDetailsModel::~BlockDetailsModel()
{
    // After beginShutdown(), JoinThreadAsync() owns the thread.
    if (!m_worker_thread || m_stopping) return;
    stopWorker();
    m_worker_thread->wait();
    delete m_worker_thread;
}

qint64 BlockDetailsModel::interval() const
{
    if (!m_block || !m_block->previous_time) return -1;
    const qint64 interval{m_block->time - *m_block->previous_time};
    return interval > 0 ? interval : -1;
}

void BlockDetailsModel::select(qint64 time, int ordinal)
{
    if (m_stopping) return;
    startWorker();
    LOCK(m_state->mutex);
    m_state->request = State::Request{++m_request, time, ordinal};
    m_state->cv.notify_all();
}

void BlockDetailsModel::clear()
{
    ++m_request;
    {
        LOCK(m_state->mutex);
        m_state->request.reset();
    }
    setBlock(std::nullopt);
}

void BlockDetailsModel::startWorker()
{
    if (m_worker_thread) return;
    m_worker_thread = QThread::create([this, state = m_state] {
        util::ThreadRename("qml-blockinfo");
        // Blocks don't change.
        std::map<uint256, Block> cache;
        while (true) {
            State::Request request;
            {
                WAIT_LOCK(state->mutex, lock);
                state->cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) {
                    return state->stopping || state->request.has_value();
                });
                if (state->stopping) return;
                request = *state->request;
                state->request.reset();
            }

            std::optional<Block> block;
            try {
                if (const std::optional<uint256> hash{m_find_block(request.time, request.ordinal)}) {
                    auto it{cache.find(*hash)};
                    if (it == cache.end()) {
                        if (cache.size() >= MAX_CACHED_BLOCKS) cache.clear();
                        it = cache.emplace(*hash, ReadBlock(m_node, *hash)).first;
                    }
                    block = it->second;
                }
            } catch (const UniValue&) {
                // RPC errors, e.g. while the node shuts down.
            } catch (const std::exception&) {
            }
            QMetaObject::invokeMethod(this, [this, id = request.id, block = std::move(block)] {
                if (!m_stopping && id == m_request) setBlock(block);
            }, Qt::QueuedConnection);
        }
    });
    m_worker_thread->setObjectName(QStringLiteral("block-details"));
    m_worker_thread->start();
}

void BlockDetailsModel::stopWorker()
{
    LOCK(m_state->mutex);
    m_state->stopping = true;
    m_state->cv.notify_all();
}

void BlockDetailsModel::setBlock(std::optional<Block> block)
{
    if (m_block == block) return;
    m_block = std::move(block);
    Q_EMIT blockChanged();
}

void BlockDetailsModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    if (!m_worker_thread) {
        Q_EMIT drained();
        return;
    }
    stopWorker();
    JoinThreadAsync(m_worker_thread, this, [this] {
        m_worker_thread = nullptr;
        Q_EMIT drained();
    });
}

std::optional<uint256> FindBlockTimestampedAt(interfaces::Chain& chain, int64_t time, int ordinal)
{
    // Blocks below the first one timestamped at or after the time are
    // timestamped before it.
    uint256 hash;
    if (!chain.findFirstBlockWithTimeAndHeight(time, /*min_height=*/0, interfaces::FoundBlock{}.hash(hash))) {
        return std::nullopt;
    }
    while (true) {
        int64_t block_time{0};
        int64_t median_time_past{0};
        bool has_next{false};
        uint256 next_hash;
        if (!chain.findBlock(hash, interfaces::FoundBlock{}.time(block_time).mtpTime(median_time_past).nextBlock(
                                       interfaces::FoundBlock{}.inActiveChain(has_next).hash(next_hash)))) {
            return std::nullopt;
        }
        if (block_time == time && ordinal-- == 0) return hash;
        // Blocks above it are timestamped after its median time past.
        if (median_time_past >= time || !has_next) return std::nullopt;
        hash = next_hash;
    }
}
