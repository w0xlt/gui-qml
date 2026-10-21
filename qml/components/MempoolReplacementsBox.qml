// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../controls/utils.js" as Utils

// The latest transactions that replaced others in the mempool, with the fee
// rates before and after, like mempool.space's recent replacements.
Control {
    id: root

    // RecentReplacementsModel.
    property var model: null
    // Height the card is at least, e.g. to line up with a card beside it.
    property real minimumCardHeight: 0
    readonly property real cardImplicitHeight: implicitContentHeight + topPadding + bottomPadding

    readonly property real feeColumnWidth: 100
    readonly property real statusColumnWidth: 120

    function formatFeeRate(feeRate) {
        return feeRate < 0 ? "–" : Utils.formatFeeRate(feeRate) + " " + qsTr("sat/vB")
    }

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
            text: qsTr("Recent Replacements")
        }

        Row {
            width: parent.width
            height: 28

            Repeater {
                model: [
                    { label: qsTr("TXID"), width: root.availableWidth - 2 * root.feeColumnWidth - root.statusColumnWidth },
                    { label: qsTr("Previous fee"), width: root.feeColumnWidth },
                    { label: qsTr("New fee"), width: root.feeColumnWidth },
                    { label: qsTr("Status"), width: root.statusColumnWidth },
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
                id: replacement
                objectName: "mempoolReplacement"
                width: parent.width
                height: 34
                readonly property bool mined: model.mined
                readonly property bool fullRbf: model.fullRbf

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.color.neutral2
                }

                Row {
                    anchors.fill: parent

                    MempoolTxid {
                        objectName: "mempoolReplacementTxid"
                        width: root.availableWidth - 2 * root.feeColumnWidth - root.statusColumnWidth
                        height: parent.height
                        rightPadding: 8
                        txid: model.txid
                    }
                    CoreText {
                        objectName: "mempoolReplacementOldFee"
                        width: root.feeColumnWidth
                        height: parent.height
                        wrap: false
                        font.pixelSize: 13
                        text: root.formatFeeRate(model.oldFeeRate)
                    }
                    CoreText {
                        objectName: "mempoolReplacementNewFee"
                        width: root.feeColumnWidth
                        height: parent.height
                        wrap: false
                        font.pixelSize: 13
                        text: root.formatFeeRate(model.newFeeRate)
                    }
                    Item {
                        width: root.statusColumnWidth
                        height: parent.height

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Repeater {
                                // Mined once the replacement is in a block. Full RBF when
                                // a replaced transaction did not signal it could be
                                // replaced.
                                model: (replacement.mined ? [{ label: qsTr("Mined"), color: Theme.color.green }] : []).concat(
                                    replacement.fullRbf ? [{ label: qsTr("Full RBF"), color: Theme.color.blue }]
                                                        : [{ label: qsTr("RBF"), color: Theme.color.green }])

                                Rectangle {
                                    objectName: "mempoolReplacementBadge"
                                    readonly property string label: modelData.label
                                    width: badgeText.implicitWidth + 10
                                    height: badgeText.implicitHeight + 4
                                    radius: 4
                                    color: modelData.color

                                    CoreText {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        wrap: false
                                        bold: true
                                        font.pixelSize: 11
                                        color: Theme.color.white
                                        text: modelData.label
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        CoreText {
            objectName: "mempoolReplacementsEmpty"
            visible: !root.model || root.model.count === 0
            width: parent.width
            topPadding: 12
            bottomPadding: 12
            color: Theme.color.neutral7
            text: qsTr("No transactions have been replaced since the node started.")
        }
    }
}
