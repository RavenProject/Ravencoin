#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bit 12 candidate-parent script rules and the 80,000 sigop boundary."""

import configparser
import os

from test_framework import util as test_util
from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.mininode import COutPoint, CTransaction, CTxIn, CTxOut, from_hex, to_hex
from test_framework.script import CScript, OP_CHECKMULTISIG, OP_CHECKSIG, OP_TRUE
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


PQ_VERSION = 0x30001040
PQ_REGTEST_SEED0_ADDRESS = (
    'rcrt1z4hyhd28n75tkqt6tf6j3jdtalvnuaxd74nepjk5nn7lmz8jh9vasg4ztx8')
PQ_BIP9_ARGS = [
    '-blockversion={}'.format(PQ_VERSION),
    '-vbparams=pq_hybrid:0:999999999999',
    '-persistmempool=0',
]


class PQActivationReorgSigopsTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[
            '-mnemonic=abandon abandon abandon abandon abandon abandon '
            'abandon abandon abandon abandon abandon about',
            '-mnemonicpassphrase=TREZOR',
        ]]

    @staticmethod
    def status(node):
        return node.getblockchaininfo()['bip9_softforks']['pq_hybrid']['status']

    @staticmethod
    def candidate(node, parent, height, tx=None, legacy_sigops=0):
        coinbase = create_coinbase(height)
        if legacy_sigops:
            multi_count, single_count = divmod(legacy_sigops, 20)
            script = CScript([OP_CHECKMULTISIG] * multi_count +
                             [OP_CHECKSIG] * single_count)
            assert_equal(script.get_sig_op_count(False), legacy_sigops)
            coinbase.vout.append(CTxOut(0, script))
            coinbase.rehash()
        block = create_block(int(parent, 16), coinbase,
                             node.getblockheader(parent)['time'] + 1)
        block.nVersion = PQ_VERSION
        if tx is not None:
            block.vtx.append(tx)
            if not tx.wit.is_null():
                add_witness_commitment(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    @staticmethod
    def submit(node, block):
        return node.submitblock(block.serialize(True).hex())

    @staticmethod
    def spend(txid, *vouts):
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(txid, 16), vout), b'', 0xffffffff)
                  for vout in vouts]
        tx.vout = [CTxOut(90_000_000 * len(vouts), CScript([OP_TRUE]))]
        return tx

    def run_test(self):
        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])

        node = self.nodes[0]
        # Obtain the wallet key before switching regtest from its usual
        # immediate PQ mode to real bit-12 versionbits activation.
        pq_address = node.getnewpqaddress()
        assert_equal(pq_address, PQ_REGTEST_SEED0_ADDRESS)
        pq_script = bytes.fromhex(node.validateaddress(pq_address)['scriptPubKey'])
        self.restart_node(0, PQ_BIP9_ARGS)
        node = self.nodes[0]
        assert_equal(self.status(node), 'defined')

        node.generate(99)
        parent = node.getbestblockhash()
        coinbase = create_coinbase(100)
        coinbase.vout[0].nValue -= 400_000_000
        coinbase.vout.extend(CTxOut(100_000_000, pq_script)
                             for _ in range(4))
        coinbase.rehash()
        funding = create_block(int(parent, 16), coinbase,
                               node.getblockheader(parent)['time'] + 1)
        funding.nVersion = PQ_VERSION
        funding.hashMerkleRoot = funding.calc_merkle_root()
        funding.solve()
        assert_equal(self.submit(node, funding), None)
        funding_txid = funding.vtx[0].hash

        node.generate(187)
        assert_equal(node.getblockcount(), 287)
        assert_equal(self.status(node), 'locked_in')
        node.generate(144)
        assert_equal(node.getblockcount(), 431)
        assert_equal(self.status(node), 'active')
        self.log.info('Bit 12 reaches ACTIVE for candidate H432')

        malformed = self.spend(funding_txid, 1)
        active_accept = node.testmempoolaccept([to_hex(malformed)])[0]
        assert_equal(active_accept['allowed'], False)
        assert 'Witness program hash mismatch' in active_accept['reject-reason']
        self.log.info('Malformed active PQ spend: %s', active_accept['reject-reason'])
        active_parent = node.getbestblockhash()
        invalid_432 = self.candidate(node, active_parent, 432, malformed)
        assert_equal(self.submit(node, invalid_432),
                     'block-validation-failed')
        assert_equal(node.getbestblockhash(), active_parent)

        signed = node.signrawtransaction(to_hex(self.spend(funding_txid, 1)))
        assert_equal(signed['complete'], True)
        valid_432 = self.candidate(node, active_parent, 432,
                                   from_hex(CTransaction(), signed['hex']))
        assert_equal(self.submit(node, valid_432), None)
        node.generate(1)
        active_tip = node.getbestblockhash()
        assert_equal(node.getblockcount(), 433)

        # The same malformed spend is valid under the old rule on a branch
        # whose own H430 parent is still LOCKED_IN. This node has already
        # validated an active-chain PQ signature at H432.
        old_parent = node.getblockhash(430)
        old_431 = self.candidate(node, old_parent, 431, malformed)
        assert_equal(self.submit(node, old_431), 'inconclusive')
        node.invalidateblock(active_parent)
        assert_equal(node.getbestblockhash(), old_431.hash)
        assert_equal(self.status(node), 'active')
        assert_equal(node.verifychain(4, 2), True)
        self.log.info('Malformed PQ spend accepted at H431 on old-rule branch')
        node.invalidateblock(old_431.hash)
        assert_equal(node.getbestblockhash(), old_parent)
        assert_equal(self.status(node), 'locked_in')
        node.reconsiderblock(active_parent)
        assert_equal(node.getbestblockhash(), active_tip)

        # CheckBlock sees 79,996 legacy sigops. Three active PQ inputs
        # add six and cross the 80,000 limit. The prior one-unit rule
        # admits this block with 79,999 and is the red control.
        sigop_parent = node.getbestblockhash()
        signed_three = node.signrawtransaction(
            to_hex(self.spend(funding_txid, 2, 3, 4)))
        assert_equal(signed_three['complete'], True)
        excess = self.candidate(node, sigop_parent, 434,
                                from_hex(CTransaction(), signed_three['hex']),
                                legacy_sigops=19_999)
        assert_equal(self.submit(node, excess), 'bad-blk-sigops')
        assert_equal(node.getbestblockhash(), sigop_parent)
        self.log.info('80,002 sigops rejected with three PQ inputs')

        signed_two = node.signrawtransaction(
            to_hex(self.spend(funding_txid, 2, 3)))
        assert_equal(signed_two['complete'], True)
        at_limit = self.candidate(node, sigop_parent, 434,
                                  from_hex(CTransaction(), signed_two['hex']),
                                  legacy_sigops=19_999)
        assert_equal(self.submit(node, at_limit), None)
        assert_equal(node.getbestblockhash(), at_limit.hash)
        self.log.info('80,000 sigops accepted with two PQ inputs')

        legacy_control = self.candidate(node, at_limit.hash, 435,
                                        legacy_sigops=20_000)
        assert_equal(self.submit(node, legacy_control), None)
        self.log.info('80,000 legacy sigops accepted without PQ inputs')


if __name__ == '__main__':
    PQActivationReorgSigopsTest().main()
