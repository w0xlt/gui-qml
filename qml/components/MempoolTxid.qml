// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../controls"

// A transaction id shortened in the middle to fit, shown in full on hover
// and copied on click.
CoreText {
    id: root

    property string txid: ""
    property bool copied: false

    horizontalAlignment: Text.AlignLeft
    wrap: false
    elide: Text.ElideMiddle
    font: Theme.text.monoCaption.font
    color: hover.hovered ? Theme.color.orange : Theme.color.neutral9
    text: txid
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Copy transaction ID %1").arg(txid)

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: if (!hovered) root.copied = false
    }
    TapHandler {
        onTapped: {
            Clipboard.setText(root.txid)
            root.copied = true
        }
    }
    ToolTip {
        visible: hover.hovered
        delay: 400
        text: root.copied ? qsTr("Copied") : root.txid
    }
}
