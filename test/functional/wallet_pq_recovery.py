#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Recover a funded deterministic PQ key from mnemonic words alone."""

from decimal import Decimal
import os

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


MNEMONIC = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about'


class PQRecoveryTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-bip44=1', '-mnemonic=' + MNEMONIC]]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        source_addresses = [node.getnewpqaddress() for _ in range(4)]
        funded_address = source_addresses[2]
        funding_txid = node.sendtoaddress(funded_address, Decimal('1'))
        node.generate(1)
        tip = node.getblockcount()

        self.stop_node(0)
        os.remove(os.path.join(node.datadir, 'regtest', 'wallet.dat'))
        self.start_node(0, extra_args=['-bip44=1', '-mnemonic=' + MNEMONIC])

        assert_equal(node.validateaddress(funded_address)['ismine'], False)
        assert_equal([coin for coin in node.listunspent()
                      if coin['txid'] == funding_txid and
                      coin['address'] == funded_address], [])
        assert_raises_rpc_error(-8, 'index', node.recoverpqkeys, 0, 100, 0, tip)
        assert_raises_rpc_error(-8, 'height', node.recoverpqkeys, 0, 2, tip, 0)

        result = node.recoverpqkeys(0, 2, 0, tip)
        assert_equal(result['new_keys'], 3)
        assert_equal(result['next_index'], 3)
        assert_equal(result['start_height'], 0)
        assert_equal(result['stop_height'], tip)
        assert_equal(node.validateaddress(funded_address)['ismine'], True)

        recovered = [coin for coin in node.listunspent()
                     if coin['txid'] == funding_txid and
                     coin['address'] == funded_address]
        assert_equal(len(recovered), 1)
        assert_equal(recovered[0]['amount'], Decimal('1'))
        assert_equal(recovered[0]['spendable'], True)

        repeated = node.recoverpqkeys(0, 2, 0, tip)
        assert_equal(repeated['new_keys'], 0)
        assert_equal(repeated['next_index'], 3)

        self.stop_node(0)
        self.start_node(0, extra_args=['-bip44=1'])
        assert_equal(node.validateaddress(funded_address)['ismine'], True)
        assert_equal(node.getnewpqaddress(), source_addresses[3])

        destination = node.getnewaddress()
        raw = node.createrawtransaction(
            [{'txid': recovered[0]['txid'], 'vout': recovered[0]['vout']}],
            {destination: Decimal('0.9')})
        signed = node.signrawtransaction(raw)
        assert_equal(signed['complete'], True)
        node.sendrawtransaction(signed['hex'])


if __name__ == '__main__':
    PQRecoveryTest().main()
