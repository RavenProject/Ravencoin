#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Select an anchored authority outpoint when other protected copies exist."""

from decimal import Decimal

from test_framework.mininode import CTransaction, from_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


ASSET_OPCODE = 0xc0


class PQAssetAuthoritySelectionTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex', '-vbparams=pq_assets:0:999999999999']]

    @staticmethod
    def program(node, address):
        script = bytes.fromhex(node.validateaddress(address)['scriptPubKey'])
        assert_equal(script[:2], b'\x52\x20')
        return script[2:]

    @staticmethod
    def asset_outpoint(transaction, txid, program):
        matches = [index for index, output in enumerate(transaction.vout)
                   if len(output.scriptPubKey) > 57 and
                   output.scriptPubKey[25] == ASSET_OPCODE and
                   output.scriptPubKey[-32:] == program]
        assert_equal(len(matches), 1)
        return (txid, matches[0])

    def run_test(self):
        node = self.nodes[0]
        node.generate(863)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['pq_assets']['status'],
                     'active')
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'],
                     'active')

        descriptor_a = node.getnewpqassetaddress()
        descriptor_b = node.getnewpqassetaddress()
        _, native_a = descriptor_a.split('|')
        _, native_b = descriptor_b.split('|')
        program_a = self.program(node, native_a)
        program_b = self.program(node, native_b)
        script_a = bytes.fromhex(node.validateaddress(native_a)['scriptPubKey'])
        qualifier = '#PQAUTHSELECT'

        node.issuequalifierasset(qualifier, 2, descriptor_a)
        node.generate(1)
        node.sendtoaddress(native_a, Decimal('1'))
        node.generate(1)

        # The transfer writes B before the change back to A. Both authority
        # outpoints are spendable, but only A has a funded native PQ anchor.
        split_txid = node.transferfromaddress(
            qualifier, descriptor_a, 1, descriptor_b, '', 0, '', descriptor_a)[0]
        split = from_hex(CTransaction(), node.getrawtransaction(split_txid))
        outpoint_a = self.asset_outpoint(split, split_txid, program_a)
        outpoint_b = self.asset_outpoint(split, split_txid, program_b)
        assert outpoint_b[1] < outpoint_a[1]
        node.generate(1)
        assert_equal(node.listmyassets(qualifier, True)[qualifier]['balance'], 2)

        anchors_a = {(coin['txid'], coin['vout']) for coin in node.listunspent()
                     if coin['scriptPubKey'] == script_a.hex()}
        assert anchors_a
        first_target = node.getnewaddress()
        tag_txid = node.addtagtoaddress(qualifier, first_target)[0]
        tag = from_hex(CTransaction(), node.getrawtransaction(tag_txid))
        spent = {(format(txin.prevout.hash, '064x'), txin.prevout.n)
                 for txin in tag.vin}
        assert outpoint_a in spent
        assert outpoint_b not in spent
        assert spent & anchors_a
        returned = self.asset_outpoint(tag, tag_txid, program_a)
        assert returned[0] == tag_txid
        node.generate(1)
        assert_equal(node.listtagsforaddress(first_target), [qualifier])

        # With A's refreshed native anchor unavailable, B's unanchored
        # authority must not be chosen randomly or returned as a fallback.
        refreshed_a = [{'txid': coin['txid'], 'vout': coin['vout']}
                       for coin in node.listunspent()
                       if coin['scriptPubKey'] == script_a.hex()]
        assert refreshed_a
        assert_equal(node.lockunspent(False, refreshed_a), True)
        second_target = node.getnewaddress()
        assert_raises_rpc_error(-4, 'funded matching PQ anchor',
                                node.addtagtoaddress, qualifier, second_target)
        assert_equal(node.lockunspent(True, refreshed_a), True)

        # An explicit return to B is allowed, while the authority input and
        # its signature still come from anchored A.
        explicit_txid = node.addtagtoaddress(
            qualifier, second_target, descriptor_b)[0]
        explicit = from_hex(CTransaction(), node.getrawtransaction(explicit_txid))
        explicit_spent = {(format(txin.prevout.hash, '064x'), txin.prevout.n)
                          for txin in explicit.vin}
        assert returned in explicit_spent
        self.asset_outpoint(explicit, explicit_txid, program_b)


if __name__ == '__main__':
    PQAssetAuthoritySelectionTest().main()
