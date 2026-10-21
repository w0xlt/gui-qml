// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../controls/utils.js" as Utils

// The next difficulty retarget or halving, like mempool.space's difficulty
// adjustment box. The retarget period's blocks are cells in a grid, colored
// as mined, ahead of or behind schedule, and remaining.
Column {
    id: root

    // MempoolBlocksModel's difficultyAdjustment and halving.
    property var adjustment: ({})
    property var halving: ({})
    property bool loaded: false
    // Steps of the projected blocks' pulse, 60 every 2 seconds, which the
    // next block's cell blinks with.
    property int pulseStep: 0
    // "difficulty" or "halving" once the viewer picks one. Until then, the
    // halving shows when it comes before the retarget.
    property string selectedMode: ""
    // Height the card is at least, e.g. to line up with a card beside it.
    // Its content stays centered.
    property real minimumCardHeight: 0
    readonly property real cardImplicitHeight: card.contentItem.implicitHeight + 40

    readonly property bool available: adjustment.remainingBlocks !== undefined && halving.remainingBlocks !== undefined
    // Without retargets, as on regtest, there is only the halving to show.
    readonly property bool retargets: adjustment.retargets !== false
    readonly property string mode: {
        if (!retargets) return "halving"
        if (selectedMode !== "") return selectedMode
        return available && halving.remainingBlocks < adjustment.remainingBlocks ? "halving" : "difficulty"
    }
    readonly property real contentWidth: card.availableWidth
    // Too narrow for three columns, so the average block time is left out.
    readonly property bool compact: contentWidth < 400

    // The grid has a column of 9 cells for every 9 blocks of the period.
    readonly property int periodBlocks: 2016
    readonly property int gridRows: 9
    readonly property int gridColumns: periodBlocks / gridRows
    // Index in the period of the tip, and of the block expected by now.
    readonly property int currentIndex: available ? periodBlocks - adjustment.remainingBlocks : 0
    readonly property int expectedCount: available ? Math.floor(adjustment.expectedBlocks) : 0
    readonly property int expectedIndex: Math.min(expectedCount, periodBlocks) - 1
    readonly property var shapes: {
        if (!available) return []
        let shapes = []
        shapes = shapes.concat(blocksToShapes(0, Math.min(currentIndex, expectedIndex), "mined"))
        shapes = shapes.concat(blocksToShapes(currentIndex + 1, expectedIndex, "behind"))
        shapes = shapes.concat(blocksToShapes(expectedIndex + 1, currentIndex, "ahead"))
        if (currentIndex < periodBlocks - 1) {
            shapes = shapes.concat(blocksToShapes(currentIndex + 1, currentIndex + 1, "next"))
        }
        return shapes
    }

    // Cells from block start to end, both included, as up to three
    // rectangles in grid units, like mempool.space's blocksToShapes().
    function blocksToShapes(start, end, status) {
        const startX = Math.floor(start / gridRows)
        const startY = start % gridRows
        const endX = Math.floor(end / gridRows)
        const endY = end % gridRows
        if (startX > endX) return []
        if (startX === endX) return [{ x: startX, y: startY, w: 1, h: 1 + endY - startY, status: status }]
        const shapes = [
            { x: startX, y: startY, w: 1, h: gridRows - startY, status: status },
            { x: endX, y: 0, w: 1, h: endY + 1, status: status },
        ]
        if (startX < endX - 1) shapes.push({ x: startX + 1, y: 0, w: endX - startX - 1, h: gridRows, status: status })
        return shapes
    }

    // What the cell of block index is, as the hover text says it.
    function describeBlock(index) {
        const mined = currentIndex
        if (index === currentIndex + 1 && currentIndex < periodBlocks - 1) return qsTr("Next block")
        if (index <= Math.min(currentIndex, expectedIndex)) {
            const count = currentIndex > expectedIndex ? expectedCount : mined
            if (currentIndex > expectedIndex) return count === 1 ? qsTr("1 block expected") : qsTr("%1 blocks expected").arg(count)
            return count === 1 ? qsTr("1 block mined") : qsTr("%1 blocks mined").arg(count)
        }
        if (index <= expectedIndex) {
            const behind = expectedCount - mined
            return behind === 1 ? qsTr("1 block behind") : qsTr("%1 blocks behind").arg(behind)
        }
        if (index <= currentIndex) {
            const ahead = mined - expectedCount
            return ahead === 1 ? qsTr("1 block ahead") : qsTr("%1 blocks ahead").arg(ahead)
        }
        const remaining = adjustment.remainingBlocks
        return remaining === 1 ? qsTr("1 block remaining") : qsTr("%1 blocks remaining").arg(remaining)
    }

    function mixColors(from, to, amount) {
        return Qt.rgba(from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
                       from.b + (to.b - from.b) * amount, 1)
    }

    function changeColor(change) {
        return change > 0 ? Theme.color.green : (change < 0 ? Theme.color.red : Theme.color.neutral7)
    }

    function formatChange(change) {
        return Math.abs(change).toLocaleString(Qt.locale(), 'f', 2)
    }

    spacing: 8

    Item {
        width: parent.width
        height: title.height

        CoreText {
            id: title
            width: parent.width
            // Left of the mode switch when there is no room to center it.
            horizontalAlignment: width - 2 * modeSwitch.width >= implicitWidth ? Text.AlignHCenter : Text.AlignLeft
            bold: true
            color: Theme.color.neutral7
            text: root.mode === "halving" ? qsTr("Halving Countdown") : qsTr("Difficulty Adjustment")
        }

        Row {
            id: modeSwitch
            visible: root.retargets
            anchors.right: parent.right
            anchors.verticalCenter: title.verticalCenter
            spacing: 4

            Repeater {
                model: [
                    { mode: "difficulty", label: qsTr("difficulty") },
                    { mode: "halving", label: qsTr("halving") },
                ]

                Row {
                    spacing: 4

                    CoreText {
                        visible: index > 0
                        wrap: false
                        color: Theme.color.neutral5
                        font.pixelSize: 9
                        text: "|"
                    }
                    CoreText {
                        objectName: "mempoolDifficultyMode_" + modelData.mode
                        readonly property bool current: root.mode === modelData.mode
                        wrap: false
                        color: current ? Theme.color.neutral6 : Theme.color.orange
                        font.pixelSize: 12
                        text: modelData.label
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label

                        MouseArea {
                            anchors.fill: parent
                            enabled: !parent.current
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedMode = modelData.mode
                        }
                    }
                }
            }
        }
    }

    Control {
        id: card
        width: parent.width
        padding: 20
        topPadding: 20 + Math.max(0, root.minimumCardHeight - root.cardImplicitHeight) / 2
        bottomPadding: topPadding

        background: Rectangle {
            radius: 10
            color: Theme.color.neutral1
            SurfaceGradientBorder {
                anchors.fill: parent
                surfaceColor: parent.color
                cornerRadius: parent.radius
            }
        }

        contentItem: Item {
            implicitHeight: 22 + 12 + stats.height

            // Period grid.
            Rectangle {
                id: epochGrid
                objectName: "mempoolDifficultyGrid"
                visible: root.available && root.mode === "difficulty"
                width: parent.width
                height: 22
                color: Theme.color.mempoolProgressEmpty
                readonly property real cellWidth: width / root.gridColumns
                readonly property real cellHeight: height / root.gridRows

                Repeater {
                    model: root.shapes

                    Item {
                        objectName: "mempoolDifficultyShape_" + modelData.status
                        readonly property string status: modelData.status
                        x: Math.round(modelData.x * epochGrid.cellWidth)
                        y: Math.round(modelData.y * epochGrid.cellHeight)
                        width: Math.round((modelData.x + modelData.w) * epochGrid.cellWidth) - x
                        height: Math.round((modelData.y + modelData.h) * epochGrid.cellHeight) - y
                        clip: true

                        // Mined cells show their part of one gradient across
                        // the grid.
                        Rectangle {
                            visible: parent.status === "mined"
                            x: -parent.x
                            width: epochGrid.width
                            height: parent.height
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.color.mempoolProgressStart }
                                GradientStop { position: 1; color: Theme.color.mempoolProgressEnd }
                            }
                        }
                        Rectangle {
                            objectName: "mempoolDifficultyCell"
                            visible: parent.status !== "mined"
                            anchors.fill: parent
                            color: {
                                if (parent.status === "behind") return Theme.color.mempoolProgressBehind
                                if (parent.status === "ahead") return Theme.color.mempoolProgressAhead
                                // The next block blinks, in pink when it is late.
                                const late = root.expectedIndex > root.currentIndex
                                return root.mixColors(Theme.color.neutral9,
                                                      late ? Theme.color.mempoolProgressNextBehind : Theme.color.mempoolProgressNext,
                                                      1 - Math.abs(root.pulseStep / 30 - 1))
                            }
                        }
                    }
                }

                HoverHandler { id: gridHover }
                ToolTip {
                    objectName: "mempoolDifficultyTip"
                    visible: gridHover.hovered
                    x: gridHover.point.position.x - width / 2
                    y: -height - 6
                    readonly property int blockIndex: Math.floor(gridHover.point.position.x / epochGrid.cellWidth) * root.gridRows
                        + Math.min(root.gridRows - 1, Math.floor(gridHover.point.position.y / epochGrid.cellHeight))
                    text: root.describeBlock(blockIndex)
                }
            }

            // Halving period bar.
            Rectangle {
                objectName: "mempoolHalvingBar"
                visible: root.available && root.mode === "halving"
                width: parent.width
                height: 22
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.color.mempoolProgressStart }
                    GradientStop { position: 1; color: Theme.color.mempoolProgressEnd }
                }

                Rectangle {
                    objectName: "mempoolHalvingRemaining"
                    x: Math.round(parent.width * (root.halving.progressPercent || 0) / 100)
                    width: parent.width - x
                    height: parent.height
                    color: Theme.color.mempoolProgressEmpty
                }
                CoreText {
                    objectName: "mempoolHalvingProgress"
                    anchors.centerIn: parent
                    wrap: false
                    text: (root.halving.progressPercent || 0).toLocaleString(Qt.locale(), 'f', 2) + "%"
                }
            }

            Skeleton {
                visible: !root.available && !root.loaded
                width: parent.width
                height: 22
                baseColor: Theme.color.neutral2
                highlightColor: Theme.color.neutral3
                loading: visible
            }

            CoreText {
                objectName: "mempoolDifficultyUnavailable"
                visible: !root.available && root.loaded
                width: parent.width
                height: parent.height
                color: Theme.color.neutral7
                text: qsTr("Estimates appear once your node is synced.")
            }

            Row {
                id: stats
                y: 22 + 12
                width: parent.width
                height: 50
                visible: root.available

                // Difficulty: average block time, change, retarget date.
                // Halving: new subsidy, blocks remaining, halving date.
                Repeater {
                    model: 3

                    Column {
                        objectName: "mempoolDifficultyStat"
                        visible: !(index === 0 && root.compact && root.mode === "difficulty")
                        width: stats.width / (root.compact && root.mode === "difficulty" ? 2 : 3)
                        spacing: 2

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 2

                            CoreText {
                                anchors.baseline: value.baseline
                                visible: root.mode === "difficulty" && index === 1 && root.adjustment.difficultyChange !== 0
                                wrap: false
                                font.pixelSize: 14
                                color: root.changeColor(root.adjustment.difficultyChange)
                                text: root.adjustment.difficultyChange > 0 ? "▲" : "▼"
                            }
                            CoreText {
                                id: value
                                objectName: "mempoolDifficultyValue"
                                wrap: false
                                font.pixelSize: index === 1 || root.mode === "halving" && index === 0 ? 20 : 18
                                color: root.mode === "difficulty" && index === 1
                                    ? root.changeColor(root.adjustment.difficultyChange) : Theme.color.neutral9
                                text: {
                                    if (!root.available) return ""
                                    if (root.mode === "halving") {
                                        if (index === 0) return Utils.formatBlockFees(root.halving.nextSubsidy)
                                        if (index === 1) return Number(root.halving.remainingBlocks).toLocaleString(Qt.locale(), 'f', 0)
                                        return new Date(root.halving.estimatedDate).toLocaleDateString(Qt.locale(), "MMM d, yyyy")
                                    }
                                    if (index === 0) return "~" + Utils.formatDuration(root.adjustment.timeAvg / 1000, false, { fractionDigits: 1 })
                                    if (index === 1) return root.formatChange(root.adjustment.difficultyChange) + " %"
                                    return Utils.formatDuration((root.adjustment.estimatedRetargetDate - root.adjustment.now) / 1000, true, { precision: 1 })
                                }
                            }
                        }

                        CoreText {
                            objectName: "mempoolDifficultyLabel"
                            width: parent.width
                            wrap: false
                            elide: Text.ElideRight
                            color: Theme.color.neutral7
                            text: {
                                if (!root.available) return ""
                                if (root.mode === "halving") {
                                    if (index === 0) return qsTr("New subsidy")
                                    if (index === 1) return root.halving.remainingBlocks === 1 ? qsTr("Block remaining") : qsTr("Blocks remaining")
                                    if (root.halving.remainingBlocks === 1) {
                                        return Utils.formatDuration(root.adjustment.adjustedTimeAvg / 1000, true, { precision: 1 })
                                    }
                                    return Utils.formatDuration((root.halving.estimatedDate - root.halving.now) / 1000, true,
                                                                { numUnits: 2, units: ["year", "day", "hour", "minute"] })
                                }
                                if (index === 0) return qsTr("Average block time")
                                if (index === 1) {
                                    const previous = root.adjustment.previousRetarget
                                    const sign = previous > 0 ? "▲ " : (previous < 0 ? "▼ " : "")
                                    return qsTr("Previous: %1").arg("<font color=\"%1\">%2%3</font> %")
                                        .arg(root.changeColor(previous)).arg(sign).arg(root.formatChange(previous))
                                }
                                const date = new Date(root.adjustment.estimatedRetargetDate)
                                const day = date.toLocaleDateString(Qt.locale(), "MMMM d")
                                // Within about 4 weeks of blocks, the time of day too.
                                return root.adjustment.remainingBlocks > 1870
                                    ? day : day + ", " + date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                            }
                            textFormat: root.mode === "difficulty" && index === 1 ? Text.StyledText : Text.PlainText
                        }
                    }
                }
            }

            Row {
                y: 22 + 12
                width: parent.width
                visible: !root.available && !root.loaded

                Repeater {
                    model: 3
                    Item {
                        width: parent.width / 3
                        height: 50
                        Skeleton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 6
                            width: Math.min(80, parent.width - 10)
                            height: 18
                            baseColor: Theme.color.neutral2
                            highlightColor: Theme.color.neutral3
                            loading: visible
                        }
                        Skeleton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 30
                            width: Math.min(120, parent.width - 10)
                            height: 14
                            baseColor: Theme.color.neutral2
                            highlightColor: Theme.color.neutral3
                            loading: visible
                        }
                    }
                }
            }
        }
    }
}
