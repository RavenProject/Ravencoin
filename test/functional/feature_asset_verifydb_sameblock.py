#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Replay an asset issue and dependent transfer in the same block."""

import configparser
import os

from test_framework.test_framework import RavenTestFramework
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, from_hex
from test_framework import util as test_util
from test_framework.util import assert_equal, disconnect_nodes


class AssetVerifyDBSameBlockTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def run_test(self):
        producer, verifier = self.nodes
        producer.generate(432)
        self.sync_all()
        disconnect_nodes(producer, 1)
        disconnect_nodes(verifier, 0)

        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])

        asset_name = 'VERIFYDBCHILD'
        producer.issue(asset_name, 100)
        issue_block = producer.generate(1)[0]
        issue_version = producer.getblockheader(issue_block)['version']
        issue_txid = producer.getblock(issue_block)['tx'][1]
        issue_tx = from_hex(CTransaction(), producer.gettransaction(issue_txid)['hex'])
        transfer_txid = producer.transfer(asset_name, 1, producer.getnewaddress())[0]
        transfer_tx = from_hex(CTransaction(), producer.gettransaction(transfer_txid)['hex'])

        verifier.generate(1)
        assert_equal(verifier.getblockcount(), 433)
        tip = verifier.getbestblockhash()
        block_time = verifier.getblockheader(tip)['time'] + 1
        block = create_block(int(tip, 16), create_coinbase(434), block_time)
        block.nVersion = issue_version
        block.vtx.extend([issue_tx, transfer_tx])
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        block_hash = block.hash
        assert_equal(verifier.submitblock(block.serialize().hex()), None)
        assert_equal(verifier.getblock(block_hash)['tx'][1:], [issue_txid, transfer_txid])
        assert_equal(verifier.getassetdata(asset_name)['amount'], 100)

        assert_equal(verifier.verifychain(4, 1), True)
        self.restart_node(1, ['-assetindex', '-checklevel=4', '-checkblocks=1'])
        assert_equal(self.nodes[1].getbestblockhash(), block_hash)


if __name__ == '__main__':
    AssetVerifyDBSameBlockTest().main()
