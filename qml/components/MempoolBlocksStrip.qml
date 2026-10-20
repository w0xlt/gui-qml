// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Shapes 1.15
import QtQuick.Window 2.15
import "../controls"

// Projected blocks lined up toward the chain. The divider sits at the center
// of the strip and the next block just left of it; later blocks run off the
// left edge. When a block is mined the nearest one leaves and the others slide
// over. Like on mempool.space, the blocks pulse, as the mempool keeps changing.
Item {
    id: root

    property alias model: blocksView.model
    property bool loaded: false
    property real blockSize: 125
    readonly property real blockStep: blockSize * 1.24
    readonly property real blockHeight: blockSize * 1.192
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    // The projected blocks' opacity goes from 70% to full and back every 2
    // seconds, in steps 30 times a second like mempool.space, which keeps
    // the window from redrawing on every frame.
    property int pulseStep: 0
    readonly property real projectedOpacity: 0.7 + 0.3 * (1 - Math.abs(pulseStep / 30 - 1))

    implicitHeight: blockHeight

    Timer {
        interval: 2000 / 60
        repeat: true
        running: root.visible && root.windowVisible
        onTriggered: root.pulseStep = (root.pulseStep + 1) % 60
    }

    ListView {
        id: blocksView
        objectName: "mempoolBlocksView"
        anchors.left: parent.left
        anchors.right: divider.left
        anchors.rightMargin: root.blockStep - root.blockSize * 1.16
        anchors.verticalCenter: parent.verticalCenter
        height: root.blockHeight
        orientation: ListView.Horizontal
        layoutDirection: Qt.RightToLeft
        spacing: root.blockStep - root.blockSize * 1.16
        clip: true
        opacity: root.projectedOpacity
        // Fades the blocks as a whole, so their faces don't show through
        // their fill and text.
        layer.enabled: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        // A right-to-left view keeps its scroll offset when resized, which
        // would push the next block out of sight.
        onWidthChanged: positionViewAtBeginning()
        ScrollBar.horizontal: ScrollBar {
            policy: blocksView.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        delegate: MempoolBlock {
            blockSize: root.blockSize
            // A mined block keeps reading as the next block while it fades out.
            blockIndex: Math.max(0, index)
            medianFeeRate: model.medianFeeRate
            minFeeRate: model.minFeeRate
            maxFeeRate: model.maxFeeRate
            feeRange: model.feeRange
            totalFees: model.totalFees
            transactionCount: model.transactionCount
            vsize: model.vsize
        }

        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 600 }
        }
        remove: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: 600 }
        }
        displaced: Transition {
            NumberAnimation { properties: "x"; duration: 2000; easing.type: Easing.InOutQuad }
        }
    }

    MempoolBlock {
        objectName: "mempoolBlocksPlaceholder"
        anchors.right: blocksView.right
        anchors.verticalCenter: blocksView.verticalCenter
        blockSize: root.blockSize
        visible: blocksView.count === 0
        opacity: root.projectedOpacity
        layer.enabled: true
        message: root.loaded ? qsTr("No transactions waiting") : qsTr("Loading…")
    }

    Shape {
        id: divider
        objectName: "mempoolBlocksDivider"
        x: Math.round((root.width - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: root.blockHeight

        ShapePath {
            strokeColor: Theme.color.neutral6
            strokeWidth: divider.width
            strokeStyle: ShapePath.DashLine
            dashPattern: [5, 8.5]
            startX: divider.width / 2; startY: 0
            PathLine { x: divider.width / 2; y: divider.height }
        }
    }
}
