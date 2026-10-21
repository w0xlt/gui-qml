// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../controls/utils.js" as Utils

// The latest transactions that entered the mempool, with what they send and
// their fee rate, like mempool.space's recent transactions.
Control {
    id: root

    // RecentTransactionsModel.
    property var model: null
    // Height the card is at least, e.g. to line up with a card beside it.
    property real minimumCardHeight: 0
    readonly property real cardImplicitHeight: implicitContentHeight + topPadding + bottomPadding

    readonly property real amountColumnWidth: 130
    readonly property real feeColumnWidth: 100

    padding: 20
    implicitHeight: Math.max(cardImplicitHeight, minimumCardHeight)

    background: Rectangle {
        radius: 10
        color: Theme.color.neutral1
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    contentItem: Column {
        spacing: 8

        CoreText {
            bold: true
            wrap: false
            font.pixelSize: 15
            text: qsTr("Recent Transactions")
        }

        Row {
            width: parent.width
            height: 28

            Repeater {
                model: [
                    { label: qsTr("TXID"), width: root.availableWidth - root.amountColumnWidth - root.feeColumnWidth },
                    { label: qsTr("Amount"), width: root.amountColumnWidth },
                    { label: qsTr("Fee"), width: root.feeColumnWidth },
                ]

                CoreText {
                    width: modelData.width
                    height: parent.height
                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignHCenter
                    wrap: false
                    elide: Text.ElideRight
                    color: Theme.color.neutral7
                    font.pixelSize: 12
                    text: modelData.label
                }
            }
        }

        Repeater {
            model: root.model

            Item {
                objectName: "mempoolTransaction"
                width: parent.width
                height: 34

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.color.neutral2
                }

                Row {
                    anchors.fill: parent

                    MempoolTxid {
                        objectName: "mempoolTransactionTxid"
                        width: root.availableWidth - root.amountColumnWidth - root.feeColumnWidth
                        height: parent.height
                        rightPadding: 8
                        txid: model.txid
                    }
                    CoreText {
                        objectName: "mempoolTransactionAmount"
                        width: root.amountColumnWidth
                        height: parent.height
                        wrap: false
                        font.pixelSize: 13
                        text: Utils.formatAmount(model.value)
                    }
                    CoreText {
                        objectName: "mempoolTransactionFee"
                        width: root.feeColumnWidth
                        height: parent.height
                        wrap: false
                        font.pixelSize: 13
                        text: model.feeRate < 0 ? "–" : Utils.formatFeeRate(model.feeRate) + " " + qsTr("sat/vB")
                    }
                }
            }
        }

        CoreText {
            objectName: "mempoolTransactionsEmpty"
            visible: !root.model || root.model.count === 0
            width: parent.width
            topPadding: 12
            bottomPadding: 12
            color: Theme.color.neutral7
            text: qsTr("Waiting for new transactions to enter the mempool.")
        }
    }
}
