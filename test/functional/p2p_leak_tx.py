#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test that we don't leak txs to inbound peers that we haven't yet announced to"""

from test_framework.mininode import MsgGetdata, CInv, NetworkThread, NodeConn, NodeConnCB
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, p2p_port

class TestNode(NodeConnCB):
    def on_inv(self, conn, message):
        pass


class P2PLeakTxTest(RavenTestFramework):
    def set_test_params(self):
        self.num_nodes = 1


    def run_test(self):
        gen_node = self.nodes[0]  # The block and tx generating node
        gen_node.generate(1)

        # Setup the attacking p2p connection and start up the network thread.
        self.inbound_peer = TestNode()
        connections = [NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.inbound_peer)]
        self.inbound_peer.add_connection(connections[0])
        NetworkThread().start() # Start up network handling in another thread


        max_repeats = 48
        self.log.info("Running test up to {} times.".format(max_repeats))
        for i in range(max_repeats):
            self.log.info('Run repeat {}'.format(i + 1))
            txid = gen_node.sendtoaddress(gen_node.getnewaddress(), 0.01)

            want_tx = MsgGetdata()
            want_tx.inv.append(CInv(t=1, h=int(txid, 16)))
            self.inbound_peer.last_message.pop('notfound', None)
            connections[0].send_message(want_tx)
            self.inbound_peer.sync_with_ping()

            if self.inbound_peer.last_message.get('notfound'):
                self.log.debug('tx {} was not yet announced to us.'.format(txid))
                self.log.debug("node has responded with a notfound message. End test.")
                assert_equal(self.inbound_peer.last_message['notfound'].vec[0].hash, int(txid, 16))
                self.inbound_peer.last_message.pop('notfound')
                break
            else:
                self.log.debug('tx {} was already announced to us. Try test again.'.format(txid))
                assert int(txid, 16) in [inv.hash for inv in self.inbound_peer.last_message['inv'].inv]


if __name__ == '__main__':
    P2PLeakTxTest().main()
