#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test the abandontransaction RPC.

 The abandontransaction RPC marks a transaction and all its in-wallet
 descendants as abandoned which allows their inputs to be respent. It can be
 used to replace "stuck" or evicted transactions. It only works on transactions
 which are not included in a block and are not currently in the mempool. It has
 no effect on transactions which are already conflicted or abandoned.
"""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import sync_blocks, Decimal, sync_mempools, disconnect_nodes, assert_equal, connect_nodes

class AbandonConflictTest(RavenTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-minrelaytxfee=0.00001"], []]

    def run_test(self):
        self.nodes[1].generate(100)
        sync_blocks(self.nodes)
        balance = self.nodes[0].getbalance()
        txA = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("10"))
        txB = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("10"))
        txC = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("10"))
        sync_mempools(self.nodes)
        self.nodes[1].generate(1)

        sync_blocks(self.nodes)
        new_balance = self.nodes[0].getbalance()
        assert(balance - new_balance < Decimal("0.01")) #no more than fees lost
        balance = new_balance

        # Disconnect nodes so node0's transactions don't get into node1's mempool
        disconnect_nodes(self.nodes[0], 1)

        # Identify the 10btc outputs
        nA = next(i for i, vout in enumerate(self.nodes[0].getrawtransaction(txA, 1)["vout"]) if vout["value"] == Decimal("10"))
        nB = next(i for i, vout in enumerate(self.nodes[0].getrawtransaction(txB, 1)["vout"]) if vout["value"] == Decimal("10"))
        nC = next(i for i, vout in enumerate(self.nodes[0].getrawtransaction(txC, 1)["vout"]) if vout["value"] == Decimal("10"))

        inputs = [{"txid": txA, "vout": nA}, {"txid": txB, "vout": nB}]
        # spend 10btc outputs from txA and txB
        outputs = {self.nodes[0].getnewaddress(): Decimal("14.99998"), self.nodes[1].getnewaddress(): Decimal("5")}

        signed = self.nodes[0].signrawtransaction(self.nodes[0].createrawtransaction(inputs, outputs))
        txAB1 = self.nodes[0].sendrawtransaction(signed["hex"])

        # Identify the 14.99998btc output
        nAB = next(i for i, vout in enumerate(self.nodes[0].getrawtransaction(txAB1, 1)["vout"]) if vout["value"] == Decimal("14.99998"))

        #Create a child tx spending AB1 and C
        inputs = [{"txid": txAB1, "vout": nAB}, {"txid": txC, "vout": nC}]
        outputs = {self.nodes[0].getnewaddress(): Decimal("24.9996")}
        signed2 = self.nodes[0].signrawtransaction(self.nodes[0].createrawtransaction(inputs, outputs))
        txABC2 = self.nodes[0].sendrawtransaction(signed2["hex"])

        # In mempool txs from self should increase balance from change
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance - Decimal("30") + Decimal("24.9996"))
        balance = new_balance

        # Restart the node with a higher min relay fee so the parent tx is no longer in mempool
        # TODO: redo with eviction
        self.stop_node(0)
        self.start_node(0, extra_args=["-minrelaytxfee=0.0001"])

        # Verify txs no longer in either node's mempool
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        assert_equal(len(self.nodes[1].getrawmempool()), 0)

        # Not in mempool txs from self should only reduce balance
        # inputs are still spent, but change not received
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance - Decimal("24.9996"))
        # Unconfirmed received funds that are not in mempool, also shouldn't show
        # up in unconfirmed balance
        unconf_balance = self.nodes[0].getunconfirmedbalance() + self.nodes[0].getbalance()
        assert_equal(unconf_balance, new_balance)
        # Also shouldn't show up in listunspent
        assert(not txABC2 in [utxo["txid"] for utxo in self.nodes[0].listunspent(0)])
        balance = new_balance

        # Abandon original transaction and verify inputs are available again
        # including that the child tx was also abandoned
        self.nodes[0].abandontransaction(txAB1)
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance + Decimal("30"))
        balance = new_balance

        # Verify that even with a low min relay fee, the tx is not re-accepted from wallet on startup once abandoned
        self.stop_node(0)
        self.start_node(0, extra_args=["-minrelaytxfee=0.00001"])
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        assert_equal(self.nodes[0].getbalance(), balance)

        # But if its received again then it is un-abandoned
        # And since now in mempool, the change is available
        # But its child tx remains abandoned
        self.nodes[0].sendrawtransaction(signed["hex"])
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance - Decimal("20") + Decimal("14.99998"))
        balance = new_balance

        # Send child tx again so its un-abandoned
        self.nodes[0].sendrawtransaction(signed2["hex"])
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance - Decimal("10") - Decimal("14.99998") + Decimal("24.9996"))
        balance = new_balance

        # Remove using high relay fee again
        self.stop_node(0)
        self.start_node(0, extra_args=["-minrelaytxfee=0.0001"])
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance - Decimal("24.9996"))
        balance = new_balance

        # Create a double spend of AB1 by spending again from only A's 10 output
        # Mine double spend from node 1
        inputs = [{"txid": txA, "vout": nA}]
        outputs = {self.nodes[1].getnewaddress(): Decimal("9.998")}
        tx = self.nodes[0].createrawtransaction(inputs, outputs)
        signed = self.nodes[0].signrawtransaction(tx)
        self.nodes[1].sendrawtransaction(signed["hex"])
        self.nodes[1].generate(1)

        connect_nodes(self.nodes[0], 1)
        sync_blocks(self.nodes)

        # Verify that B and C's 10 RVN outputs are available for spending again because AB1 is now conflicted
        new_balance = self.nodes[0].getbalance()
        assert_equal(new_balance, balance + Decimal("20"))
        balance = new_balance

        # There is currently a minor bug around this and so this test doesn't work.  See Issue #7315
        # Invalidate the block with the double spend and B's 10 RVN output should no longer be available
        # Don't think C's should either
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        new_balance = self.nodes[0].getbalance()
        #assert_equal(new_balance, balance - Decimal("10"))
        self.log.info("If balance has not declined after invalidateblock then out of mempool wallet tx which is no longer")
        self.log.info("conflicted has not resumed causing its inputs to be seen as spent.  See Issue #7315")
        self.log.info(str(balance) + " -> " + str(new_balance) + " ?")

if __name__ == '__main__':
    AbandonConflictTest().main()
