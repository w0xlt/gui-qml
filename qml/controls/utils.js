// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// utils.js

// Map a live peer/connection count to one of the five node-connection icon
// tiers (image provider aliases node-0-connections .. node-4-connections),
// each showing one more filled node than the last. Thresholds mirror the Qt
// Widgets GUI's connection-strength icon (connect_0 .. connect_4).
function nodeConnectionIcon(numPeers) {
    if (numPeers <= 0) return "image://images/node-0-connections"
    if (numPeers <= 3) return "image://images/node-1-connection"
    if (numPeers <= 6) return "image://images/node-2-connections"
    if (numPeers <= 9) return "image://images/node-3-connections"
    return "image://images/node-4-connections"
}

// Relative to now, or to the given time.
function formatRelativeTime(isoString, now) {
    if (!isoString) return ""
    var then = new Date(isoString)
    var current = now === undefined ? new Date() : new Date(now)
    var diffSec = Math.floor((current - then) / 1000)
    if (diffSec < 60) return qsTr("just now")
    var m = Math.floor(diffSec / 60)
    if (diffSec < 3600) return m === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(m)
    var h = Math.floor(diffSec / 3600)
    if (diffSec < 86400) return h === 1 ? qsTr("1 hour ago") : qsTr("%1 hours ago").arg(h)
    var d = Math.floor(diffSec / 86400)
    return d === 1 ? qsTr("1 day ago") : qsTr("%1 days ago").arg(d)
}

// A fee rate in sat/vB, with more decimals for low rates.
function formatFeeRate(rate) {
    var decimals = rate >= 10 ? 0 : rate >= 1 ? 1 : 2
    var rounded = Number(rate.toFixed(decimals))
    while (decimals > 0 && Number(rounded.toFixed(decimals - 1)) === rounded) --decimals
    return rounded.toLocaleString(Qt.locale(), 'f', decimals)
}

// Decimal units, like block explorers.
function formatBlockSize(bytes) {
    if (bytes >= 1000000) return qsTr("%1 MB").arg((bytes / 1000000).toLocaleString(Qt.locale(), 'f', 2))
    if (bytes >= 1000) return qsTr("%1 kB").arg((bytes / 1000).toLocaleString(Qt.locale(), 'f', bytes >= 10000 ? 0 : 1))
    return qsTr("%1 B").arg(bytes)
}

// A time between blocks, in seconds.
function formatInterval(seconds) {
    if (seconds < 60) return qsTr("%1 s").arg(seconds)
    var minutes = Math.round(seconds / 60)
    if (minutes < 60) return qsTr("%1 min").arg(minutes)
    return qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
}

function formatRemainingSyncTime(milliseconds) {
    var minutes = Math.floor(milliseconds / 60000);
    var seconds = Math.floor((milliseconds % 60000) / 1000);
    var weeks = Math.floor(minutes / 10080);
    minutes %= 10080;
    var days = Math.floor(minutes / 1440);
    minutes %= 1440;
    var hours = Math.floor(minutes / 60);
    minutes %= 60;
    var result = "";
    var estimatingStatus = false;

    if (weeks > 0) {
        return {
            text: "~" + weeks + (weeks === 1 ? " week" : " weeks") + " left",
            estimating: false
        };
    }
    if (days > 0) {
        return {
            text: "~" + days + (days === 1 ? " day" : " days") + " left",
            estimating: false
        };
    }
    if (hours >= 5) {
        return {
            text: "~" + hours + (hours === 1 ? " hour" : " hours") + " left",
            estimating: false
        };
    }
    if (hours > 0) {
        return {
            text: "~" + hours + "h " + minutes + "m" + " left",
            estimating: false
        };
    }
    if (minutes >= 5) {
        return {
            text: "~" + minutes + (minutes === 1 ? " minute" : " minutes") + " left",
            estimating: false
        };
    }
    if (minutes > 0) {
        return {
            text: "~" + minutes + "m " + seconds + "s" + " left",
            estimating: false
        };
    }
    if (seconds > 0) {
        return {
            text: "~" + seconds + (seconds === 1 ? " second" : " seconds") + " left",
            estimating: false
        };
    } else {
        return {
            text: "Estimating",
            estimating: true
        };
    }
}
