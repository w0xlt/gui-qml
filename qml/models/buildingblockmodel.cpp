// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/buildingblockmodel.h>
#include <qml/asyncjoin.h>

#include <interfaces/node.h>
#include <policy/policy.h>
#include <sync.h>
#include <univalue.h>
#include <util/feefrac.h>
#include <util/threadnames.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <utility>

#include <QMetaObject>
#include <QThread>

namespace building_block {

std::vector<double> ChunkFeeRates(const std::vector<TemplateTransaction>& transactions)
{
    std::vector<FeeFrac> chunks;
    for (const TemplateTransaction& transaction : transactions) {
        chunks.emplace_back(transaction.fee, static_cast<int32_t>(GetVirtualTransactionSize(transaction.weight, 0, 0)));
        while (chunks.size() > 1 && ByRatio{chunks.back()} > ByRatio{chunks[chunks.size() - 2]}) {
            chunks[chunks.size() - 2] += chunks.back();
            chunks.pop_back();
        }
    }
    std::vector<double> rates;
    rates.reserve(chunks.size());
    for (const FeeFrac& chunk : chunks) {
        rates.push_back(chunk.size > 0 ? static_cast<double>(chunk.fee) / chunk.size : 0.0);
    }
    return rates;
}

BuildingBlock ParseBlockTemplate(const UniValue& block_template)
{
    BuildingBlock block;
    block.height = block_template["height"].getInt<int>();
    block.previous_block_hash = block_template["previousblockhash"].get_str();
    std::vector<TemplateTransaction> transactions;
    for (const UniValue& transaction : block_template["transactions"].get_array().getValues()) {
        transactions.push_back({
            .fee = transaction["fee"].getInt<CAmount>(),
            .weight = transaction["weight"].getInt<int64_t>(),
        });
    }
    block.transaction_count = static_cast<int>(transactions.size());
    if (const std::vector<double> rates{ChunkFeeRates(transactions)}; !rates.empty()) {
        block.max_fee_rate = rates.front();
        block.min_fee_rate = rates.back();
    }
    return block;
}

Tip ParseBlockHeader(const UniValue& header)
{
    return {
        .hash = header["hash"].get_str(),
        .transaction_count = header["nTx"].getInt<int>(),
        .time = header["time"].getInt<int64_t>(),
    };
}

} // namespace building_block

namespace {
UniValue BlockTemplateParams()
{
    UniValue rules{UniValue::VARR};
    rules.push_back("segwit");
    // Required on signet, ignored elsewhere.
    rules.push_back("signet");
    UniValue request{UniValue::VOBJ};
    request.pushKV("rules", std::move(rules));
    UniValue params{UniValue::VARR};
    params.push_back(std::move(request));
    return params;
}
} // namespace

struct BuildingBlockModel::State {
    Mutex mutex;
    std::condition_variable cv;
    bool stopping GUARDED_BY(mutex){false};
    bool active GUARDED_BY(mutex){false};
    bool refresh GUARDED_BY(mutex){true};
};

BuildingBlockModel::BuildingBlockModel(interfaces::Node& node, int refresh_interval_ms, QObject* parent)
    : QObject{parent}
    , m_node{node}
    , m_refresh_interval_ms{std::max(1, refresh_interval_ms)}
    , m_state{std::make_shared<State>()}
{
}

BuildingBlockModel::~BuildingBlockModel()
{
    // After beginShutdown(), JoinThreadAsync() owns the thread.
    if (!m_worker_thread || m_stopping) return;
    stopWorker();
    m_worker_thread->wait();
    delete m_worker_thread;
}

void BuildingBlockModel::setViewerActive(QObject* viewer, bool active)
{
    if (!viewer) return;
    const bool was_active{this->active()};
    if (active && !m_active_viewers.contains(viewer)) {
        m_active_viewers.insert(viewer);
        connect(viewer, &QObject::destroyed, this, [this, viewer] { setViewerActive(viewer, false); });
    } else if (!active && m_active_viewers.remove(viewer)) {
        disconnect(viewer, nullptr, this, nullptr);
    }
    if (this->active() == was_active) return;
    {
        LOCK(m_state->mutex);
        m_state->active = this->active();
        // The template may be stale by the time a view shows it again.
        if (m_state->active) m_state->refresh = true;
        m_state->cv.notify_all();
    }
    Q_EMIT activeChanged();
}

void BuildingBlockModel::refresh()
{
    LOCK(m_state->mutex);
    m_state->refresh = true;
    m_state->cv.notify_all();
}

void BuildingBlockModel::start()
{
    if (m_started || m_stopping) return;
    m_started = true;

    m_worker_thread = QThread::create([this, state = m_state, interval = std::chrono::milliseconds{m_refresh_interval_ms}] {
        util::ThreadRename("qml-buildblock");
        std::optional<building_block::Tip> tip;
        while (true) {
            {
                WAIT_LOCK(state->mutex, lock);
                state->cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) { return state->stopping || state->active; });
                if (state->stopping) return;
                if (!state->refresh) {
                    state->cv.wait_for(lock, interval, [&]() EXCLUSIVE_LOCKS_REQUIRED(state->mutex) {
                        return state->stopping || state->refresh || !state->active;
                    });
                    if (state->stopping) return;
                    if (!state->active) continue;
                }
                state->refresh = false;
            }

            std::optional<building_block::BuildingBlock> building_block;
            try {
                building_block = building_block::ParseBlockTemplate(
                    m_node.executeRpc("getblocktemplate", BlockTemplateParams(), /*uri=*/""));
                if (!tip || tip->hash != building_block->previous_block_hash) {
                    UniValue params{UniValue::VARR};
                    params.push_back(building_block->previous_block_hash);
                    tip = building_block::ParseBlockHeader(m_node.executeRpc("getblockheader", params, /*uri=*/""));
                }
            } catch (const UniValue&) {
                // RPC errors, e.g. while the node syncs, has no peers or shuts down.
                building_block.reset();
            } catch (const std::exception&) {
                building_block.reset();
            }
            QMetaObject::invokeMethod(this, [this, building_block, tip = building_block ? tip : std::nullopt] {
                if (!m_stopping) setResult(building_block, tip);
            }, Qt::QueuedConnection);
        }
    });
    m_worker_thread->setObjectName(QStringLiteral("building-block"));
    m_worker_thread->start();
}

void BuildingBlockModel::setResult(std::optional<building_block::BuildingBlock> building_block,
                                   std::optional<building_block::Tip> tip)
{
    if (m_building_block == building_block && m_tip == tip) return;
    m_building_block = std::move(building_block);
    m_tip = std::move(tip);
    Q_EMIT buildingBlockChanged();
}

void BuildingBlockModel::stopWorker()
{
    LOCK(m_state->mutex);
    m_state->stopping = true;
    m_state->cv.notify_all();
}

void BuildingBlockModel::beginShutdown()
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
