// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../controls/utils.js" as Utils

// Fee rates to pay for each priority, under a bar colored from the minimum
// fee rate to the high priority one, like mempool.space's transaction fees.
Column {
    id: root

    // Fee rates in sat/vB, keyed fastest, halfHour, hour, economy and minimum.
    property var fees: ({})
    property bool loaded: false
    // Height the card is at least, e.g. to line up with a card beside it.
    // Its content stays centered.
    property real minimumCardHeight: 0
    readonly property real cardImplicitHeight: card.contentItem.implicitHeight + 40

    readonly property real contentWidth: card.availableWidth
    // Too narrow for four columns, so No Priority is left out.
    readonly property bool compact: contentWidth < 400
    // No Priority, a hatched gap, then the three priorities in one bar.
    readonly property real slotWidth: compact ? contentWidth / 3 : contentWidth * 5 / 21
    readonly property real priorityX: compact ? 0 : contentWidth * 6 / 21
    readonly property color startColor: loaded ? feeColor(fees.minimum) : Theme.color.neutral3
    readonly property color endColor: loaded ? feeColor(fees.fastest) : Theme.color.neutral3
    readonly property var priorities: [
        {
            key: "economy",
            label: qsTr("No Priority"),
            tip: qsTr("Either twice the minimum, or the Low Priority rate if lower"),
        },
        {
            key: "hour",
            label: qsTr("Low Priority"),
            tip: qsTr("Usually places your transaction between the second and third projected blocks"),
        },
        {
            key: "halfHour",
            label: qsTr("Medium Priority"),
            tip: qsTr("Usually places your transaction between the first and second projected blocks"),
        },
        {
            key: "fastest",
            label: qsTr("High Priority"),
            tip: qsTr("Places your transaction in the first projected block"),
        },
    ]

    function feeColor(feeRate) {
        return Theme.color.mempoolFeeColors[Utils.mempoolFeeLevelIndex(feeRate || 0)]
    }

    spacing: 8

    CoreText {
        width: parent.width
        bold: true
        color: Theme.color.neutral7
        text: qsTr("Transaction Fees")
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
            implicitHeight: bars.height + 12 + 26

            Item {
                id: bars
                width: parent.width
                height: 22

                Rectangle {
                    objectName: "mempoolFeesNoPriorityBar"
                    visible: !root.compact
                    width: root.slotWidth
                    height: parent.height
                    color: root.startColor
                    Behavior on color { ColorAnimation { duration: 1000 } }
                }

                Row {
                    visible: !root.compact
                    x: root.slotWidth
                    width: root.priorityX - root.slotWidth
                    height: parent.height
                    spacing: 2
                    clip: true

                    Repeater {
                        model: Math.max(0, Math.ceil(parent.width / 4))
                        Rectangle {
                            width: 2
                            height: parent.height
                            color: Theme.color.neutral4
                        }
                    }
                }

                // Rounded on the right only.
                Item {
                    x: root.priorityX
                    width: parent.width - root.priorityX
                    height: parent.height
                    clip: true

                    Rectangle {
                        objectName: "mempoolFeesPriorityBar"
                        x: -radius
                        width: parent.width + radius
                        height: parent.height
                        radius: 10
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop {
                                position: 0
                                color: root.startColor
                                Behavior on color { ColorAnimation { duration: 1000 } }
                            }
                            GradientStop {
                                position: 1
                                color: root.endColor
                                Behavior on color { ColorAnimation { duration: 1000 } }
                            }
                        }
                    }
                }
            }

            Repeater {
                model: root.priorities

                Item {
                    objectName: "mempoolFee_" + modelData.key
                    visible: index > 0 || !root.compact
                    x: index === 0 ? 0 : root.priorityX + (index - 1) * root.slotWidth
                    width: root.slotWidth
                    height: parent.height

                    CoreText {
                        objectName: "mempoolFeeLabel"
                        width: parent.width
                        height: bars.height
                        leftPadding: 5
                        rightPadding: 5
                        wrap: false
                        elide: Text.ElideRight
                        color: root.loaded ? Theme.color.white : Theme.color.neutral9
                        font.pixelSize: 12
                        text: modelData.label

                        HoverHandler { id: labelHover }
                        ToolTip {
                            visible: labelHover.hovered
                            delay: 400
                            text: modelData.tip
                        }
                    }

                    Row {
                        objectName: "mempoolFeeRate"
                        visible: root.loaded
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: bars.height + 12
                        spacing: 4

                        CoreText {
                            id: rateText
                            objectName: "mempoolFeeRateValue"
                            wrap: false
                            font.pixelSize: 20
                            text: Utils.formatRecommendedFeeRate(root.fees[modelData.key] || 0)
                        }
                        CoreText {
                            anchors.baseline: rateText.baseline
                            wrap: false
                            color: Theme.color.neutral7
                            font.pixelSize: 12
                            text: qsTr("sat/vB")
                        }
                    }

                    Skeleton {
                        visible: !root.loaded
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: bars.height + 15
                        width: Math.min(70, parent.width - 10)
                        height: 20
                        baseColor: Theme.color.neutral2
                        highlightColor: Theme.color.neutral3
                        loading: visible
                    }
                }
            }
        }
    }
}
