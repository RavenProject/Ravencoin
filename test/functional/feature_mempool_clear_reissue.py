#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""A live mempool clear must release pending asset reissue reservations."""

from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class MempoolClearReissueTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        address = node.getnewaddress()
        asset_name = 'MEMPOOLCLEARTEST'
        node.generate(500)
        node.issue(asset_name, 1, address)
        node.generate(1)

        first_txid = node.reissue(asset_name, 1, address)[0]
        first_raw = node.gettransaction(first_txid)['hex']
        assert first_txid in node.getrawmempool()
        node.clearmempool()
        assert_equal(node.getrawmempool(), [])

        replacement = from_hex(CTransaction(), first_raw)
        replacement.nLockTime = 1
        signed = node.signrawtransaction(to_hex(replacement))
        assert_equal(signed['complete'], True)
        second_txid = node.sendrawtransaction(signed['hex'])
        assert second_txid != first_txid
        block_hash = node.generate(1)[0]
        assert second_txid in node.getblock(block_hash)['tx']


if __name__ == '__main__':
    MempoolClearReissueTest().main()
