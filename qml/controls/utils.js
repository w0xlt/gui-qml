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

// Time since isoString, as of nowMs (milliseconds since the epoch) if given.
function formatRelativeTime(isoString, nowMs) {
    if (!isoString) return ""
    var then = new Date(isoString)
    var now = nowMs === undefined ? new Date() : new Date(nowMs)
    var diffSec = Math.floor((now - then) / 1000)
    if (diffSec < 60) return qsTr("just now")
    var m = Math.floor(diffSec / 60)
    if (diffSec < 3600) return m === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(m)
    var h = Math.floor(diffSec / 3600)
    if (diffSec < 86400) return h === 1 ? qsTr("1 hour ago") : qsTr("%1 hours ago").arg(h)
    var d = Math.floor(diffSec / 86400)
    return d === 1 ? qsTr("1 day ago") : qsTr("%1 days ago").arg(d)
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

// Rates in vB/s where mempool.space's incoming transactions chart changes
// color. Theme.color.mempoolIncomingColors has one more color than limits.
var mempoolIncomingLimits = [1667, 2000, 2500, 3000, 3500]

// Fee rate levels in sat/vB that mempool.space uses to color fee rates.
// Theme.color.mempoolFeeColors has one color per level.
var mempoolFeeLevels = [0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100,
                        125, 150, 175, 200, 250, 300, 350, 400, 500, 600, 700, 800, 900, 1000,
                        1200, 1400, 1600, 1800, 2000]

// Index of the highest fee level at or below feeRate.
function mempoolFeeLevelIndex(feeRate) {
    for (var i = mempoolFeeLevels.length - 1; i > 0; --i) {
        if (feeRate >= mempoolFeeLevels[i]) return i
    }
    return 0
}

// About three significant digits: 0.12, 6.38, 77.5, 221.
function formatFeeRate(feeRate) {
    var decimals = feeRate >= 100 ? 0 : (feeRate >= 10 ? 1 : 2)
    return Number(feeRate).toLocaleString(Qt.locale(), 'f', decimals)
}

var durationUnitSeconds = {
    year: 31536000, month: 2592000, week: 604800, day: 86400, hour: 3600, minute: 60, second: 1,
}
// Most of a unit shown before the next larger one is used instead.
var durationUnitLimits = { year: 100, month: 18, week: 12, day: 31, hour: 48, minute: 90, second: 90 }

function formatDurationUnit(value, unit, fractionDigits) {
    var number = Number(value).toLocaleString(Qt.locale(), 'f', value % 1 === 0 ? 0 : fractionDigits)
    var one = value === 1
    switch (unit) {
    case "year": return one ? qsTr("1 year") : qsTr("%1 years").arg(number)
    case "month": return one ? qsTr("1 month") : qsTr("%1 months").arg(number)
    case "week": return one ? qsTr("1 week") : qsTr("%1 weeks").arg(number)
    case "day": return one ? qsTr("1 day") : qsTr("%1 days").arg(number)
    case "hour": return one ? qsTr("1 hour") : qsTr("%1 hours").arg(number)
    case "minute": return one ? qsTr("1 minute") : qsTr("%1 minutes").arg(number)
    }
    return one ? qsTr("1 second") : qsTr("%1 seconds").arg(number)
}

// Durations as mempool.space writes them, e.g. "9.2 minutes", or with until,
// "In ~3 days". Options, as in its TimeService.calculate():
// - precision: use up to this many smaller units while their count stays
//   small, e.g. "In ~36 hours" rather than "In ~1 day".
// - numUnits: how many units to write, e.g. 2 for "In ~3 years, 165 days".
// - units: the units to use, largest first.
// - fractionDigits: decimals of the last unit, e.g. 1 for "9.2 minutes".
function formatDuration(seconds, until, options) {
    options = options || {}
    var units = options.units || ["year", "month", "week", "day", "hour", "minute", "second"]
    var precision = options.precision || 0
    var numUnits = until ? (options.numUnits || 1) : 1
    var factor = Math.pow(10, options.fractionDigits || 0)
    if (until && seconds < 60) seconds = 60
    var parts = []
    for (var i = 0; i < units.length && parts.length < numUnits; ++i) {
        var unit = units[i]
        var shownUnit = units[Math.min(units.length - 1, i + precision)]
        if (Math.round(seconds / durationUnitSeconds[shownUnit]) > durationUnitLimits[shownUnit]) shownUnit = unit
        var count = Math.floor(seconds / durationUnitSeconds[unit])
        if (shownUnit === units[units.length - 1]) count = Math.max(1, count)
        if (count <= 0) continue
        var scaled = seconds / durationUnitSeconds[shownUnit] * factor
        var value = (until ? Math.floor(scaled) : Math.round(scaled)) / factor
        parts.push(formatDurationUnit(value, shownUnit, options.fractionDigits || 0))
        seconds -= value * durationUnitSeconds[shownUnit]
    }
    var text = parts.join(", ")
    return until ? qsTr("In ~%1").arg(text) : text
}

// Recommended fee rates as mempool.space shows them: whole numbers from 1
// sat/vB, otherwise one decimal, or two below 0.1.
function formatRecommendedFeeRate(feeRate) {
    var decimals = feeRate >= 1 ? 0 : (feeRate >= 0.1 ? 1 : 2)
    return Number(feeRate).toLocaleString(Qt.locale(), 'f', decimals)
}

// Byte counts in the largest decimal unit that keeps them under 1000, as
// mempool.space writes them: with significantDigits digits, e.g. 3 for
// "93.2 kB" or "287 MB", or else whole, e.g. "300 MB". Bytes are always whole.
function formatBytes(bytes, significantDigits) {
    var units = ["B", "kB", "MB", "GB", "TB"]
    var unit = 0
    var value = bytes
    while (value >= 1000 && unit < units.length - 1) {
        value /= 1000
        ++unit
    }
    var decimals = 0
    if (significantDigits && unit > 0 && value > 0) {
        decimals = Math.max(0, significantDigits - 1 - Math.floor(Math.log10(value)))
        // Rounding up to 1000 moves to the next unit, e.g. 999.6 kB is 1.00 MB.
        if (Math.round(value * Math.pow(10, decimals)) >= 1000 * Math.pow(10, decimals) && unit < units.length - 1) {
            value /= 1000
            ++unit
            decimals = significantDigits - 1
        }
    }
    return Number(value).toLocaleString(Qt.locale(), 'f', decimals) + " " + units[unit]
}

// Fee totals in BTC with two or three decimals, or as many as needed below
// 0.001 BTC.
function formatBlockFees(satoshis) {
    var decimals = 2
    if (satoshis > 0 && satoshis < 100000) {
        decimals = 8
        while (decimals > 3 && satoshis % Math.pow(10, 9 - decimals) === 0) --decimals
    } else if (Math.round(satoshis / 100000) % 10 !== 0) {
        decimals = 3
    }
    return Number(satoshis / 100000000).toLocaleString(Qt.locale(), 'f', decimals) + " BTC"
}
