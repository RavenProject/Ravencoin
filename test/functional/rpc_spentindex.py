#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test RPC addressindex generation and fetching"""

import binascii
from test_framework.test_framework import RavenTestFramework
from test_framework.util import connect_nodes_bi, assert_equal
from test_framework.script import CScript, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, CTxIn, COutPoint, CTxOut

class SpentIndexTest(RavenTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4

    def setup_network(self):
        self.add_nodes(4, [
            # Nodes 0/1 are "wallet" nodes
            [],
            ["-spentindex"],
            # Nodes 2/3 are used for testing
            ["-spentindex"],
            ["-spentindex", "-txindex"]])

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

        # Check that
        self.log.info("Testing spent index...")

        fee_satoshis = 192000
        privkey = "cSdkPxkAjA4HDr5VHgsebAPDEh9Gyub4HK8UJr2DFGGqKKy4K5sG"
        #address = "mgY65WSfEmsyYaYPQaXhmXMeBhwp4EcsQW"
        address_hash = bytes([11,47,10,12,49,191,224,64,107,12,204,19,129,253,190,49,25,70,218,220])
        script_pub_key = CScript([OP_DUP, OP_HASH160, address_hash, OP_EQUALVERIFY, OP_CHECKSIG])
        unspent = self.nodes[0].listunspent()
        tx = CTransaction()
        amount = int(unspent[0]["amount"] * 100000000 - fee_satoshis)
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        tx.vout = [CTxOut(amount, script_pub_key)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()

        self.log.info("Testing getspentinfo method...")

        # Check that the spentinfo works standalone
        info = self.nodes[1].getspentinfo({"txid": unspent[0]["txid"], "index": unspent[0]["vout"]})
        assert_equal(info["txid"], txid)
        assert_equal(info["index"], 0)
        assert_equal(info["height"], 106)

        self.log.info("Testing getrawtransaction method...")

        # Check that verbose raw transaction includes spent info
        tx_verbose = self.nodes[3].getrawtransaction(unspent[0]["txid"], 1)
        assert_equal(tx_verbose["vout"][unspent[0]["vout"]]["spentTxId"], txid)
        assert_equal(tx_verbose["vout"][unspent[0]["vout"]]["spentIndex"], 0)
        assert_equal(tx_verbose["vout"][unspent[0]["vout"]]["spentHeight"], 106)

        # Check that verbose raw transaction includes input values
        tx_verbose2 = self.nodes[3].getrawtransaction(txid, 1)
        assert_equal(float(tx_verbose2["vin"][0]["value"]), (amount + fee_satoshis) / 100000000)
        assert_equal(tx_verbose2["vin"][0]["valueSat"], amount + fee_satoshis)

        # Check that verbose raw transaction includes address values and input values
        #privkey2 = "cSdkPxkAjA4HDr5VHgsebAPDEh9Gyub4HK8UJr2DFGGqKKy4K5sG"
        address2 = "mgY65WSfEmsyYaYPQaXhmXMeBhwp4EcsQW"
        address_hash2 = bytes([11, 47, 10, 12, 49, 191, 224, 64, 107, 12, 204, 19, 129, 253, 190, 49, 25, 70, 218, 220])
        script_pub_key2 = CScript([OP_DUP, OP_HASH160, address_hash2, OP_EQUALVERIFY, OP_CHECKSIG])
        tx2 = CTransaction()
        tx2.vin = [CTxIn(COutPoint(int(txid, 16), 0))]
        amount = int(amount - fee_satoshis)
        tx2.vout = [CTxOut(amount, script_pub_key2)]
        tx.rehash()
        self.nodes[0].importprivkey(privkey)
        signed_tx2 = self.nodes[0].signrawtransaction(binascii.hexlify(tx2.serialize()).decode("utf-8"))
        txid2 = self.nodes[0].sendrawtransaction(signed_tx2["hex"], True)

        # Check the mempool index
        self.sync_all()
        tx_verbose3 = self.nodes[1].getrawtransaction(txid2, 1)
        assert_equal(tx_verbose3["vin"][0]["address"], address2)
        assert_equal(tx_verbose3["vin"][0]["valueSat"], amount + fee_satoshis)
        assert_equal(float(tx_verbose3["vin"][0]["value"]), (amount + fee_satoshis) / 100000000)


        # Check the database index
        block_hash = self.nodes[0].generate(1)
        self.sync_all()

        tx_verbose4 = self.nodes[3].getrawtransaction(txid2, 1)
        assert_equal(tx_verbose4["vin"][0]["address"], address2)
        assert_equal(tx_verbose4["vin"][0]["valueSat"], amount + fee_satoshis)
        assert_equal(float(tx_verbose4["vin"][0]["value"]), (amount + fee_satoshis) / 100000000)

        # Check block deltas
        self.log.info("Testing getblockdeltas...")

        block = self.nodes[3].getblockdeltas(block_hash[0])
        assert_equal(len(block["deltas"]), 2)
        assert_equal(block["deltas"][0]["index"], 0)
        assert_equal(len(block["deltas"][0]["inputs"]), 0)
        assert_equal(len(block["deltas"][0]["outputs"]), 0)
        assert_equal(block["deltas"][1]["index"], 1)
        assert_equal(block["deltas"][1]["txid"], txid2)
        assert_equal(block["deltas"][1]["inputs"][0]["index"], 0)
        assert_equal(block["deltas"][1]["inputs"][0]["address"], "mgY65WSfEmsyYaYPQaXhmXMeBhwp4EcsQW")
        assert_equal(block["deltas"][1]["inputs"][0]["satoshis"], (amount + fee_satoshis) * -1)
        assert_equal(block["deltas"][1]["inputs"][0]["prevtxid"], txid)
        assert_equal(block["deltas"][1]["inputs"][0]["prevout"], 0)
        assert_equal(block["deltas"][1]["outputs"][0]["index"], 0)
        assert_equal(block["deltas"][1]["outputs"][0]["address"], "mgY65WSfEmsyYaYPQaXhmXMeBhwp4EcsQW")
        assert_equal(block["deltas"][1]["outputs"][0]["satoshis"], amount)

        self.log.info("All Tests Passed")


if __name__ == '__main__':
    SpentIndexTest().main()
