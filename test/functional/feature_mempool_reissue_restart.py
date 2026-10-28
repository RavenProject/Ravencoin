#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""A restart without a mempool must not retain pending reissue locks."""

from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class MempoolReissueRestartTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex', '-persistmempool=0']]

    def run_test(self):
        node = self.nodes[0]
        address = node.getnewaddress()
        asset_name = 'REISSUERESTART'
        node.generate(500)
        node.issue(asset_name, 1, address)
        node.generate(1)

        first_txid = node.reissue(asset_name, 1, address)[0]
        first_raw = node.gettransaction(first_txid)['hex']
        assert first_txid in node.getrawmempool()
        self.restart_node(0, ['-assetindex', '-persistmempool=0',
                              '-walletbroadcast=0'])
        assert_equal(node.getrawmempool(), [])

        replacement = from_hex(CTransaction(), first_raw)
        replacement.nLockTime = 1
        signed = node.signrawtransaction(to_hex(replacement))
        assert_equal(signed['complete'], True)
        second_txid = node.sendrawtransaction(signed['hex'])
        assert second_txid != first_txid
        assert second_txid in node.getrawmempool()
        node.generate(1)

        # The converse must also hold: a reissue actually restored from
        # mempool.dat retains the anti-chaining reservation after restart.
        self.restart_node(0, ['-assetindex', '-persistmempool=1'])
        third_txid = node.reissue(asset_name, 1, address)[0]
        third_raw = node.gettransaction(third_txid)['hex']
        self.restart_node(0, ['-assetindex', '-persistmempool=1',
                              '-walletbroadcast=0'])
        assert third_txid in node.getrawmempool()
        assert node.getcacheinfo()[0]['reissue tracking (memory only)'] > 0
        conflicting = from_hex(CTransaction(), third_raw)
        conflicting.nLockTime = 1
        conflicting_signed = node.signrawtransaction(to_hex(conflicting))
        assert_equal(conflicting_signed['complete'], True)
        node.clearmempool()
        assert_equal(node.getcacheinfo()[0]['reissue tracking (memory only)'], 0)
        replacement_txid = node.sendrawtransaction(conflicting_signed['hex'])
        assert replacement_txid != third_txid


if __name__ == '__main__':
    MempoolReissueRestartTest().main()
