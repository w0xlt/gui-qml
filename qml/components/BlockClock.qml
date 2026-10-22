// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

import org.bitcoincore.qt 1.0

import "../controls"
import "../controls/utils.js" as Utils

Item {
    id: root
    objectName: "blockClock"
    property real parentWidth: 600
    property real parentHeight: 600
    property bool showNetworkIndicator: true
    // Backing models remain current while false; only presentation work stops.
    property bool renderingActive: true
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property var blockClockModelRef: typeof blockClockModel !== "undefined" ? blockClockModel : null
    property var networkStatusModelRef: typeof networkStatusModel !== "undefined" ? networkStatusModel : null
    property var buildingBlockModelRef: typeof buildingBlockModel !== "undefined" ? buildingBlockModel : null
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    readonly property bool presentationActive: root.renderingActive && root.visible && root.windowVisible

    width: dial.width
    height: dial.height + (networkIndicator.visible ? networkIndicator.height + networkIndicator.anchors.topMargin : 0)

    property alias header: mainText.text
    property alias headerSize: mainText.font.pixelSize
    property alias subText: subText.text
    readonly property bool connected: root.nodeModelRef !== null && root.nodeModelRef.numPeers > 0
    // Completion is latched after Core leaves IBD and the active chain catches
    // the best known header, and resets if the chain falls substantially behind.
    // verificationProgress only renders ongoing sync.
    readonly property bool synced: root.nodeModelRef !== null && root.nodeModelRef.initialSyncComplete
    readonly property bool headerSyncActive: root.nodeModelRef !== null && root.nodeModelRef.headerSyncActive
    readonly property real syncProgressFraction: root.headerSyncActive
        ? root.nodeModelRef.headerSyncProgress
        : (root.nodeModelRef !== null ? root.nodeModelRef.verificationProgress : 0)
    readonly property string syncProgress: formatProgressPercentage(root.syncProgressFraction * 100, root.synced)
    property bool paused: root.nodeModelRef !== null && root.nodeModelRef.pause
    property var syncState: Utils.formatRemainingSyncTime(root.nodeModelRef !== null ? root.nodeModelRef.remainingSyncTime : 0)
    property string syncTime: syncState.text
    property bool estimating: syncState.estimating
    property bool faulted: root.nodeModelRef !== null && root.nodeModelRef.faulted
    property bool offline: root.networkStatusModelRef !== null && root.networkStatusModelRef.networkOffline
    // Shared by the dial and the newest block painted over it.
    readonly property real dialPenWidth: dial.width / 50
    readonly property real currentTimeFraction: root.blockClockModelRef !== null ? root.blockClockModelRef.currentTimeFraction : 0
    readonly property var blockTimeFractions: root.blockClockModelRef !== null ? root.blockClockModelRef.blockTimeFractions : []
    readonly property bool dialPaused: root.paused || root.faulted || root.offline
    readonly property bool dialConnected: root.connected && !root.offline

    // While the clock shows block time, its newest arc, from the latest block
    // to now, is the block being built. The arc breathes, and the text below
    // the height describes the latest block and the one being built.
    readonly property bool showsBuildingBlock: root.state === "BLOCKCLOCK" &&
        root.buildingBlockModelRef !== null && root.buildingBlockModelRef.available &&
        root.buildingBlockModelRef.height === root.nodeModelRef.blockTipHeight + 1
    readonly property bool wantsBuildingBlock: root.state === "BLOCKCLOCK" && root.presentationActive
    onWantsBuildingBlockChanged: root.updateBuildingBlockViewer()
    Component.onCompleted: root.updateBuildingBlockViewer()

    // Breathes every two seconds. Stepping it 20 times a second, rather than
    // every frame, renders a third of the frames for a pulse this slow.
    property real buildingPulse: 1
    readonly property real buildingPulseOpacity: 0.45 + 0.55 * root.buildingPulse
    Timer {
        interval: 50
        repeat: true
        running: root.showsBuildingBlock && root.presentationActive
        onTriggered: root.buildingPulse = 0.5 + 0.5 * Math.cos(2 * Math.PI * (Date.now() % 2000) / 2000)
        onRunningChanged: if (!running) root.buildingPulse = 1
    }

    // Block ages are shown in minutes.
    property double currentTimeMs: Date.now()
    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: root.showsBuildingBlock && root.presentationActive
        onTriggered: root.currentTimeMs = Date.now()
    }

    readonly property string latestBlockText: root.showsBuildingBlock && root.buildingBlockModelRef.tipTransactionCount >= 0
        ? qsTr("%1 · %2 tx")
            .arg(Utils.formatRelativeTime(root.buildingBlockModelRef.tipTime, root.currentTimeMs))
            .arg(Number(root.buildingBlockModelRef.tipTransactionCount).toLocaleString(Qt.locale(), 'f', 0))
        : ""

    AppSettings {
        id: settings
        property alias blockclocksize: dial.scale
    }

    BlockClockDial {
        id: dial
        objectName: "blockClockDial"
        anchors.horizontalCenter: root.horizontalCenter
        scale: Theme.blockclocksize
        width: {Math.max(Math.min(200, Math.min(root.parentWidth - 30, root.parentHeight - 30)), 
                Math.min((root.parentWidth * dial.scale), (root.parentHeight * dial.scale)))}
        height: dial.width
        penWidth: root.dialPenWidth
        currentTimeFraction: root.currentTimeFraction
        blockTimeFractions: root.blockTimeFractions
        syncProgress: root.syncProgressFraction
        paused: root.dialPaused
        connected: root.dialConnected
        synced: root.synced
        backgroundColor: Theme.color.neutral2
        timeTickColor: Theme.color.neutral5
        confirmationColors: Theme.color.confirmationColors
        renderingActive: root.presentationActive
        blockLayer: root.showsBuildingBlock ? BlockClockDial.SettledBlocks : BlockClockDial.AllBlocks

        Behavior on backgroundColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on timeTickColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on confirmationColors {
            ColorAnimation { duration: 150 }
        }
    }

    // The block being built, painted apart so it can breathe without the
    // whole dial being repainted.
    BlockClockDial {
        id: newestBlock
        objectName: "blockClockNewestBlock"
        anchors.centerIn: dial
        visible: root.showsBuildingBlock
        blockLayer: BlockClockDial.NewestBlock
        // Room for the glow at the end of the segment.
        readonly property int glowMargin: Math.ceil(root.dialPenWidth * 3)
        ringMargin: glowMargin
        width: dial.width + 2 * glowMargin
        height: width
        penWidth: root.dialPenWidth
        currentTimeFraction: root.currentTimeFraction
        blockTimeFractions: root.blockTimeFractions
        syncProgress: root.syncProgressFraction
        paused: root.dialPaused
        connected: root.dialConnected
        synced: root.synced
        showTimeTicks: false
        confirmationColors: Theme.color.confirmationColors
        headColor: Theme.color.white
        renderingActive: root.presentationActive && root.showsBuildingBlock
        opacity: root.buildingPulseOpacity
    }

    Icon {
        id: bitcoinIcon
        source: "image://images/bitcoin-circle"
        color: Theme.color.neutral9
        size: Math.max(dial.width / 5, 1)
        anchors.bottom: mainText.top
        anchors.horizontalCenter: root.horizontalCenter
    }

    Label {
        id: mainText
        anchors.centerIn: dial
        font.family: "BitcoinCoreSans"
        font.styleName: "Semi Bold"
        font.pixelSize: dial.width * (4/25)
        color: Theme.color.neutral9

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    TextMetrics {
        id: subTextMetrics
        font.family: "BitcoinCoreSans"
        // The latest block's details are smaller than the clock's status words.
        font.styleName: root.showsBuildingBlock ? "Regular" : "Semi Bold"
        font.pixelSize: root.showsBuildingBlock ? Math.max(12, Math.round(dial.width * (23/500)))
                                                : Math.max(1, Math.round(dial.width * (9/100)))
        text: subText.text
    }

    Label {
        id: subText
        objectName: "blockClockSubText"
        anchors.top: mainText.bottom
        property bool estimating: root.estimating
        anchors.horizontalCenter: root.horizontalCenter
        width: dial.width * (4/5)
        horizontalAlignment: Text.AlignHCenter
        font.family: "BitcoinCoreSans"
        font.styleName: subTextMetrics.font.styleName
        readonly property int desiredPixelSize: subTextMetrics.font.pixelSize
        readonly property real desiredTextWidth: Math.max(subTextMetrics.width, subTextMetrics.advanceWidth)
        font.pixelSize: subText.desiredTextWidth > subText.width
            ? Math.max(1, Math.floor(subText.desiredPixelSize * (subText.width - 2) / subText.desiredTextWidth))
            : subText.desiredPixelSize
        elide: Text.ElideRight
        color: root.showsBuildingBlock ? Theme.color.neutral6 : Theme.color.neutral4

        Behavior on color {
            enabled: !subText.estimating
            ColorAnimation { duration: 150 }
        }

        SequentialAnimation {
            id: estimatingTime
            objectName: "blockClockEstimatingAnimation"
            running: root.presentationActive && subText.estimating
            loops: Animation.Infinite
            ColorAnimation { target: subText; property: "color"; from: Theme.color.neutral4; to: Theme.color.neutral6; duration: 1000 }
            ColorAnimation { target: subText; property: "color"; from: Theme.color.neutral6; to: Theme.color.neutral4; duration: 1000 }
        }

    }

    StatusPill {
        id: buildingBlockPill
        objectName: "blockClockBuildingBlock"
        // Smaller dials leave out the fee rates, then the height, then the pill.
        readonly property real textRoom: dial.width * (4/5) - (implicitWidth - textImplicitWidth)
        readonly property string fullText: root.buildingBlockText(true, true)
        readonly property string mediumText: root.buildingBlockText(true, false)
        readonly property string shortText: root.buildingBlockText(false, false)
        anchors.top: subText.bottom
        anchors.topMargin: dial.width / 40
        anchors.horizontalCenter: root.horizontalCenter
        visible: root.showsBuildingBlock && shortTextMetrics.advanceWidth <= textRoom
        text: fullTextMetrics.advanceWidth <= textRoom ? fullText
            : mediumTextMetrics.advanceWidth <= textRoom ? mediumText : shortText
        accentColor: Theme.color.confirmationColors[0]
        textColor: Theme.color.neutral8
        dotOpacity: root.buildingPulseOpacity

        TextMetrics {
            id: fullTextMetrics
            font: Theme.text.captionStrong.font
            text: buildingBlockPill.fullText
        }
        TextMetrics {
            id: mediumTextMetrics
            font: Theme.text.captionStrong.font
            text: buildingBlockPill.mediumText
        }
        TextMetrics {
            id: shortTextMetrics
            font: Theme.text.captionStrong.font
            text: buildingBlockPill.shortText
        }
    }

    PeersIndicator {
        objectName: "blockClockPeersIndicator"
        anchors.top: buildingBlockPill.visible ? buildingBlockPill.bottom : subText.bottom
        anchors.topMargin: dial.width / 10
        anchors.horizontalCenter: root.horizontalCenter
        numOutboundPeers: root.nodeModelRef !== null ? root.nodeModelRef.numOutboundPeers : 0
        maxNumOutboundPeers: root.nodeModelRef !== null ? Math.max(root.nodeModelRef.maxNumOutboundPeers, 1) : 1
        indicatorDimensions: dial.width * (3/200)
        indicatorSpacing: dial.width / 40
        paused: root.paused || root.faulted
        active: root.presentationActive
    }

    NetworkIndicator {
        id: networkIndicator
        objectName: "blockClockNetworkIndicator"
        show: root.showNetworkIndicator
        chainModelRef: root.chainModelRef
        anchors.top: dial.bottom
        anchors.topMargin: networkIndicator.visible ? 30 : 0
        anchors.horizontalCenter: root.horizontalCenter
    }

    states: [
        State {
            name: "IBD"; when: !faulted && !offline && !synced && !paused && connected
            PropertyChanges {
                target: root
                header: root.syncProgress
                subText: root.headerSyncActive
                    ? (root.nodeModelRef.headerPresync ? qsTr("Pre-syncing headers") : qsTr("Syncing headers"))
                    : root.syncTime
            }
        },

        State {
            name: "BLOCKCLOCK"; when: !faulted && !offline && synced && !paused && connected
            PropertyChanges {
                target: root
                header: Number(root.nodeModelRef !== null ? root.nodeModelRef.blockTipHeight : 0).toLocaleString(Qt.locale(), 'f', 0)
                subText: root.latestBlockText !== "" ? root.latestBlockText : "Blocktime"
                estimating: false
            }
        },

        State {
            name: "PAUSE"; when: paused && !faulted && !offline
            PropertyChanges {
                target: root
                header: "Paused"
                headerSize: dial.width * (3/25)
                subText: ""
                estimating: false
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: dial.width / 50
            }
        },

        State {
            name: "ERROR"; when: faulted
            PropertyChanges {
                target: root
                header: "Error"
                headerSize: dial.width * (3/25)
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
                icon.source: "image://images/error"
            }
        },

        State {
            name: "OFFLINE"; when: !faulted && offline
            PropertyChanges {
                target: root
                header: qsTr("Offline")
                headerSize: dial.width * (3/25)
                subText: qsTr("Check network")
                estimating: false
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
                icon.source: "image://images/network-light"
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: dial.width / 50
            }
        },

        State {
            name: "CONNECTING"; when: !faulted && !paused && !connected
            PropertyChanges {
                target: root
                header: qsTr("Connecting")
                headerSize: dial.width * (3/25)
                subText: qsTr("Please wait")
                estimating: false
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: dial.width / 50
            }
        }
    ]


    function updateBuildingBlockViewer() {
        if (root.buildingBlockModelRef !== null) root.buildingBlockModelRef.setViewerActive(root, root.wantsBuildingBlock)
    }

    function buildingBlockText(withHeight, withFeeRates) {
        if (!root.showsBuildingBlock) return ""
        const model = root.buildingBlockModelRef
        const transactions = Number(model.transactionCount).toLocaleString(Qt.locale(), 'f', 0)
        if (!withHeight) return qsTr("Building · %1 tx").arg(transactions)
        let text = qsTr("Building %1 · %2 tx")
            .arg(Number(model.height).toLocaleString(Qt.locale(), 'f', 0))
            .arg(transactions)
        if (withFeeRates && model.transactionCount > 0) {
            const min = Utils.formatFeeRate(model.minFeeRate)
            const max = Utils.formatFeeRate(model.maxFeeRate)
            text += " · " + (min === max ? qsTr("%1 sat/vB").arg(min) : qsTr("%1–%2 sat/vB").arg(min).arg(max))
        }
        return text
    }

    function formatProgressPercentage(progress, complete) {
        if (complete === true) {
            return "100%"
        }
        // Never present an estimate as complete while initial sync is pending.
        if (progress >= 99.9) {
            return "99.9%"
        }
        if (progress >= 1) {
            return Math.round(progress) + "%"
        } else if (progress >= 0.1) {
            return progress.toFixed(1) + "%"
        } else if (progress >= 0.01) {
            return progress.toFixed(2) + "%"
        } else {
            return "0%"
        }
    }
}
