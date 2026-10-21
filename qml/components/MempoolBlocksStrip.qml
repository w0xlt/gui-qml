// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Shapes 1.15
import QtQuick.Window 2.15
import "../controls"

// Projected blocks on the left of a divider at the center of the strip, the
// most recent mined blocks on the right, both nearest the divider first and
// running off the edges. When a block is mined the next projected block
// leaves, a mined one arrives and the others slide over. Like on
// mempool.space, the projected blocks pulse, as the mempool keeps changing.
Item {
    id: root

    property alias model: blocksView.model
    property alias minedModel: minedView.model
    property bool loaded: false
    property real blockSize: 125
    readonly property real blockStep: blockSize * 1.24
    readonly property real blockGap: blockStep - blockSize * 1.16
    readonly property real blockHeight: blockSize * 1.192
    readonly property real heightLabelSize: Math.round(blockSize * 0.2)
    // Mined block ages are shown relative to this.
    property real now: Date.now()
    // ChainModel name of the network, which colors the mined blocks.
    property string network: "MAIN"
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    // The projected blocks' opacity goes from 70% to full and back every 2
    // seconds, in steps 30 times a second like mempool.space, which keeps
    // the window from redrawing on every frame.
    property int pulseStep: 0
    readonly property real projectedOpacity: 0.7 + 0.3 * (1 - Math.abs(pulseStep / 30 - 1))

    implicitHeight: heightLabelSize + blockHeight

    Timer {
        interval: 30000
        repeat: true
        triggeredOnStart: true
        running: root.visible && minedView.count > 0
        onTriggered: root.now = Date.now()
    }

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
        anchors.rightMargin: root.blockGap
        anchors.bottom: parent.bottom
        height: root.blockHeight
        orientation: ListView.Horizontal
        layoutDirection: Qt.RightToLeft
        spacing: root.blockGap
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

    ListView {
        id: minedView
        objectName: "minedBlocksView"
        anchors.left: divider.right
        anchors.leftMargin: root.blockGap
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        orientation: ListView.Horizontal
        spacing: root.blockGap
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        ScrollBar.horizontal: ScrollBar {
            policy: minedView.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        delegate: Item {
            width: minedBlock.width
            height: minedView.height

            CoreText {
                objectName: "minedBlockHeight"
                width: parent.width
                height: root.heightLabelSize
                wrap: false
                verticalAlignment: Text.AlignTop
                font.pixelSize: 16 * minedBlock.textScale
                text: model.height
            }

            MempoolBlock {
                id: minedBlock
                objectName: "minedBlock"
                anchors.bottom: parent.bottom
                blockSize: root.blockSize
                mined: true
                medianFeeRate: model.medianFeeRate
                minFeeRate: model.minFeeRate
                maxFeeRate: model.maxFeeRate
                feeRange: model.feeRange
                totalFees: model.totalFees
                transactionCount: model.transactionCount
                vsize: model.vsize
                time: model.time
                now: root.now
                network: root.network
            }
        }

        // A new block comes out from behind the divider.
        add: Transition {
            NumberAnimation { property: "x"; from: ViewTransition.destination.x - root.blockStep; duration: 1000; easing.type: Easing.OutQuad }
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
        anchors.bottom: parent.bottom
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
