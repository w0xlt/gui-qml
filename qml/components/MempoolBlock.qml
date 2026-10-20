// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import "../controls"
import "../controls/utils.js" as Utils

// A projected block drawn as a cube. The front face fills from the right in
// proportion to the block's size, colored by its fee range.
Item {
    id: root

    property real blockSize: 125
    // Position in the strip, 0 being the next block.
    property int blockIndex: 0
    property real medianFeeRate: 0
    property real minFeeRate: 0
    property real maxFeeRate: 0
    property var feeRange: []
    property real totalFees: 0
    property int transactionCount: 0
    property real vsize: 0
    // Shown instead of the block statistics, e.g. while loading.
    property string message: ""

    readonly property real depthX: blockSize * 0.16
    readonly property real depthY: blockSize * 0.192
    readonly property real textScale: blockSize / 125
    readonly property real fillRatio: Math.max(0, Math.min(1, vsize / 1000000))
    // The last block holds the rest of the mempool and can span many blocks.
    readonly property int stackedBlocks: Math.ceil(vsize / 1000000)
    readonly property var segmentColors: {
        if (!feeRange || feeRange.length === 0) return []
        // The next block's top rate is often an outlier, so leave it out.
        const rates = blockIndex === 0 && feeRange.length > 1 ? feeRange.slice(0, -1) : feeRange
        return rates.map(rate => Theme.color.mempoolFeeColors[Utils.mempoolFeeLevelIndex(rate)])
    }

    implicitWidth: blockSize + depthX
    implicitHeight: blockSize + depthY

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: "transparent"
            fillColor: Theme.color.mempoolBlockTop
            startX: 0; startY: 0
            PathLine { x: root.blockSize; y: 0 }
            PathLine { x: root.blockSize + root.depthX; y: root.depthY }
            PathLine { x: root.depthX; y: root.depthY }
            PathLine { x: 0; y: 0 }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: Theme.color.mempoolBlockSide
            startX: 0; startY: 0
            PathLine { x: root.depthX; y: root.depthY }
            PathLine { x: root.depthX; y: root.depthY + root.blockSize }
            PathLine { x: 0; y: root.blockSize }
            PathLine { x: 0; y: 0 }
        }
    }

    Rectangle {
        id: face
        objectName: "mempoolBlockFace"
        x: root.depthX
        y: root.depthY
        width: root.blockSize
        height: root.blockSize
        color: Theme.color.mempoolBlockEmpty

        Row {
            objectName: "mempoolBlockFill"
            anchors.right: parent.right
            height: parent.height

            Repeater {
                model: root.message.length === 0 ? root.segmentColors.length : 0

                Rectangle {
                    width: face.width * root.fillRatio / root.segmentColors.length
                    height: face.height
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: root.segmentColors[Math.max(0, index - 1)] }
                        GradientStop { position: 1; color: root.segmentColors[index] }
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            width: parent.width - 8
            spacing: 2 * root.textScale
            visible: root.message.length === 0

            CoreText {
                objectName: "mempoolBlockMedianFee"
                width: parent.width
                wrap: false
                color: Theme.color.white
                font.pixelSize: 12 * root.textScale
                text: "~" + (root.medianFeeRate >= 1 ? Math.round(root.medianFeeRate)
                                                     : Utils.formatFeeRate(root.medianFeeRate))
                      + " " + qsTr("sat/vB")
            }
            CoreText {
                objectName: "mempoolBlockFeeRange"
                width: parent.width
                wrap: false
                color: Theme.color.mempoolBlockFeeRange
                font.pixelSize: 11 * root.textScale
                text: Utils.formatFeeRate(root.minFeeRate) + " - " + Utils.formatFeeRate(root.maxFeeRate)
                      + " " + qsTr("sat/vB")
            }
            CoreText {
                objectName: "mempoolBlockTotalFees"
                width: parent.width
                wrap: false
                bold: true
                color: Theme.color.white
                font.pixelSize: 16 * root.textScale
                text: Utils.formatBlockFees(root.totalFees)
            }
            CoreText {
                objectName: "mempoolBlockTransactions"
                width: parent.width
                wrap: false
                color: Theme.color.white
                font.pixelSize: 10 * root.textScale
                text: root.transactionCount === 1
                    ? qsTr("1 transaction")
                    : qsTr("%1 transactions").arg(Number(root.transactionCount).toLocaleString(Qt.locale(), 'f', 0))
            }
            CoreText {
                objectName: "mempoolBlockEta"
                width: parent.width
                wrap: false
                bold: root.stackedBlocks > 1
                color: Theme.color.white
                font.pixelSize: 13 * root.textScale
                text: root.stackedBlocks > 1
                    ? qsTr("(%1 blocks)").arg(root.stackedBlocks)
                    : qsTr("In ~%1 minutes").arg((root.blockIndex + 1) * 10)
            }
        }

        CoreText {
            objectName: "mempoolBlockMessage"
            anchors.centerIn: parent
            width: parent.width - 16
            visible: root.message.length > 0
            color: Theme.color.white
            font.pixelSize: 13 * root.textScale
            text: root.message
        }
    }
}
