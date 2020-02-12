#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test processing of feefilter messages.

(Wallet now has DEFAULT_TRANSACTION_MINFEE = 0.01000000
"""

import time
from test_framework.mininode import mininode_lock, NodeConnCB, NodeConn, NetworkThread, MsgFeeFilter
from test_framework.test_framework import RavenTestFramework
from test_framework.util import sync_blocks, p2p_port, Decimal, sync_mempools

def hash_to_hex(hash_data):
    return format(hash_data, '064x')

# Wait up to 60 secs to see if the testnode has received all the expected invs
def all_invs_match(invs_expected, testnode):
    for _ in range(60):
        with mininode_lock:
            if sorted(invs_expected) == sorted(testnode.txinvs):
                return True
        time.sleep(1)
    return False

class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.txinvs = []

    def on_inv(self, conn, message):
        for i in message.inv:
            if i.type == 1:
                self.txinvs.append(hash_to_hex(i.hash))

    def clear_invs(self):
        with mininode_lock:
            self.txinvs = []

class FeeFilterTest(RavenTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def run_test(self):
        node1 = self.nodes[1]
        node0 = self.nodes[0]
        # Get out of IBD
        node1.generate(1)
        sync_blocks(self.nodes)

        # Setup the p2p connections and start up the network thread.
        test_node = TestNode()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node)
        test_node.add_connection(connection)
        NetworkThread().start()
        test_node.wait_for_verack()

        # Test that invs are received for all txs at feerate of 2,000,000 corbies
        node1.settxfee(Decimal("0.02000000"))
        txids = [node1.sendtoaddress(node1.getnewaddress(), 1) for _ in range(3)]
        assert(all_invs_match(txids, test_node))
        test_node.clear_invs()

        # Set a filter of 1,500,000 corbies (must be above 1,000,000 corbies (min fee is enforced)
        test_node.send_and_ping(MsgFeeFilter(1500000))

        # Test that txs are still being received (paying 70 sat/byte)
        txids = [node1.sendtoaddress(node1.getnewaddress(), 1) for _ in range(3)]
        assert(all_invs_match(txids, test_node))
        test_node.clear_invs()

        # Change tx fee rate to 1,350,000 corbies and test they are no longer received
        node1.settxfee(Decimal("0.013500000"))
        [node1.sendtoaddress(node1.getnewaddress(), 1) for _ in range(3)]
        sync_mempools(self.nodes) # must be sure node 0 has received all txs 

        # Raise the tx fee back up above the mintxfee, submit 1 tx on node 0,
        # then sync nodes 0 and 1 - we should only have 1 tx (this one below since
        # the one above was below the min txfee).
        # Send one transaction from node0 that should be received, so that we
        # we can sync the test on receipt (if node1's txs were relayed, they'd
        # be received by the time this node0 tx is received). This is
        # unfortunately reliant on the current relay behavior where we batch up
        # to 35 entries in an inv, which means that when this next transaction
        # is eligible for relay, the prior transactions from node1 are eligible
        # as well.
        node0.settxfee(Decimal("0.01600000"))
        txids = [node0.sendtoaddress(node0.getnewaddress(), 1)] #
        assert(all_invs_match(txids, test_node))
        test_node.clear_invs()

        # Remove fee filter and check that txs are received again
        test_node.send_and_ping(MsgFeeFilter(0))
        txids = [node1.sendtoaddress(node1.getnewaddress(), 1) for _ in range(3)]
        assert(all_invs_match(txids, test_node))
        test_node.clear_invs()

if __name__ == '__main__':
    FeeFilterTest().main()
