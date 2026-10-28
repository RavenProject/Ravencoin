#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Asset activation policy must not depend on whether the node saw ACTIVE."""

import configparser
import os

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, from_hex
from test_framework.test_framework import RavenTestFramework
from test_framework import util as test_util
from test_framework.util import Decimal, assert_equal


class AssetActivationReorgTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex', '-persistmempool=0']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(287)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'locked_in')
        active_block = None
        for _ in range(150):
            candidate = node.generate(1)[0]
            if node.getblockchaininfo()['bip9_softforks']['assets']['status'] == 'active':
                active_block = candidate
                break
        assert active_block is not None

        node.listmyassets()  # Exercise the cached deployment query at ACTIVE.
        native_coin = next(coin for coin in node.listunspent()
                           if coin['amount'] > 501 and coin['confirmations'] > 101)
        raw = node.createrawtransaction(
            [{'txid': native_coin['txid'], 'vout': native_coin['vout']}],
            {'n1issueAssetXXXXXXXXXXXXXXXXWdnemQ': 500,
             node.getnewaddress(): native_coin['amount'] - Decimal('500.1'),
             node.getnewaddress(): {'issue': {
                 'asset_name': 'LATCHASSET', 'asset_quantity': 1,
                 'units': 0, 'reissuable': 1, 'has_ipfs': 0}}})
        raw = node.signrawtransaction(raw)['hex']

        active_height = node.getblockcount()
        active_accept = node.testmempoolaccept([raw])[0]
        assert_equal(active_accept['allowed'], True)
        issue_txid = node.sendrawtransaction(raw)
        issue_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), active_height + 1)
        assert issue_txid in node.getblock(issue_block)['tx']
        after_one_txid = node.issue('LATCHPLUSONE', 1)[0]
        after_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), active_height + 2)
        assert after_one_txid in node.getblock(after_block)['tx']
        after_two_txid = node.issue('LATCHPLUSTWO', 1)[0]
        after_two_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), active_height + 3)
        assert after_two_txid in node.getblock(after_two_block)['tx']
        assert_equal(node.verifychain(4, 5), True)

        # Reject the same signed issue in mined candidates at H-2 and H-1
        # while this node's unrelated active tip is already past activation.
        active_tip = node.getbestblockhash()
        locked_tip = node.getblockhash(active_height - 1)
        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])
        for candidate_height in (active_height - 1, active_height):
            parent = node.getblockhash(candidate_height - 1)
            original_block = node.getblockhash(candidate_height)
            block_time = node.getblockheader(parent)['time'] + 1
            invalid_issue = create_block(int(parent, 16),
                                         create_coinbase(candidate_height), block_time)
            invalid_issue.nVersion = node.getblockheader(original_block)['version']
            invalid_issue.vtx.append(from_hex(CTransaction(), raw))
            invalid_issue.hashMerkleRoot = invalid_issue.calc_merkle_root()
            invalid_issue.solve()
            rejected = node.submitblock(invalid_issue.serialize().hex())
            self.log.info('Pre-activation side-branch block %d: %s',
                          candidate_height, rejected)
            assert_equal(rejected, 'inconclusive')
            assert_equal(node.getbestblockhash(), active_tip)
            # Make this candidate the next possible block. It must not
            # replace the parent even after its branch is considered.
            node.invalidateblock(original_block)
            assert_equal(node.getbestblockhash(), parent)
            side_tip = next(tip for tip in node.getchaintips()
                            if tip['hash'] == invalid_issue.hash)
            assert_equal(side_tip['status'], 'invalid')

            # A distinct candidate at the same height now reaches the
            # active-chain rejection path and reports the exact reason.
            direct_issue = create_block(int(parent, 16),
                                        create_coinbase(candidate_height),
                                        block_time + 2)
            direct_issue.nVersion = invalid_issue.nVersion
            direct_issue.vtx.append(from_hex(CTransaction(), raw))
            direct_issue.hashMerkleRoot = direct_issue.calc_merkle_root()
            direct_issue.solve()
            assert_equal(node.submitblock(direct_issue.serialize().hex()),
                         'bad-txns-assets-not-active')
            assert_equal(node.getbestblockhash(), parent)
            node.reconsiderblock(original_block)
            assert_equal(node.getbestblockhash(), active_tip)

        # Connect a longer competing branch with the signed issue at the
        # first active height. The later main-chain issues are absent on this
        # branch, then restored after it is invalidated.
        side_parent = active_block
        side_first = None
        for candidate_height in range(active_height + 1, active_height + 5):
            block = create_block(
                int(side_parent, 16), create_coinbase(candidate_height),
                node.getblockheader(side_parent)['time'] + 2)
            block.nVersion = node.getblockheader(issue_block)['version']
            if candidate_height == active_height + 1:
                block.vtx.append(from_hex(CTransaction(), raw))
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            result = node.submitblock(block.serialize().hex())
            assert_equal(result, None if candidate_height == active_height + 4
                         else 'inconclusive')
            if side_first is None:
                side_first = block.hash
                assert side_first != issue_block
            side_parent = block.hash
        assert_equal(node.getbestblockhash(), side_parent)
        assert issue_txid in node.getblock(side_first)['tx']
        assert_equal(node.getassetdata('LATCHASSET')['amount'], 1)
        assert_equal(node.getassetdata('LATCHPLUSONE'), None)
        assert_equal(node.getassetdata('LATCHPLUSTWO'), None)
        self.stop_node(0)
        self.start_node(0, ['-assetindex', '-persistmempool=0'])
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), side_parent)
        assert_equal(node.getassetdata('LATCHASSET')['amount'], 1)
        node.invalidateblock(side_first)
        assert_equal(node.getbestblockhash(), active_tip)
        assert_equal(node.getassetdata('LATCHPLUSONE')['amount'], 1)
        assert_equal(node.getassetdata('LATCHPLUSTWO')['amount'], 1)

        node.invalidateblock(active_block)
        assert_equal(node.getbestblockhash(), locked_tip)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'locked_in')
        live_result = node.testmempoolaccept([raw])[0]
        assert_equal(live_result['allowed'], False)

        node.invalidateblock(locked_tip)
        before_two = node.testmempoolaccept([raw])[0]
        assert_equal(before_two['allowed'], False)
        node.reconsiderblock(locked_tip)
        # Reconsidering an ancestor also revives its descendants. Keep the
        # ACTIVE branch invalid while comparing the LOCKED_IN tip to restart.
        node.invalidateblock(active_block)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'locked_in')
        assert_equal(node.testmempoolaccept([raw])[0]['allowed'], False)

        self.stop_node(0)
        self.start_node(0, ['-assetindex', '-persistmempool=0'])
        node = self.nodes[0]
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'locked_in')
        restarted_result = node.testmempoolaccept([raw])[0]
        assert_equal(live_result['allowed'], restarted_result['allowed'])

        node.reconsiderblock(active_block)
        assert_equal(node.getbestblockhash(), side_parent)
        assert issue_txid in node.getblock(side_first)['tx']
        node.invalidateblock(side_first)
        assert_equal(node.getbestblockhash(), after_two_block)
        assert issue_txid in node.getblock(issue_block)['tx']
        assert after_one_txid in node.getblock(after_block)['tx']
        assert after_two_txid in node.getblock(after_two_block)['tx']
        assert_equal(node.verifychain(4, 5), True)


if __name__ == '__main__':
    AssetActivationReorgTest().main()
