#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Recover asset transfer balances after an interrupted coins flush."""

import http.client
import os

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, connect_nodes, wait_until


class AssetTransferCrashTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def run_test(self):
        source_node, reference_node = self.nodes
        source_node.generate(432)
        self.sync_all()
        assert_equal(source_node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')

        source_address = source_node.getnewaddress()
        destination_address = reference_node.getnewaddress()
        asset_name = 'DBCRASHTRANSFER'
        amount = 37
        source_node.issue(asset_name, amount, source_address)
        source_node.generate(1)
        self.sync_all()

        # Make the post-issuance state durable before arming the transfer fault.
        source_node.gettxoutsetinfo()
        self.restart_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcrashratio=1', '-dbcache=1000'])
        source_node = self.nodes[0]
        connect_nodes(source_node, 1)

        source_node.transfer(asset_name, amount, destination_address)
        transfer_block = source_node.generate(1)[0]
        self.sync_all()
        expected_utxo = reference_node.gettxoutsetinfo()
        expected_balances = reference_node.listaddressesbyasset(asset_name)
        expected_asset_data = reference_node.getassetdata(asset_name)
        assert_equal(expected_balances.get(source_address, 0), 0)
        assert_equal(expected_balances[destination_address], amount)
        assert_equal(sum(expected_balances.values()), amount)
        assert_equal(expected_asset_data['amount'], amount)

        try:
            source_node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The transfer partial coins-batch fault hook did not crash')
        source_node.wait_until_stopped()

        self.start_node(0, ['-assetindex', '-dbcache=1000'])
        source_node = self.nodes[0]
        with open(os.path.join(source_node.datadir, 'regtest', 'debug.log'), encoding='utf-8') as log:
            debug_log = log.read()
        assert 'Simulating a crash. Goodbye.' in debug_log
        assert 'Asset database state is interrupted or not certified' in debug_log
        assert 'Replaying blocks' not in debug_log
        wait_until(lambda: source_node.getbestblockhash() == transfer_block,
                   err_msg='asset transfer recovery did not reach the expected tip',
                   timeout=60)
        assert_equal(source_node.getbestblockhash(), transfer_block)
        recovered_utxo = source_node.gettxoutsetinfo()
        recovered_balances = source_node.listaddressesbyasset(asset_name)
        assert_equal(recovered_utxo['hash_serialized_2'], expected_utxo['hash_serialized_2'])
        assert_equal(recovered_utxo['txouts'], expected_utxo['txouts'])
        assert_equal(recovered_balances, expected_balances)
        assert_equal(source_node.listassetbalancesbyaddress(source_address).get(asset_name, 0), 0)
        assert_equal(source_node.listassetbalancesbyaddress(destination_address)[asset_name], amount)
        assert_equal(sum(recovered_balances.values()), source_node.getassetdata(asset_name)['amount'])
        assert_equal(source_node.getassetdata(asset_name), expected_asset_data)


if __name__ == '__main__':
    AssetTransferCrashTest().main()
