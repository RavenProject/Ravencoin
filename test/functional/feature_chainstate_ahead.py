#!/usr/bin/env python3
# Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Recover when the coins and asset databases are ahead of the block index."""

import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, wait_until


class ChainstateAheadTest(RavenTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
        observer, miner = self.nodes

        # The cached chain has 200 blocks. Activate assets and restricted assets
        # before taking the block-index checkpoint.
        miner.generate(232)
        self.sync_all()
        assert_equal(observer.getblockcount(), 432)
        assert_equal(observer.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')
        assert_equal(observer.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        checkpoint_tip = observer.getbestblockhash()

        self.stop_node(0)
        index_dir = os.path.join(observer.datadir, 'regtest', 'blocks', 'index')
        checkpoint_dir = os.path.join(self.options.tmpdir, 'checkpoint_index')
        shutil.copytree(index_dir, checkpoint_dir)
        self.start_node(0)
        connect_nodes_bi(self.nodes, 0, 1)

        # The observer only receives blocks, so its wallet cannot repopulate
        # the asset mempool after the older block index is restored.
        asset_name = 'CHAINSTATE_AHEAD'
        qualifier_name = '#CHAINSTATE_AHEAD'
        tagged_address = miner.getnewaddress()
        miner.issue(asset_name)
        miner.issuequalifierasset(qualifier_name)
        miner.generate(1)
        miner.addtagtoaddress(qualifier_name, tagged_address)
        miner.generate(1)
        self.sync_all()
        assert_equal(observer.getassetdata(asset_name)['name'], asset_name)
        assert_equal(observer.getassetdata(qualifier_name)['name'], qualifier_name)
        assert_equal(observer.checkaddresstag(tagged_address, qualifier_name), True)
        ahead_tip = observer.getbestblockhash()
        assert ahead_tip != checkpoint_tip

        self.stop_node(0)
        self.stop_node(1)
        shutil.rmtree(index_dir)
        shutil.copytree(checkpoint_dir, index_dir)

        debug_log = os.path.join(observer.datadir, 'regtest', 'debug.log')
        log_offset = os.path.getsize(debug_log)
        self.start_node(0)

        # No explicit -reindex or -reindex-chainstate was requested. The node
        # must detect the unknown coins tip and rebuild all consensus state.
        with open(debug_log, encoding='utf-8') as log_file:
            log_file.seek(log_offset)
            recovery_log = log_file.read()
        assert 'rebuilding chainstate' in recovery_log
        # RPC warmup can finish before ThreadImport connects the rebuilt chain.
        wait_until(lambda: observer.getbestblockhash() == checkpoint_tip,
                   err_msg='chainstate rebuild did not reach the checkpoint tip',
                   timeout=60)
        assert_equal(observer.getbestblockhash(), checkpoint_tip)
        assert_equal(observer.getblockcount(), 432)
        assert_equal(observer.getassetdata(asset_name), None)
        assert_equal(observer.getassetdata(qualifier_name), None)
        assert_equal(observer.checkaddresstag(tagged_address, qualifier_name), False)


if __name__ == '__main__':
    ChainstateAheadTest().main()
