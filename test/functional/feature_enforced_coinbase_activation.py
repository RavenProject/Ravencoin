#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mined asset-value and coinbase checks must use the candidate parent."""

import configparser
import os

from test_framework import util as test_util
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, CTxOut, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import Decimal, assert_equal, hex_str_to_bytes


ASSETS_VERSION = 0x30000040
ASSETS_VALUES_VERSION = 0x30000240
ASSETS_VALUES_COINBASE_VERSION = 0x30000640


class EnforcedCoinbaseActivationTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[
            '-assetindex', '-persistmempool=0',
            '-blockversion={}'.format(ASSETS_VERSION),
        ]]

    def restart_with_version(self, version):
        self.restart_node(0, [
            '-assetindex', '-persistmempool=0',
            '-blockversion={}'.format(version),
        ])
        return self.nodes[0]

    @staticmethod
    def deployment_status(node, name):
        return node.getblockchaininfo()['bip9_softforks'][name]['status']

    @staticmethod
    def candidate(node, parent, height, version, time_offset=1,
                  transaction=None, coinbase_asset_script=None):
        coinbase = create_coinbase(height)
        if coinbase_asset_script is not None:
            coinbase.vout.append(CTxOut(0, hex_str_to_bytes(coinbase_asset_script)))
            coinbase.rehash()
        block_time = node.getblockheader(parent)['time'] + time_offset
        block = create_block(int(parent, 16), coinbase, block_time)
        block.nVersion = version
        if transaction is not None:
            block.vtx.append(from_hex(CTransaction(), transaction))
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    @staticmethod
    def submit(node, block):
        return node.submitblock(block.serialize().hex())

    @staticmethod
    def assert_invalid_tip(node, block):
        tip = next(tip for tip in node.getchaintips()
                   if tip['hash'] == block.hash)
        assert_equal(tip['status'], 'invalid')

    @staticmethod
    def nonzero_reissue(node, asset_name):
        native_coin = next(coin for coin in node.listunspent()
                           if coin['amount'] > Decimal('101')
                           and coin['confirmations'] > 101)
        owner_name = asset_name + '!'
        owner_coin = node.listmyassets(owner_name, True)[owner_name]['outpoints'][0]
        raw = node.createrawtransaction(
            [{'txid': native_coin['txid'], 'vout': native_coin['vout']},
             {'txid': owner_coin['txid'], 'vout': owner_coin['vout']}],
            {'n1ReissueAssetXXXXXXXXXXXXXXWG9NLd': 100,
             node.getnewaddress(): native_coin['amount'] - Decimal('100.1'),
             node.getnewaddress(): {'reissue': {
                 'asset_name': asset_name, 'asset_quantity': 1}}})
        tx = from_hex(CTransaction(), raw)
        assert b'rvnr' in tx.vout[-1].scriptPubKey
        tx.vout[-1].nValue = 1
        signed = node.signrawtransaction(to_hex(tx))
        assert_equal(signed['complete'], True)
        return signed['hex']

    def run_test(self):
        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])

        node = self.nodes[0]
        node.generate(431)
        assert_equal(self.deployment_status(node, 'assets'), 'active')
        assert_equal(self.deployment_status(node, 'enforce'), 'started')

        # Bit 9 must lag bit 6: its normal LOCKED_IN boundary at H288
        # precedes asset activation, so no reissue can exercise that rule.
        node = self.restart_with_version(ASSETS_VALUES_VERSION)
        node.generate(141)
        assert_equal(node.getblockcount(), 572)
        asset_name = 'F106VALUE'
        issue_txid = node.issue(asset_name, 1)[0]
        issue_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), 573)
        assert issue_txid in node.getblock(issue_block)['tx']
        assert_equal(self.deployment_status(node, 'enforce'), 'started')
        assert_equal(node.getassetdata(asset_name)['amount'], 1)
        reissue_raw = self.nonzero_reissue(node, asset_name)
        reissue_txid = node.decoderawtransaction(reissue_raw)['txid']

        # H574 and H575 still use STARTED; H576 is the first LOCKED_IN
        # candidate. The value rule intentionally starts at LOCKED_IN.
        original_574 = node.generate(1)[0]
        assert_equal(node.getblockcount(), 574)
        original_575 = node.generate(1)[0]
        assert_equal(node.getblockcount(), 575)
        assert_equal(self.deployment_status(node, 'enforce'), 'locked_in')
        node = self.restart_with_version(ASSETS_VALUES_COINBASE_VERSION)
        node.generate(145)
        assert_equal(node.getblockcount(), 720)
        assert_equal(self.deployment_status(node, 'enforce'), 'active')
        active_value_tip = node.getbestblockhash()

        # Even while the active tip is far beyond H576, this candidate must
        # use its own H575 parent and fail the contextual transaction check.
        invalid_576 = self.candidate(
            node, original_575, 576, ASSETS_VALUES_COINBASE_VERSION,
            transaction=reissue_raw)
        assert_equal(self.submit(node, invalid_576),
                     "bad-txns-asset-reissued-amount-isn't-zero")
        original_576 = node.getblockhash(576)
        node.invalidateblock(original_576)
        assert_equal(node.getbestblockhash(), original_575)
        self.assert_invalid_tip(node, invalid_576)
        direct_invalid_576 = self.candidate(
            node, original_575, 576, ASSETS_VALUES_COINBASE_VERSION,
            time_offset=3, transaction=reissue_raw)
        assert_equal(self.submit(node, direct_invalid_576),
                     "bad-txns-asset-reissued-amount-isn't-zero")
        node.reconsiderblock(original_576)
        assert_equal(node.getbestblockhash(), active_value_tip)

        for height in (577, 578):
            invalid = self.candidate(
                node, node.getblockhash(height - 1), height,
                ASSETS_VALUES_COINBASE_VERSION, time_offset=2,
                transaction=reissue_raw)
            assert_equal(self.submit(node, invalid),
                         "bad-txns-asset-reissued-amount-isn't-zero")

        valid_575 = self.candidate(
            node, original_574, 575, ASSETS_VALUES_VERSION,
            time_offset=2, transaction=reissue_raw)
        assert_equal(self.submit(node, valid_575), 'inconclusive')
        valid_574 = self.candidate(
            node, issue_block, 574, ASSETS_VALUES_VERSION,
            time_offset=2, transaction=reissue_raw)
        assert_equal(self.submit(node, valid_574), 'inconclusive')
        node.invalidateblock(original_575)
        assert_equal(node.getbestblockhash(), valid_575.hash)
        assert reissue_txid in node.getblock(valid_575.hash)['tx']
        assert_equal(node.getassetdata(asset_name)['amount'], 2)

        node.invalidateblock(original_574)
        assert_equal(node.getbestblockhash(), valid_574.hash)
        assert reissue_txid in node.getblock(valid_574.hash)['tx']
        assert_equal(node.getassetdata(asset_name)['amount'], 2)

        # Rewind across LOCKED_IN after this process saw ACTIVE. Restart at
        # the old-rule parent, then restore the long branch.
        node.invalidateblock(valid_574.hash)
        assert_equal(node.getbestblockhash(), issue_block)
        assert_equal(self.deployment_status(node, 'enforce'), 'started')
        assert_equal(node.getassetdata(asset_name)['amount'], 1)
        node = self.restart_with_version(ASSETS_VALUES_COINBASE_VERSION)
        assert_equal(node.getbestblockhash(), issue_block)
        assert_equal(self.deployment_status(node, 'enforce'), 'started')
        node.reconsiderblock(original_574)
        node.reconsiderblock(original_575)
        assert_equal(node.getbestblockhash(), active_value_tip)
        assert_equal(self.deployment_status(node, 'enforce'), 'active')

        # Signaling bit 10 from H576 through H999 exceeds its 400/500
        # threshold. Its first ACTIVE candidate is H1500.
        node.generate(776)
        assert_equal(node.getblockcount(), 1496)
        coinbase_asset = 'F106COINBASE'
        coinbase_issue = node.issue(coinbase_asset, 1)[0]
        issue_block_1497 = node.generate(1)[0]
        assert coinbase_issue in node.getblock(issue_block_1497)['tx']
        transfer_txid = node.transfer(coinbase_asset, 1, node.getnewaddress())[0]
        transfer_block = node.generate(1)[0]
        assert_equal(node.getblockcount(), 1498)
        assert transfer_txid in node.getblock(transfer_block)['tx']
        assert_equal(self.deployment_status(node, 'coinbase'), 'locked_in')
        transfer = node.decoderawtransaction(node.gettransaction(transfer_txid)['hex'])
        transfer_outputs = [out for out in transfer['vout']
                            if out['scriptPubKey'].get('asset', {}).get('name') == coinbase_asset]
        assert_equal(len(transfer_outputs), 1)
        asset_script = transfer_outputs[0]['scriptPubKey']['hex']

        original_1499 = node.generate(1)[0]
        assert_equal(node.getblockcount(), 1499)
        assert_equal(self.deployment_status(node, 'coinbase'), 'active')
        node.generate(3)
        assert_equal(node.getblockcount(), 1502)
        active_coinbase_tip = node.getbestblockhash()

        invalid_1500 = self.candidate(
            node, original_1499, 1500, ASSETS_VALUES_COINBASE_VERSION,
            coinbase_asset_script=asset_script)
        assert_equal(self.submit(node, invalid_1500),
                     'bad-txns-coinbase-contains-asset-txes')
        original_1500 = node.getblockhash(1500)
        node.invalidateblock(original_1500)
        assert_equal(node.getbestblockhash(), original_1499)
        self.assert_invalid_tip(node, invalid_1500)
        direct_invalid_1500 = self.candidate(
            node, original_1499, 1500, ASSETS_VALUES_COINBASE_VERSION,
            time_offset=3, coinbase_asset_script=asset_script)
        assert_equal(self.submit(node, direct_invalid_1500),
                     'bad-txns-coinbase-contains-asset-txes')
        node.reconsiderblock(original_1500)
        assert_equal(node.getbestblockhash(), active_coinbase_tip)

        for height in (1501, 1502):
            invalid = self.candidate(
                node, node.getblockhash(height - 1), height,
                ASSETS_VALUES_COINBASE_VERSION, time_offset=2,
                coinbase_asset_script=asset_script)
            assert_equal(self.submit(node, invalid),
                         'bad-txns-coinbase-contains-asset-txes')

        valid_1499 = self.candidate(
            node, transfer_block, 1499, ASSETS_VALUES_COINBASE_VERSION,
            time_offset=2, coinbase_asset_script=asset_script)
        assert_equal(self.submit(node, valid_1499), 'inconclusive')
        valid_1498 = self.candidate(
            node, issue_block_1497, 1498, ASSETS_VALUES_COINBASE_VERSION,
            time_offset=2, coinbase_asset_script=asset_script)
        assert_equal(self.submit(node, valid_1498), 'inconclusive')
        node.invalidateblock(original_1499)
        assert_equal(node.getbestblockhash(), valid_1499.hash)
        assert_equal(node.getblock(valid_1499.hash)['tx'][0], valid_1499.vtx[0].hash)

        node.invalidateblock(transfer_block)
        assert_equal(node.getbestblockhash(), valid_1498.hash)
        assert_equal(node.getblock(valid_1498.hash)['tx'][0], valid_1498.vtx[0].hash)

        node.invalidateblock(valid_1498.hash)
        assert_equal(node.getbestblockhash(), issue_block_1497)
        assert_equal(self.deployment_status(node, 'coinbase'), 'locked_in')
        node = self.restart_with_version(ASSETS_VALUES_COINBASE_VERSION)
        assert_equal(node.getbestblockhash(), issue_block_1497)
        assert_equal(self.deployment_status(node, 'coinbase'), 'locked_in')
        node.reconsiderblock(transfer_block)
        node.reconsiderblock(original_1499)
        assert_equal(node.getbestblockhash(), active_coinbase_tip)
        assert_equal(self.deployment_status(node, 'coinbase'), 'active')
        assert_equal(node.verifychain(4, 4), True)


if __name__ == '__main__':
    EnforcedCoinbaseActivationTest().main()
