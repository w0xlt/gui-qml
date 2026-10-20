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

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 30
        anchors.bottomMargin: 30
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
            loaded: mempoolBlocksModel.loaded
        }

        CoreText {
            objectName: "mempoolDescription"
            Layout.fillWidth: true
            Layout.leftMargin: 30
            Layout.rightMargin: 30
            color: Theme.color.neutral7
            text: nodeModel.mempoolInformationAvailable
                ? qsTr("Transactions waiting in your node's mempool, grouped into the blocks they would most likely be mined in next.")
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

        Item {
            Layout.fillHeight: true
        }
    }
}
