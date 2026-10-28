#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Restricted-asset blocks must use the candidate parent's bit-7 state."""

import configparser
import os

from test_framework import util as test_util
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, from_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import Decimal, assert_equal, assert_raises_rpc_error


ASSETS_ONLY_VERSION = 0x30000040
ASSETS_AND_RIP5_VERSION = 0x300000c0


class Rip5ActivationReorgTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex', '-persistmempool=0',
                            '-blockversion={}'.format(ASSETS_ONLY_VERSION)]]

    def rip5_status(self, node):
        return node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status']

    def issue_raw(self, node, asset_name):
        native_coin = next(coin for coin in node.listunspent()
                           if coin['amount'] > 1501 and coin['confirmations'] > 101)
        owner_name = asset_name[1:] + '!'
        owner_coin = node.listmyassets(owner_name, True)[owner_name]['outpoints'][0]
        raw = node.createrawtransaction(
            [{'txid': native_coin['txid'], 'vout': native_coin['vout']},
             {'txid': owner_coin['txid'], 'vout': owner_coin['vout']}],
            {'n1issueRestrictedXXXXXXXXXXXXZVT9V': 1500,
             node.getnewaddress(): native_coin['amount'] - Decimal('1500.1'),
             node.getnewaddress(): {'issue_restricted': {
                 'asset_name': asset_name, 'asset_quantity': 1,
                 'verifier_string': 'true', 'units': 0,
                 'reissuable': 1, 'has_ipfs': 0}}})
        signed = node.signrawtransaction(raw)
        assert_equal(signed['complete'], True)
        return signed['hex']

    def run_test(self):
        node = self.nodes[0]
        activation_height = 576
        node.generate(287)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'locked_in')
        assert_equal(self.rip5_status(node), 'started')
        # Signal bit 6 alone in the first STARTED period. Signal bit 7 in
        # the next one so assets are active before restricted assets.
        self.restart_node(0, ['-assetindex', '-persistmempool=0',
                              '-blockversion={}'.format(ASSETS_AND_RIP5_VERSION)])
        node = self.nodes[0]
        node.generate(activation_height - 4 - node.getblockcount())
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')
        assert_equal(self.rip5_status(node), 'locked_in')
        base_names = ('RIP5BOUNDARY', 'RIP5PLUSONE', 'RIP5PLUSTWO')
        base_name = base_names[0]
        restricted_name = '$' + base_name
        base_txids = [node.issue(name, 1)[0] for name in base_names]
        base_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), activation_height - 3)
        assert all(txid in node.getblock(base_block)['tx'] for txid in base_txids)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')
        assert_equal(self.rip5_status(node), 'locked_in')

        original_h_minus_two = node.generate(1)[0]
        original_h_minus_one = node.generate(1)[0]
        node.generate(1)
        assert_equal(node.getblockcount(), activation_height)
        assert_equal(self.rip5_status(node), 'active')
        assert_equal(node.listglobalrestrictions(), [])
        raw = self.issue_raw(node, restricted_name)
        txid = node.decoderawtransaction(raw)['txid']
        assert_equal(node.testmempoolaccept([raw])[0]['allowed'], True)

        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])

        # Submit pre-active siblings while the unrelated active tip is at H.
        # They are stored as side branches, then fail when the main child is
        # invalidated and each sibling is considered for connection.
        active_tip = node.getbestblockhash()
        for candidate_height, parent, original in (
                (activation_height - 1, original_h_minus_two,
                 original_h_minus_one),
                (activation_height - 2, base_block, original_h_minus_two)):
            block = create_block(
                int(parent, 16), create_coinbase(candidate_height),
                node.getblockheader(parent)['time'] + 2)
            block.nVersion = node.getblockheader(original)['version']
            block.vtx.append(from_hex(CTransaction(), raw))
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            assert_equal(node.submitblock(block.serialize().hex()), 'inconclusive')
            assert_equal(node.getbestblockhash(), active_tip)
            node.invalidateblock(original)
            assert_equal(node.getbestblockhash(), parent)
            side_tip = next(tip for tip in node.getchaintips()
                            if tip['hash'] == block.hash)
            assert_equal(side_tip['status'], 'invalid')
            node.reconsiderblock(original)
            assert_equal(node.getbestblockhash(), active_tip)

        # Rewind to H-3. H-2 and H-1 candidates must reject the same
        # transaction even though this process has already reached ACTIVE.
        node.invalidateblock(original_h_minus_two)
        assert_equal(node.getbestblockhash(), base_block)
        assert_equal(self.rip5_status(node), 'locked_in')
        assert_raises_rpc_error(-1, 'Restricted assets must be active',
                                node.listglobalrestrictions)
        early_result = node.testmempoolaccept([raw])[0]
        assert_equal(early_result['allowed'], False)
        assert_equal(early_result['reject-reason'],
                     '16: bad-tx-null-asset-data-before-restricted-assets-activated')

        for candidate_height, original in (
                (activation_height - 2, original_h_minus_two),
                (activation_height - 1, original_h_minus_one)):
            parent = node.getbestblockhash()
            block = create_block(
                int(parent, 16), create_coinbase(candidate_height),
                node.getblockheader(parent)['time'] + 1)
            block.nVersion = node.getblockheader(original)['version']
            block.vtx.append(from_hex(CTransaction(), raw))
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            assert_equal(node.submitblock(block.serialize().hex()),
                         'bad-tx-null-asset-data-before-restricted-assets-activated')
            assert_equal(node.getbestblockhash(), parent)
            node.generate(1)
            assert_equal(node.getblockcount(), candidate_height)

        # Mempool admission at H-1 targets H, the first active block.
        pre_activation = node.getbestblockhash()
        assert_equal(node.getblockcount(), activation_height - 1)
        assert_equal(self.rip5_status(node), 'active')
        assert_equal(node.testmempoolaccept([raw])[0]['allowed'], True)
        assert_equal(node.sendrawtransaction(raw), txid)
        activation_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), activation_height)
        assert txid in node.getblock(activation_block)['tx']
        assert_equal(node.getassetdata(restricted_name)['amount'], 1)
        plus_one_name = '$' + base_names[1]
        plus_one_raw = self.issue_raw(node, plus_one_name)
        plus_one_txid = node.sendrawtransaction(plus_one_raw)
        plus_one_block = node.generate(1)[0]
        assert plus_one_txid in node.getblock(plus_one_block)['tx']
        plus_two_name = '$' + base_names[2]
        plus_two_raw = self.issue_raw(node, plus_two_name)
        plus_two_txid = node.sendrawtransaction(plus_two_raw)
        post_activation = node.generate(1)[0]
        assert plus_two_txid in node.getblock(post_activation)['tx']
        assert_equal(node.getblockcount(), activation_height + 2)
        assert_equal(self.rip5_status(node), 'active')
        assert_equal(node.verifychain(4, 4), True)

        # A longer competing branch may issue the same asset at the first
        # active height. Connect it, restart on it, and restore the main
        # branch before the old-rule rollback below.
        main_tip = post_activation
        side_parent = pre_activation
        side_first = None
        for candidate_height in range(activation_height, activation_height + 4):
            block = create_block(
                int(side_parent, 16), create_coinbase(candidate_height),
                node.getblockheader(side_parent)['time'] + 2)
            block.nVersion = ASSETS_AND_RIP5_VERSION
            if candidate_height == activation_height:
                block.vtx.append(from_hex(CTransaction(), raw))
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            result = node.submitblock(block.serialize().hex())
            assert_equal(result, None if candidate_height == activation_height + 3
                         else 'inconclusive')
            if side_first is None:
                side_first = block.hash
                assert side_first != activation_block
            side_parent = block.hash
        assert_equal(node.getbestblockhash(), side_parent)
        assert txid in node.getblock(side_first)['tx']
        assert_equal(node.getassetdata(restricted_name)['amount'], 1)
        assert_equal(node.getassetdata(plus_one_name), None)
        assert_equal(node.getassetdata(plus_two_name), None)
        self.restart_node(0, ['-assetindex', '-persistmempool=0',
                              '-blockversion={}'.format(ASSETS_AND_RIP5_VERSION)])
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), side_parent)
        assert_equal(node.getassetdata(restricted_name)['amount'], 1)
        node.invalidateblock(side_first)
        assert_equal(node.getbestblockhash(), main_tip)
        assert_equal(node.getassetdata(plus_one_name)['amount'], 1)
        assert_equal(node.getassetdata(plus_two_name)['amount'], 1)

        # Disconnect both sides of the boundary and restart while the old
        # rules apply. Reconsidering the saved branch must restore the asset.
        node.invalidateblock(pre_activation)
        assert_equal(node.getblockcount(), activation_height - 2)
        assert_equal(node.getassetdata(restricted_name), None)
        assert_equal(node.getassetdata(plus_one_name), None)
        assert_equal(node.getassetdata(plus_two_name), None)
        assert_equal(self.rip5_status(node), 'locked_in')
        assert_equal(node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')
        assert_raises_rpc_error(-1, 'Restricted assets must be active',
                                node.listglobalrestrictions)
        assert_equal(node.testmempoolaccept([raw])[0]['reject-reason'],
                     early_result['reject-reason'])
        self.restart_node(0, ['-assetindex', '-persistmempool=0',
                              '-blockversion={}'.format(ASSETS_AND_RIP5_VERSION)])
        node = self.nodes[0]
        assert_equal(node.getblockcount(), activation_height - 2)
        assert_equal(self.rip5_status(node), 'locked_in')
        assert_raises_rpc_error(-1, 'Restricted assets must be active',
                                node.listglobalrestrictions)
        assert_equal(node.testmempoolaccept([raw])[0]['reject-reason'],
                     early_result['reject-reason'])
        node.reconsiderblock(pre_activation)
        assert_equal(node.getbestblockhash(), side_parent)
        assert txid in node.getblock(side_first)['tx']
        node.invalidateblock(side_first)
        assert_equal(node.getbestblockhash(), post_activation)
        assert_equal(node.getblockcount(), activation_height + 2)
        assert txid in node.getblock(activation_block)['tx']
        assert plus_one_txid in node.getblock(plus_one_block)['tx']
        assert plus_two_txid in node.getblock(post_activation)['tx']
        assert_equal(node.getassetdata(restricted_name)['amount'], 1)
        assert_equal(node.getassetdata(plus_one_name)['amount'], 1)
        assert_equal(node.getassetdata(plus_two_name)['amount'], 1)
        assert_equal(node.listglobalrestrictions(), [])
        assert_equal(node.verifychain(4, 4), True)


if __name__ == '__main__':
    Rip5ActivationReorgTest().main()
