// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../controls"
import "../controls/utils.js" as Utils

// The mempool's minimum fee, memory usage and size, over the rate at which
// transactions entered it in the last two hours, like mempool.space's
// incoming transactions chart.
Control {
    id: root

    // MempoolBlocksModel's mempoolInfo.
    property var mempoolInfo: ({})
    // Samples of IncomingTransactionsModel: times in milliseconds since the
    // epoch and vbytes per second.
    property var times: []
    property var rates: []
    // When counting started, or 0 while the node loads its saved mempool.
    property real recordingSince: 0

    readonly property bool infoAvailable: mempoolInfo.transactionCount !== undefined
    // The minimum fee rises above the minimum relay fee while the mempool
    // is full and purges its lowest fee transactions.
    readonly property bool purging: infoAvailable && mempoolInfo.minFee !== mempoolInfo.minRelayFee
    readonly property real usageRatio: infoAvailable && mempoolInfo.maxUsage > 0 ? mempoolInfo.usage / mempoolInfo.maxUsage : 0
    // Too narrow for three columns, so the memory usage is left out.
    readonly property bool compact: availableWidth < 400

    readonly property real windowMs: 2 * 60 * 60 * 1000
    readonly property real endTime: times.length > 0 ? times[times.length - 1] : (recordingSince > 0 ? recordingSince : Date.now())
    readonly property real startTime: endTime - windowMs
    // Always room for the line blocks clear transactions at, like
    // mempool.space.
    readonly property real maxRate: {
        let max = 1800
        for (let i = 0; i < rates.length; ++i) max = Math.max(max, rates[i])
        return max
    }
    readonly property var rateTicks: {
        const step = niceStep(maxRate / 4)
        const ticks = []
        for (let value = 0; value <= maxRate; value += step) ticks.push(value)
        return ticks
    }
    readonly property var timeTicks: {
        const step = (plot.width < 400 ? 60 : 30) * 60 * 1000
        const ticks = []
        for (let time = Math.ceil(startTime / step) * step; time <= endTime; time += step) ticks.push(time)
        return ticks
    }

    // The smallest of 1, 2, 2.5 and 5 times a power of 10 that is at least
    // value.
    function niceStep(value) {
        const power = Math.pow(10, Math.floor(Math.log10(value)))
        for (const factor of [1, 2, 2.5, 5, 10]) {
            if (factor * power >= value) return factor * power
        }
        return 10 * power
    }

    function rateColor(rate) {
        let band = 0
        while (band < Utils.mempoolIncomingLimits.length && rate > Utils.mempoolIncomingLimits[band]) ++band
        return Theme.color.mempoolIncomingColors[band]
    }

    function formatTime(time) {
        return new Date(time).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }

    padding: 20

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
        spacing: 16

        Row {
            id: info
            width: parent.width
            height: 48

            Repeater {
                // Minimum fee, memory usage, transaction count.
                model: 3

                Column {
                    objectName: "mempoolInfoItem"
                    visible: !(index === 1 && root.compact)
                    width: info.width / (root.compact ? 2 : 3)
                    spacing: 6

                    CoreText {
                        objectName: "mempoolInfoTitle"
                        width: parent.width
                        wrap: false
                        color: Theme.color.neutral7
                        text: index === 0 ? (root.purging ? qsTr("Purging") : qsTr("Minimum fee"))
                            : (index === 1 ? qsTr("Memory usage") : qsTr("Unconfirmed"))
                    }

                    Row {
                        objectName: "mempoolInfoValue"
                        visible: index !== 1 && root.infoAvailable
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4

                        CoreText {
                            id: infoValue
                            objectName: "mempoolInfoNumber"
                            wrap: false
                            font.pixelSize: 18
                            text: {
                                if (!root.infoAvailable) return ""
                                if (index === 0) return (root.purging ? "< " : "") + Utils.formatFeeRate(root.mempoolInfo.minFee)
                                return Number(root.mempoolInfo.transactionCount).toLocaleString(Qt.locale(), 'f', 0)
                            }
                        }
                        CoreText {
                            anchors.baseline: infoValue.baseline
                            wrap: false
                            color: Theme.color.neutral7
                            font.pixelSize: 12
                            text: index === 0 ? qsTr("sat/vB") : qsTr("TXs")
                        }
                    }

                    Rectangle {
                        objectName: "mempoolMemoryBar"
                        visible: index === 1 && root.infoAvailable
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(200, parent.width - 10)
                        height: 20
                        radius: 3
                        clip: true
                        color: Theme.color.mempoolProgressEmpty

                        Rectangle {
                            objectName: "mempoolMemoryUsed"
                            width: parent.width * Math.min(1, root.usageRatio)
                            height: parent.height
                            color: root.usageRatio <= 0.5 ? Theme.color.green
                                : (root.usageRatio <= 0.75 ? Theme.color.amber : Theme.color.red)
                        }
                        CoreText {
                            objectName: "mempoolMemoryText"
                            anchors.centerIn: parent
                            wrap: false
                            font.pixelSize: 12
                            color: Theme.color.white
                            text: root.infoAvailable
                                ? Utils.formatBytes(root.mempoolInfo.usage, 3) + " / " + Utils.formatBytes(root.mempoolInfo.maxUsage)
                                : ""
                        }
                    }

                    Skeleton {
                        visible: !root.infoAvailable
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(100, parent.width - 10)
                        height: 18
                        baseColor: Theme.color.neutral2
                        highlightColor: Theme.color.neutral3
                        loading: visible
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: title.height

            CoreText {
                id: title
                anchors.left: parent.left
                bold: true
                wrap: false
                font.pixelSize: 15
                text: qsTr("Incoming Transactions")
            }
            CoreText {
                objectName: "mempoolIncomingSince"
                anchors.right: parent.right
                anchors.baseline: title.baseline
                visible: root.recordingSince > root.startTime
                wrap: false
                color: Theme.color.neutral6
                font.pixelSize: 12
                // History starts when the node does.
                text: qsTr("Since %1").arg(root.formatTime(root.recordingSince))
            }
        }

        Item {
            id: chart
            width: parent.width
            height: 215

            // Rate axis.
            Repeater {
                model: root.rateTicks

                CoreText {
                    x: 0
                    y: plot.y + plot.height * (1 - modelData / root.maxRate) - height / 2
                    width: plot.x - 8
                    horizontalAlignment: Text.AlignRight
                    wrap: false
                    color: Theme.color.neutral6
                    font.pixelSize: 11
                    text: Number(modelData).toLocaleString(Qt.locale(), 'f', 0)
                }
            }

            // Time axis.
            Repeater {
                model: root.timeTicks

                CoreText {
                    x: plot.x + plot.width * (modelData - root.startTime) / root.windowMs - width / 2
                    y: plot.y + plot.height + 8
                    wrap: false
                    color: Theme.color.neutral6
                    font.pixelSize: 11
                    text: root.formatTime(modelData)
                }
            }

            BandedLineGraph {
                id: plot
                objectName: "mempoolIncomingGraph"
                x: 50
                y: 10
                width: parent.width - x - 20
                height: parent.height - y - 28
                xValues: root.times
                yValues: root.rates
                minX: root.startTime
                maxX: root.endTime
                maxY: root.maxRate
                bandLimits: Utils.mempoolIncomingLimits
                bandColors: Theme.color.mempoolIncomingColors
                gridValues: root.rateTicks
                gridColor: Qt.rgba(Theme.color.neutral6.r, Theme.color.neutral6.g, Theme.color.neutral6.b, 0.4)
                // The rate blocks clear transactions at: 1,000,000 vB every
                // 10 minutes.
                referenceValue: 1667
                referenceColor: Theme.color.neutral9

                HoverHandler {
                    id: plotHover
                }

                // The sample nearest the pointer.
                readonly property int hoveredIndex: {
                    if (!plotHover.hovered || root.times.length === 0) return -1
                    const time = root.startTime + plotHover.point.position.x / width * root.windowMs
                    let nearest = -1
                    for (let i = 0; i < root.times.length; ++i) {
                        if (root.times[i] < root.startTime) continue
                        if (nearest < 0 || Math.abs(root.times[i] - time) < Math.abs(root.times[nearest] - time)) nearest = i
                    }
                    return nearest
                }

                Rectangle {
                    visible: plot.hoveredIndex >= 0
                    x: visible ? plot.width * (root.times[plot.hoveredIndex] - root.startTime) / root.windowMs : 0
                    width: 1
                    height: plot.height
                    color: Theme.color.neutral6
                }

                Rectangle {
                    objectName: "mempoolIncomingTip"
                    visible: plot.hoveredIndex >= 0
                    readonly property real pointerX: plotHover.point.position.x
                    x: pointerX < plot.width / 2 ? pointerX + 12 : pointerX - width - 12
                    y: 0
                    width: tipColumn.width + 16
                    height: tipColumn.height + 12
                    radius: 4
                    color: Theme.color.neutral2
                    border.color: Theme.color.neutral3

                    Column {
                        id: tipColumn
                        x: 8
                        y: 6
                        spacing: 2

                        CoreText {
                            objectName: "mempoolIncomingTipTime"
                            wrap: false
                            color: Theme.color.neutral7
                            font.pixelSize: 11
                            text: plot.hoveredIndex >= 0 ? root.formatTime(root.times[plot.hoveredIndex]) : ""
                        }
                        Row {
                            spacing: 6

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: plot.hoveredIndex >= 0 ? root.rateColor(root.rates[plot.hoveredIndex]) : "transparent"
                            }
                            CoreText {
                                objectName: "mempoolIncomingTipRate"
                                wrap: false
                                font.pixelSize: 13
                                text: plot.hoveredIndex >= 0
                                    ? qsTr("%1 vB/s").arg(Math.round(root.rates[plot.hoveredIndex]).toLocaleString(Qt.locale(), 'f', 0))
                                    : ""
                            }
                        }
                    }
                }
            }

            CoreText {
                objectName: "mempoolIncomingMessage"
                visible: root.times.length === 0
                x: plot.x
                y: plot.y
                width: plot.width
                height: plot.height
                color: Theme.color.neutral7
                text: root.recordingSince > 0
                    ? qsTr("Counting transactions as they arrive. The first point appears after a minute.")
                    : qsTr("Waiting for the node to load its saved mempool.")
            }
        }
    }
}
