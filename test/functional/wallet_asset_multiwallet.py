#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check that asset operations and reward retries use the selected wallet."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class AssetMultiWalletTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-wallet=w1', '-wallet=w2', '-assetindex', '-minrewardheight=15']]

    def run_test(self):
        node = self.nodes[0]
        w1 = node.get_wallet_rpc('w1')
        w2 = node.get_wallet_rpc('w2')

        w1.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')
        w1.sendtoaddress(w2.getnewaddress(), 1000)
        w1.generate(1)

        asset_owner_address = w2.getnewaddress()
        w2.issue('MULTIWALLET', 1, asset_owner_address)
        w1.generate(1)

        # Reopen before the preflight check to use fully persisted wallet state.
        self.stop_node(0)
        self.start_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex', '-minrewardheight=15'])
        node = self.nodes[0]
        w1 = node.get_wallet_rpc('w1')
        w2 = node.get_wallet_rpc('w2')

        w2.issue('MULTIWALLET/SUB', 1, w2.getnewaddress())
        w1.generate(1)
        assert 'MULTIWALLET!' in w2.listmyassets()
        assert 'MULTIWALLET!' not in w1.listmyassets()

        self.stop_node(0)
        self.start_node(0, ['-wallet=w2', '-wallet=w1', '-assetindex', '-minrewardheight=15'])
        node = self.nodes[0]
        w1 = node.get_wallet_rpc('w1')
        w2 = node.get_wallet_rpc('w2')

        w1.issue('MULTIWALLET2', 1, w1.getnewaddress())
        w1.generate(1)
        assert 'MULTIWALLET2!' in w1.listmyassets()
        assert 'MULTIWALLET2!' not in w2.listmyassets()
        w1.issue('MULTIWALLET2/SUB', 1, w1.getnewaddress())

        assert_raises_rpc_error(
            -32600, "Wallet doesn't have asset: MULTIWALLET!",
            w1.issue, 'MULTIWALLET/UNOWNED', 1, w1.getnewaddress())

        # A pending reward initiated by w2 must not be retried using wallet w1.
        self.stop_node(0)
        self.start_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex', '-minrewardheight=15'])
        node = self.nodes[0]
        w1 = node.get_wallet_rpc('w1')
        w2 = node.get_wallet_rpc('w2')
        snapshot_height = node.getblockcount() + 1
        w2.requestsnapshot('MULTIWALLET', snapshot_height)
        w1.generate(31)
        received_before = w2.getreceivedbyaddress(asset_owner_address, 1)
        w2.distributereward('MULTIWALLET', snapshot_height, 'RVN', 2000)
        assert_equal(
            w2.getdistributestatus('MULTIWALLET', snapshot_height, 'RVN', 2000)['Status'], 3)

        w1.generate(2)
        assert_equal(w2.getreceivedbyaddress(asset_owner_address, 1), received_before)

        w1.sendtoaddress(w2.getnewaddress(), 3000)
        w1.generate(4)
        assert_equal(w2.getreceivedbyaddress(asset_owner_address, 1), received_before + 2000)


if __name__ == '__main__':
    AssetMultiWalletTest().main()
