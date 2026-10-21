// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/controls/utils.js" as Utils
import "../../qml/pages/node"

TestCase {
    name: "MempoolView"
    when: windowShown
    visible: true
    width: 1300
    height: 600

    Component {
        id: viewComponent
        MempoolView {
            width: 1300
            height: 600
        }
    }

    function projectedBlock(median, min, max, fees, count, vsize) {
        return {
            medianFeeRate: median,
            minFeeRate: min,
            maxFeeRate: max,
            feeRange: [min, median, median, median, median, median, max],
            totalFees: fees,
            transactionCount: count,
            vsize: vsize,
        }
    }

    function minedBlock(height, minutesAgo, vsize) {
        return {
            height: height,
            time: Math.floor(Date.now() / 1000) - minutesAgo * 60,
            medianFeeRate: 5,
            minFeeRate: 2,
            maxFeeRate: 300,
            feeRange: [1, 2, 3, 5, 8, 13, 300],
            totalFees: 2500000,
            transactionCount: 3200,
            vsize: vsize,
        }
    }

    function init() {
        nodeModel.mempoolInformationAvailable = true
        nodeModel.initialSyncComplete = true
        mempoolBlocksModel.loaded = true
        mempoolBlocksModel.setBlocks([
            projectedBlock(12.4, 10, 45, 3300000, 1, 500000),
            projectedBlock(4, 3.5, 9.99, 2000000, 2500, 1000000),
            projectedBlock(1.2, 0.1, 3.4, 4000, 3000, 2500000),
        ])
        minedBlocksModel.setBlocks([
            minedBlock(100, 5, 750000),
            minedBlock(99, 17, 998000),
            minedBlock(98, 130, 400000),
        ])
    }

    function cleanup() {
        mempoolBlocksModel.setBlocks([])
        minedBlocksModel.setBlocks([])
        mempoolBlocksModel.loaded = false
        mempoolBlocksModel.active = false
        nodeModel.mempoolInformationAvailable = true
        nodeModel.initialSyncComplete = false
    }

    function createView() {
        const view = createTemporaryObject(viewComponent, this)
        verify(view !== null)
        const list = findChild(view, "mempoolBlocksView")
        verify(list !== null)
        tryCompare(list, "count", mempoolBlocksModel.count)
        return view
    }

    function blockAt(view, index) {
        const list = findChild(view, "mempoolBlocksView")
        let item = null
        tryVerify(function() {
            item = list.itemAtIndex(index)
            return item !== null
        })
        return item
    }

    function minedAt(view, index) {
        const list = findChild(view, "minedBlocksView")
        verify(list !== null)
        let item = null
        tryVerify(function() {
            item = list.itemAtIndex(index)
            return item !== null
        })
        return item
    }

    function blockText(block, name) {
        const text = findChild(block, name)
        verify(text !== null, name)
        return text.text
    }

    function test_shows_next_block_nearest_the_chain() {
        const view = createView()
        const next = blockAt(view, 0)
        const after = blockAt(view, 1)
        verify(next.x > after.x, "the next block is drawn closest to the divider")

        compare(blockText(next, "mempoolBlockMedianFee"), "~12 sat/vB")
        compare(blockText(next, "mempoolBlockFeeRange"),
                Utils.formatFeeRate(10) + " - " + Utils.formatFeeRate(45) + " sat/vB")
        compare(blockText(next, "mempoolBlockTotalFees"), Utils.formatBlockFees(3300000))
        compare(blockText(next, "mempoolBlockTransactions"), "1 transaction")
        compare(blockText(next, "mempoolBlockEta"), "In ~10 minutes")

        compare(blockText(after, "mempoolBlockTransactions"),
                Number(2500).toLocaleString(Qt.locale(), 'f', 0) + " transactions")
        compare(blockText(after, "mempoolBlockEta"), "In ~20 minutes")
    }

    function centerOf(item, view) {
        return item.mapToItem(view, item.width / 2, 0).x
    }

    function test_next_block_sits_left_of_the_centered_divider() {
        const view = createView()
        const divider = findChild(view, "mempoolBlocksDivider")
        verify(divider !== null)
        tryVerify(function() { return Math.abs(centerOf(divider, view) - view.width / 2) <= 1 })

        verifyNextBlockBesideDivider(view)
    }

    function verifyNextBlockBesideDivider(view) {
        const divider = findChild(view, "mempoolBlocksDivider")
        const next = blockAt(view, 0)
        const gap = centerOf(divider, view) - next.mapToItem(view, next.width, 0).x
        verify(gap > 0 && gap < 20, "the next block ends " + gap + " px before the divider")
    }

    function test_layout_follows_the_page_width() {
        // More blocks than fit, so the strip is scrolled to its start.
        const blocks = []
        for (let i = 0; i < 8; ++i) blocks.push(projectedBlock(20 - i, 19 - i, 21 - i, 100000, 3000, 1000000))
        mempoolBlocksModel.setBlocks(blocks)
        const view = createView()
        const divider = findChild(view, "mempoolBlocksDivider")
        const description = findChild(view, "mempoolDescription")
        for (const width of [801, 1300, 640]) {
            view.width = width
            tryVerify(function() { return Math.abs(centerOf(divider, view) - width / 2) <= 1 },
                      1000, "divider centered at width " + width)
            verify(description.mapToItem(view, description.width, 0).x <= width,
                   "description fits at width " + width)
            verifyNextBlockBesideDivider(view)
        }
    }

    function test_mined_blocks_sit_right_of_the_divider_newest_first() {
        const view = createView()
        const divider = findChild(view, "mempoolBlocksDivider")
        const newest = minedAt(view, 0)
        const older = minedAt(view, 1)
        tryVerify(function() { return older.x > newest.x })
        const gap = newest.mapToItem(view, 0, 0).x - centerOf(divider, view)
        verify(gap > 0 && gap < 20, "the newest mined block starts " + gap + " px after the divider")

        // Both strips stand on the same line.
        const next = blockAt(view, 0)
        const minedCube = findChild(newest, "mempoolBlockFace")
        const nextCube = findChild(next, "mempoolBlockFace")
        compare(minedCube.mapToItem(view, 0, minedCube.height).y, nextCube.mapToItem(view, 0, nextCube.height).y)

        compare(blockText(newest, "minedBlockHeight"), "100")
        compare(blockText(older, "minedBlockHeight"), "99")
        compare(blockText(newest, "mempoolBlockMedianFee"), "~5 sat/vB")
        compare(blockText(newest, "mempoolBlockFeeRange"),
                Utils.formatFeeRate(2) + " - " + Utils.formatFeeRate(300) + " sat/vB")
        compare(blockText(newest, "mempoolBlockTotalFees"), Utils.formatBlockFees(2500000))
        compare(blockText(newest, "mempoolBlockTransactions"),
                Number(3200).toLocaleString(Qt.locale(), 'f', 0) + " transactions")
        compare(blockText(newest, "mempoolBlockTime"), "5 minutes ago")
        compare(blockText(minedAt(view, 2), "mempoolBlockTime"), "2 hours ago")
        verify(!findChild(newest, "mempoolBlockEta").visible)
        verify(!findChild(next, "mempoolBlockTime").visible)
    }

    function test_mined_block_fills_from_the_bottom_by_size() {
        const view = createView()
        const newest = minedAt(view, 0)
        const face = findChild(newest, "mempoolBlockFace")
        const fill = findChild(newest, "mempoolBlockMinedFill")
        verify(fill.visible)
        verify(!findChild(newest, "mempoolBlockFill").visible)
        compare(findChild(newest, "minedBlock").segmentColors.length, 0)
        compare(fill.height, face.height * 0.75)
        compare(fill.y + fill.height, face.height)

        verify(!findChild(blockAt(view, 0), "mempoolBlockMinedFill").visible)
    }

    function test_mined_blocks_are_colored_apart_from_projected_ones() {
        const view = createView()
        const mined = findChild(minedAt(view, 0), "mempoolBlockFace")
        const projected = findChild(blockAt(view, 0), "mempoolBlockFace")
        compare(mined.color, Theme.color.mempoolMinedBlockEmpty)
        compare(projected.color, Theme.color.mempoolBlockEmpty)
        verify(!Qt.colorEqual(mined.color, projected.color))
    }

    function test_mined_block_fill_follows_the_network() {
        const view = createView()
        const strip = findChild(view, "mempoolBlocksStrip")
        const fill = findChild(minedAt(view, 0), "mempoolBlockMinedFill")
        compare(strip.network, "REGTEST")
        const networks = {
            "REGTEST": ["#7d7d7d", "#4a4a4a"],
            "SIGNET": ["#6f1d5d", "#471850"],
            "TESTNET4": ["#1d486f", "#183550"],
            "MAIN": ["#9339f4", "#007cfa"],
            "UNKNOWN": ["#9339f4", "#007cfa"],
        }
        for (const network in networks) {
            strip.network = network
            verify(Qt.colorEqual(fill.gradient.stops[0].color, networks[network][0]), network)
            verify(Qt.colorEqual(fill.gradient.stops[1].color, networks[network][1]), network)
        }
    }

    function test_mined_median_is_a_whole_number() {
        const block = minedBlock(100, 5, 1000)
        block.medianFeeRate = 0.4
        block.minFeeRate = 0
        block.maxFeeRate = 0.5
        minedBlocksModel.setBlocks([block])
        const view = createView()
        const newest = minedAt(view, 0)
        compare(blockText(newest, "mempoolBlockMedianFee"), "~0 sat/vB")
        compare(blockText(newest, "mempoolBlockFeeRange"),
                Utils.formatFeeRate(0) + " - " + Utils.formatFeeRate(0.5) + " sat/vB")
    }

    function test_new_mined_block_arrives_at_the_divider() {
        const view = createView()
        const list = findChild(view, "minedBlocksView")
        const previous = minedAt(view, 0)
        mempoolBlocksModel.removeFront(1)
        minedBlocksModel.insertFront(minedBlock(101, 0, 900000))
        tryCompare(list, "count", 4)
        tryVerify(function() {
            const newest = list.itemAtIndex(0)
            return newest !== null && newest !== previous && newest.x === 0 && newest.opacity === 1
        }, 3000)
        compare(blockText(list.itemAtIndex(0), "minedBlockHeight"), "101")
        compare(blockText(list.itemAtIndex(0), "mempoolBlockTime"), "just now")
        tryVerify(function() { return previous.x > list.itemAtIndex(0).x + previous.width }, 3000)
    }

    function test_projected_blocks_pulse_and_mined_ones_do_not() {
        const view = createView()
        const projected = findChild(view, "mempoolBlocksView")
        const mined = findChild(view, "minedBlocksView")
        tryVerify(function() { return projected.opacity < 0.75 }, 3000)
        tryVerify(function() { return projected.opacity > 0.95 }, 3000)
        verify(projected.opacity >= 0.7 && projected.opacity <= 1)
        compare(mined.opacity, 1)

        view.visible = false
        const paused = projected.opacity
        wait(300)
        compare(projected.opacity, paused)
    }

    function test_overfull_last_block_shows_block_count() {
        const view = createView()
        const last = blockAt(view, 2)
        compare(blockText(last, "mempoolBlockMedianFee"), "~1 sat/vB")
        compare(blockText(last, "mempoolBlockEta"), "(3 blocks)")
        verify(findChild(last, "mempoolBlockEta").bold)
    }

    function test_block_fills_from_the_right_by_size() {
        const view = createView()
        const next = blockAt(view, 0)
        const face = findChild(next, "mempoolBlockFace")
        const fill = findChild(next, "mempoolBlockFill")
        verify(face !== null)
        verify(fill !== null)
        tryVerify(function() { return Math.abs(fill.width - face.width / 2) < 0.5 })
        tryVerify(function() { return Math.abs(fill.x - face.width / 2) < 0.5 })
        // The next block leaves out its top fee rate.
        compare(next.segmentColors.length, 6)

        const full = blockAt(view, 1)
        tryVerify(function() {
            return Math.abs(findChild(full, "mempoolBlockFill").width - findChild(full, "mempoolBlockFace").width) < 0.5
        })
        compare(full.segmentColors.length, 7)
        compare(full.segmentColors[0], Theme.color.mempoolFeeColors[Utils.mempoolFeeLevelIndex(3.5)])
    }

    function test_mined_block_leaves_from_the_front() {
        const view = createView()
        const list = findChild(view, "mempoolBlocksView")
        const mined = blockAt(view, 0)
        mempoolBlocksModel.removeFront(1)
        tryCompare(list, "count", 2)
        compare(blockText(mined, "mempoolBlockEta"), "In ~10 minutes")
        // The mined block stays at index 0 until its exit transition ends.
        tryVerify(function() {
            const next = list.itemAtIndex(0)
            return next !== null && findChild(next, "mempoolBlockMedianFee").text === "~4 sat/vB"
        }, 3000)
        compare(blockText(list.itemAtIndex(0), "mempoolBlockEta"), "In ~10 minutes")
    }

    function test_refreshes_only_while_visible() {
        compare(mempoolBlocksModel.active, false)
        const view = createView()
        tryCompare(mempoolBlocksModel, "active", true)
        view.visible = false
        tryCompare(mempoolBlocksModel, "active", false)
        view.visible = true
        tryCompare(mempoolBlocksModel, "active", true)
        view.destroy()
        tryCompare(mempoolBlocksModel, "active", false)
    }

    function test_empty_mempool_shows_placeholder() {
        mempoolBlocksModel.setBlocks([])
        mempoolBlocksModel.loaded = false
        const view = createView()
        const placeholder = findChild(view, "mempoolBlocksPlaceholder")
        verify(placeholder !== null)
        verify(placeholder.visible)
        compare(placeholder.message, "Loading…")
        mempoolBlocksModel.loaded = true
        compare(placeholder.message, "No transactions waiting")

        mempoolBlocksModel.setBlocks([projectedBlock(2, 1, 3, 1000, 1, 200)])
        tryCompare(placeholder, "visible", false)
    }

    function test_blocks_only_node_hides_blocks() {
        nodeModel.mempoolInformationAvailable = false
        const view = createView()
        compare(findChild(view, "mempoolBlocksStrip").visible, false)
        compare(mempoolBlocksModel.active, false)
        compare(findChild(view, "mempoolSyncNote").visible, false)
        verify(findChild(view, "mempoolDescription").text.indexOf("not available") >= 0)
    }

    function test_sync_note_while_syncing() {
        nodeModel.initialSyncComplete = false
        const view = createView()
        verify(findChild(view, "mempoolSyncNote").visible)
        nodeModel.initialSyncComplete = true
        compare(findChild(view, "mempoolSyncNote").visible, false)
    }
}
