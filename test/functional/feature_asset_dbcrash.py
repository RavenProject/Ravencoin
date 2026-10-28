#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Recover asset metadata as well as UTXOs after an interrupted coins flush."""

import http.client
import os

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, wait_until


class AssetReplayCrashTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        def wait_for_recovered_tip(expected):
            # RPC can respond before background import connects the final tip.
            wait_until(lambda: self.nodes[0].getbestblockhash() == expected,
                       err_msg='asset replay did not reach the expected tip',
                       timeout=60)

        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')

        # Persist the parent before enabling the deterministic crash hook.
        node.gettxoutsetinfo()
        self.restart_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcrashratio=1', '-dbcache=1000'])
        node = self.nodes[0]

        asset_name = 'REPLAYCRASHASSET'
        node.issue(asset_name, 37)
        asset_block = node.generate(1)[0]
        asset_data = node.getassetdata(asset_name)
        assert_equal(asset_data['name'], asset_name)
        assert_equal(asset_data['amount'], 37)

        # The first partial coins batch writes a replay marker and exits
        # before the asset database flush. This must not become a UTXO-only
        # recovery on the next startup.
        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The deterministic coins flush did not crash')
        node.wait_until_stopped()

        self.start_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcache=1000'])
        node = self.nodes[0]
        wait_for_recovered_tip(asset_block)
        assert_equal(node.getbestblockhash(), asset_block)
        with open(os.path.join(node.datadir, 'regtest', 'debug.log'), encoding='utf-8') as log:
            debug_log = log.read()
        assert 'Simulating a crash. Goodbye.' in debug_log
        assert 'Asset database state is interrupted or not certified' in debug_log
        assert 'Replaying blocks' not in debug_log
        assert_equal(node.getassetdata(asset_name), asset_data)

        # A second abrupt exit must not erase an issuance already recovered
        # by replay. The first restart must leave durable asset state.
        node.process.kill()
        node.process.wait(timeout=10)
        self.start_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcache=1000'])
        node = self.nodes[0]
        wait_for_recovered_tip(asset_block)
        assert_equal(node.getbestblockhash(), asset_block)
        assert_equal(node.getassetdata(asset_name), asset_data)

        # Reissue metadata changes also need a safe replay path. In
        # particular, units changes create asset undo data during AddCoins.
        self.restart_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcrashratio=1', '-dbcache=1000'])
        node = self.nodes[0]
        node.reissue(asset_name, 5, node.getnewaddress(), '', True, 1)
        reissue_block = node.generate(1)[0]
        reissue_data = node.getassetdata(asset_name)
        assert_equal(reissue_data['amount'], 42)
        assert_equal(reissue_data['units'], 1)
        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The reissue coins flush did not crash')
        node.wait_until_stopped()
        self.start_node(0, ['-assetindex', '-dbbatchsize=1', '-dbcache=1000'])
        node = self.nodes[0]
        wait_for_recovered_tip(reissue_block)
        assert_equal(node.getbestblockhash(), reissue_block)
        assert_equal(node.getassetdata(asset_name), reissue_data)

        # A complete coins batch must not certify a tip before its asset
        # metadata is durable. Crash at that exact regtest commit boundary.
        crash_height = node.getblockcount() + 1
        self.restart_node(0, ['-assetindex', '-dbcache=1000',
                              '-dbcrashaftercoinsflushheight={}'.format(crash_height)])
        node = self.nodes[0]
        later_asset_name = 'POSTCOINCRASHASSET'
        node.issue(later_asset_name, 11)
        later_block = node.generate(1)[0]
        later_asset_data = node.getassetdata(later_asset_name)
        assert_equal(later_asset_data['amount'], 11)
        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The post-coins-flush fault hook did not crash')
        node.wait_until_stopped()

        self.start_node(0, ['-assetindex', '-dbcache=1000'])
        node = self.nodes[0]
        wait_for_recovered_tip(later_block)
        assert_equal(node.getbestblockhash(), later_block)
        assert_equal(node.getassetdata(later_asset_name), later_asset_data)


if __name__ == '__main__':
    AssetReplayCrashTest().main()
