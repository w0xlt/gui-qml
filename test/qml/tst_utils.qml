// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

import "../../qml/controls"
import "../../qml/controls/utils.js" as Utils

TestCase {
    name: "Utils"

    // Map a peer count to the matching connection-strength icon. Thresholds
    // mirror the Qt Widgets GUI (connect_0 .. connect_4): 0, 1-3, 4-6, 7-9, 10+.
    function test_nodeConnectionIcon_thresholds_data() {
        return [
            { tag: "zero",     peers: 0,  icon: "image://images/node-0-connections" },
            { tag: "negative", peers: -1, icon: "image://images/node-0-connections" },
            { tag: "one",      peers: 1,  icon: "image://images/node-1-connection" },
            { tag: "three",    peers: 3,  icon: "image://images/node-1-connection" },
            { tag: "four",     peers: 4,  icon: "image://images/node-2-connections" },
            { tag: "six",      peers: 6,  icon: "image://images/node-2-connections" },
            { tag: "seven",    peers: 7,  icon: "image://images/node-3-connections" },
            { tag: "nine",     peers: 9,  icon: "image://images/node-3-connections" },
            { tag: "ten",      peers: 10, icon: "image://images/node-4-connections" },
            { tag: "many",     peers: 99, icon: "image://images/node-4-connections" },
        ]
    }

    function test_nodeConnectionIcon_thresholds(data) {
        compare(Utils.nodeConnectionIcon(data.peers), data.icon)
    }

    function localized(value, decimals) {
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals)
    }

    function test_mempoolFeeLevels_have_one_color_each() {
        compare(Utils.mempoolFeeLevels.length, Theme.color.mempoolFeeColors.length)
    }

    function test_mempoolFeeLevelIndex_data() {
        return [
            { tag: "negative",       rate: -1,   index: 0 },
            { tag: "below one",      rate: 0.5,  index: 0 },
            { tag: "one",            rate: 1,    index: 1 },
            { tag: "between levels", rate: 7.9,  index: 6 },
            { tag: "on a level",     rate: 8,    index: 7 },
            { tag: "top level",      rate: 2000, index: 38 },
            { tag: "above top",      rate: 5000, index: 38 },
        ]
    }

    function test_mempoolFeeLevelIndex(data) {
        compare(Utils.mempoolFeeLevelIndex(data.rate), data.index)
    }

    function test_formatFeeRate_data() {
        return [
            { tag: "fraction",     rate: 0.1234, value: 0.12, decimals: 2 },
            { tag: "single digit", rate: 6.384,  value: 6.38, decimals: 2 },
            { tag: "tens",         rate: 77.53,  value: 77.5, decimals: 1 },
            { tag: "hundreds",     rate: 221.4,  value: 221,  decimals: 0 },
        ]
    }

    function test_formatFeeRate(data) {
        compare(Utils.formatFeeRate(data.rate), localized(data.value, data.decimals))
    }

    function test_formatBlockFees_data() {
        return [
            { tag: "zero",           sats: 0,         value: 0,          decimals: 2 },
            { tag: "two decimals",   sats: 2000000,   value: 0.02,       decimals: 2 },
            { tag: "three decimals", sats: 3300000,   value: 0.033,      decimals: 3 },
            { tag: "rounded",        sats: 3349999,   value: 0.033,      decimals: 3 },
            { tag: "whole coins",    sats: 125000000, value: 1.25,       decimals: 2 },
            { tag: "small",          sats: 4000,      value: 0.00004,    decimals: 5 },
            { tag: "tiny",           sats: 12345,     value: 0.00012345, decimals: 8 },
        ]
    }

    function test_formatBlockFees(data) {
        compare(Utils.formatBlockFees(data.sats), localized(data.value, data.decimals) + " BTC")
    }
}
