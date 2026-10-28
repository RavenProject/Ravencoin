#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A side branch must parse transfers using its own bit-8 parent state."""

import configparser
import os

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework import util as test_util
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes


class TransferSidebranchTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def fixed_locktime_raw(self, node, txid):
        tx = from_hex(CTransaction(), node.gettransaction(txid)['hex'])
        tx.nLockTime = 0
        for txin in tx.vin:
            txin.scriptSig = b''
        signed = node.signrawtransaction(to_hex(tx))
        assert_equal(signed['complete'], True)
        return signed['hex']

    def run_test(self):
        producer, verifier = self.nodes
        producer.generate(860)
        asset_name = 'SIDEBRANCHASSET1234567890123'
        producer.issue(asset_name, 2)
        producer.generate(1)
        self.sync_all()
        assert_equal(verifier.getblockcount(), 861)
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')

        disconnect_nodes(producer, 1)
        disconnect_nodes(verifier, 0)
        producer.generate(2)
        verifier.generate(3)
        assert_equal(producer.getblockcount(), 863)
        assert_equal(producer.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        active_tip = verifier.getbestblockhash()
        original_activation = verifier.getblockhash(863)
        assert_equal(verifier.getblockcount(), 864)

        txid = producer.transfer(asset_name, 1, producer.getnewaddress(), 'ab' * 32, 123456789)[0]
        transfer_raw = self.fixed_locktime_raw(producer, txid)
        decoded = verifier.decoderawtransaction(transfer_raw)
        assert_equal(decoded['locktime'], 0)
        assert any(out['scriptPubKey']['hex'][52:54] == '4c' for out in decoded['vout']
                   if out['scriptPubKey'].get('asset', {}).get('name') == asset_name)
        for txin in decoded['vin']:
            assert verifier.gettxout(txin['txid'], txin['vout']) is not None

        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])

        # Both candidate blocks immediately before activation use the old
        # parser, even though the verifier's current tip has reached ACTIVE.
        for candidate_height in (862, 863):
            candidate_parent = verifier.getblockhash(candidate_height - 1)
            block_time = verifier.getblockheader(candidate_parent)['time'] + 1
            block = create_block(int(candidate_parent, 16),
                                 create_coinbase(candidate_height), block_time)
            block.nVersion = verifier.getblockheader(verifier.getblockhash(candidate_height))['version']
            block.vtx.append(from_hex(CTransaction(), transfer_raw))
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            assert_equal(verifier.submitblock(block.serialize().hex()),
                         'bad-txns-transfer-asset-bad-deserialize')
            assert_equal(verifier.getbestblockhash(), active_tip)

        # The producer's branch has a different height-862 parent history. Its
        # transfer is mined only after bit 8 is active on that branch.
        branch_start = producer.getblockhash(862)
        transfer_block = producer.generate(1)[0]
        assert_equal(producer.getblockcount(), 864)
        assert txid in producer.getblock(transfer_block)['tx']
        producer.generate(2)
        branch_tip = producer.getbestblockhash()
        assert_equal(producer.getblockcount(), 866)

        # Connecting the previously unknown, longer branch must re-evaluate
        # transfer scripts against its own ancestors, not the old active tip.
        self.restart_node(1, ['-assetindex'])
        verifier = self.nodes[1]
        assert_equal(verifier.getbestblockhash(), active_tip)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()
        assert_equal(verifier.getbestblockhash(), branch_tip)
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        assert txid in verifier.getblock(transfer_block)['tx']
        transfer_tx = next(tx for tx in verifier.getblock(transfer_block, 2)['tx']
                           if tx['txid'] == txid)
        message_outputs = [out['scriptPubKey'] for out in transfer_tx['vout']
                           if out['scriptPubKey'].get('asset', {}).get('message') == 'ab' * 32]
        assert_equal(len(message_outputs), 1)
        assert_equal(message_outputs[0]['asset']['name'], asset_name)
        assert_equal(message_outputs[0]['asset']['amount'], 1)
        assert_equal(message_outputs[0]['asset']['expire_time'], 123456789)
        assert_equal(message_outputs[0]['hex'][52:54], '4c')
        assert_equal(verifier.verifychain(4, 6), True)

        # Walk backward over both forks and the activation boundary, restart
        # with the old policy, then reconsider both branches in turn.
        disconnect_nodes(producer, 1)
        disconnect_nodes(verifier, 0)
        verifier.invalidateblock(branch_start)
        assert_equal(verifier.getbestblockhash(), active_tip)
        verifier.invalidateblock(original_activation)
        assert_equal(verifier.getblockcount(), 862)
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')
        rollback_result = verifier.testmempoolaccept([transfer_raw])[0]
        assert_equal(rollback_result['allowed'], False)
        assert_equal(rollback_result['reject-reason'], '16: bad-txns-transfer-asset-bad-deserialize')

        self.restart_node(1, ['-assetindex'])
        verifier = self.nodes[1]
        assert_equal(verifier.getblockcount(), 862)
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')
        verifier.reconsiderblock(original_activation)
        assert_equal(verifier.getbestblockhash(), active_tip)
        verifier.reconsiderblock(branch_start)
        assert_equal(verifier.getbestblockhash(), branch_tip)
        assert_equal(verifier.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        assert txid in verifier.getblock(transfer_block)['tx']
        assert_equal(verifier.verifychain(4, 6), True)


if __name__ == '__main__':
    TransferSidebranchTest().main()
