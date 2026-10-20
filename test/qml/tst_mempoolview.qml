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

    function init() {
        nodeModel.mempoolInformationAvailable = true
        nodeModel.initialSyncComplete = true
        mempoolBlocksModel.loaded = true
        mempoolBlocksModel.setBlocks([
            projectedBlock(12.4, 10, 45, 3300000, 1, 500000),
            projectedBlock(4, 3.5, 9.99, 2000000, 2500, 1000000),
            projectedBlock(1.2, 0.1, 3.4, 4000, 3000, 2500000),
        ])
    }

    function cleanup() {
        mempoolBlocksModel.setBlocks([])
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

    function test_projected_blocks_pulse() {
        const view = createView()
        const projected = findChild(view, "mempoolBlocksView")
        tryVerify(function() { return projected.opacity < 0.75 }, 3000)
        tryVerify(function() { return projected.opacity > 0.95 }, 3000)
        verify(projected.opacity >= 0.7 && projected.opacity <= 1)

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
