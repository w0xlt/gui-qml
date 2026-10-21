// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "mempoolView"

    signal back

    property bool showHeader: true
    property bool showBackButton: true
    property bool ownsMempoolBlocksActivity: false

    background: Rectangle { color: Theme.color.neutral0 }

    header: NavigationBar2 {
        visible: root.showHeader
        leftItem: NavButton {
            objectName: "mempoolBackButton"
            visible: root.showBackButton
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header { headerBold: true; headerSize: 18; header: qsTr("Mempool") }
    }

    function updateMempoolBlocksActivity() {
        if (root.visible && nodeModel.mempoolInformationAvailable) {
            mempoolBlocksModel.active = true
            root.ownsMempoolBlocksActivity = true
        } else {
            if (root.ownsMempoolBlocksActivity) mempoolBlocksModel.active = false
            root.ownsMempoolBlocksActivity = false
        }
    }

    Component.onCompleted: root.updateMempoolBlocksActivity()
    onVisibleChanged: root.updateMempoolBlocksActivity()
    Component.onDestruction: {
        if (root.ownsMempoolBlocksActivity) mempoolBlocksModel.active = false
    }

    Flickable {
        id: scroll
        objectName: "mempoolScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 60
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        clip: true
        ScrollBar.vertical: ScrollBar {
            policy: scroll.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: content
            objectName: "mempoolContent"
            y: 30
            width: scroll.width
            spacing: 20

            // Edge to edge, so the divider is centered on the page and the
            // blocks run off the side of the window.
            MempoolBlocksStrip {
                id: strip
                objectName: "mempoolBlocksStrip"
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.preferredHeight: implicitHeight
                visible: nodeModel.mempoolInformationAvailable
                model: mempoolBlocksModel
                minedModel: minedBlocksModel
                loaded: mempoolBlocksModel.loaded
                network: chainModel.networkName
            }

            CoreText {
                objectName: "mempoolDescription"
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                color: Theme.color.neutral7
                text: nodeModel.mempoolInformationAvailable
                    ? qsTr("Left of the line, transactions waiting in your node's mempool, grouped into the blocks they would most likely be mined in next. Right of the line, the latest blocks in your node's chain.")
                    : qsTr("Mempool information is not available while the node only relays blocks.")
            }

            CoreText {
                objectName: "mempoolSyncNote"
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                visible: nodeModel.mempoolInformationAvailable && !nodeModel.initialSyncComplete
                color: Theme.color.neutral7
                text: qsTr("Your node is still syncing, so its mempool may not reflect the network yet.")
            }

            // Side by side when there is room, like mempool.space's dashboard.
            GridLayout {
                id: cards
                objectName: "mempoolCards"
                Layout.fillWidth: true
                Layout.maximumWidth: 2 * 600 + columnSpacing
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                visible: nodeModel.mempoolInformationAvailable
                columns: root.width - 60 >= 700 ? 2 : 1
                columnSpacing: 20
                rowSpacing: 20
                // Side by side, the cards are as tall as the taller one.
                readonly property real cardHeight: columns > 1
                    ? Math.max(feesBox.cardImplicitHeight, difficultyBox.cardImplicitHeight) : 0

                MempoolFeesBox {
                    id: feesBox
                    objectName: "mempoolFeesBox"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.maximumWidth: 600
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    fees: mempoolBlocksModel.recommendedFees
                    loaded: mempoolBlocksModel.loaded
                    minimumCardHeight: cards.cardHeight
                }

                MempoolDifficultyBox {
                    id: difficultyBox
                    objectName: "mempoolDifficultyBox"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.maximumWidth: 600
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    adjustment: mempoolBlocksModel.difficultyAdjustment
                    halving: mempoolBlocksModel.halving
                    loaded: mempoolBlocksModel.loaded
                    pulseStep: strip.pulseStep
                    minimumCardHeight: cards.cardHeight
                }

                MempoolIncomingBox {
                    objectName: "mempoolIncomingBox"
                    Layout.columnSpan: cards.columns
                    Layout.fillWidth: true
                    Layout.maximumWidth: cards.columns > 1 ? 2 * 600 + cards.columnSpacing : 600
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    mempoolInfo: mempoolBlocksModel.mempoolInfo
                    times: incomingTransactionsModel.times
                    rates: incomingTransactionsModel.rates
                    recordingSince: incomingTransactionsModel.recordingSince
                }
            }
        }
    }
}
