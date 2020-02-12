#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test txindex generation and fetching"""

import binascii
from test_framework.test_framework import RavenTestFramework
from test_framework.util import connect_nodes_bi, assert_equal
from test_framework.script import CScript, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, CTxIn, CTxOut, COutPoint

class TxIndexTest(RavenTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4

    def setup_network(self):
        self.add_nodes(4, [
            # Nodes 0/1 are "wallet" nodes
            [],
            ["-txindex"],
            # Nodes 2/3 are used for testing
            ["-txindex"],
            ["-txindex"]])

        self.start_nodes()

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 0, 3)

        self.sync_all()

    def run_test(self):
        self.log.info("Mining blocks...")
        self.nodes[0].generate(105)
        self.sync_all()

        chain_height = self.nodes[1].getblockcount()
        assert_equal(chain_height, 105)

        self.log.info("Testing transaction index...")

        #privkey = "cSdkPxkAjA4HDr5VHgsebAPDEh9Gyub4HK8UJr2DFGGqKKy4K5sG"
        #address = "mgY65WSfEmsyYaYPQaXhmXMeBhwp4EcsQW"
        address_hash = bytes([11,47,10,12,49,191,224,64,107,12,204,19,129,253,190,49,25,70,218,220])
        script_pub_key = CScript([OP_DUP, OP_HASH160, address_hash, OP_EQUALVERIFY, OP_CHECKSIG])
        unspent = self.nodes[0].listunspent()
        tx = CTransaction()
        amount = int(unspent[0]["amount"] * 10000000)
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        tx.vout = [CTxOut(amount, script_pub_key)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check verbose raw transaction results
        verbose = self.nodes[3].getrawtransaction(unspent[0]["txid"], 1)
        assert_equal(verbose["vout"][0]["valueSat"], 500000000000)
        assert_equal(verbose["vout"][0]["value"], 5000)

        self.log.info("All Tests Passed")


if __name__ == '__main__':
    TxIndexTest().main()
