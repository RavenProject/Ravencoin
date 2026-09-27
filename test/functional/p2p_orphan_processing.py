#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Core developers
# Copyright (c) 2017-2021 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Orphan processing yields between transactions.

Send many transactions that each spend several outputs of a parent the node
has not seen yet, then send the parent. Every one of those transactions should
be accepted, and a second peer should still receive a pong while that work is
in progress.
"""

import os

from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxOut, COIN, MsgTx
from test_framework.mininode import NetworkThread, NodeConn, NodeConnCB, from_hex, to_hex
from test_framework.script import CScript, OP_0, OP_DROP, OP_HASH256, OP_TRUE
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, p2p_port, wait_until

# Enough dependent transactions that working through them takes more than one
# turn of the message handler. Each one spends several outputs of the parent.
NUM_ORPHANS = 24
INPUTS_PER_ORPHAN = 4
# Bare OP_TRUE outputs are non-standard. Hash ops give each input real work
# while leaving the stack empty so the OP_TRUE output is the only stack item.
HASH_OPS = 199
# satoshis per byte, from DEFAULT_MIN_RELAY_TX_FEE (per KB)
MIN_RELAY_FEE_PER_BYTE = 1000
OUTPUT_VALUE = 10 * COIN // 100  # 0.1 RVN


def expensive_script_sig():
    return CScript([OP_0] + [OP_HASH256] * HASH_OPS + [OP_DROP])


class OrphanProcessingTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[
            "-acceptnonstdtxn=1",
            "-maxorphantx=%d" % (NUM_ORPHANS + 10),
            "-limitdescendantcount=%d" % (NUM_ORPHANS + 10),
            "-limitdescendantsize=5000",
            "-limitancestorsize=5000",
        ]]

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=120)
        self.start_nodes()

    def connect_peer(self):
        peer = NodeConnCB()
        conn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], peer)
        peer.add_connection(conn)
        return peer

    def run_test(self):
        node = self.nodes[0]
        node.generate(101)

        tx_peer = self.connect_peer()
        NetworkThread().start()
        tx_peer.wait_for_verack()
        tx_peer_id = node.getpeerinfo()[-1]["id"]

        ping_peer = self.connect_peer()
        ping_peer.wait_for_verack()
        ping_peer_id = node.getpeerinfo()[-1]["id"]
        assert ping_peer_id != tx_peer_id

        utxo = node.listunspent()[0]
        parent = CTransaction()
        parent.vin.append(CTxIn(
            COutPoint(int(utxo["txid"], 16), utxo["vout"]),
            b"",
            0xffffffff))
        for _ in range(NUM_ORPHANS * INPUTS_PER_ORPHAN):
            parent.vout.append(CTxOut(OUTPUT_VALUE, CScript([OP_TRUE])))
        signed = node.signrawtransaction(to_hex(parent))
        assert signed["complete"]
        parent = from_hex(CTransaction(), signed["hex"])
        parent.rehash()

        script_sig = expensive_script_sig()
        orphans = []
        for i in range(NUM_ORPHANS):
            tx = CTransaction()
            total_in = 0
            for j in range(INPUTS_PER_ORPHAN):
                n = i * INPUTS_PER_ORPHAN + j
                tx.vin.append(CTxIn(COutPoint(parent.x16r, n), script_sig, 0xffffffff))
                total_in += parent.vout[n].nValue
            tx.vout.append(CTxOut(0, CScript([OP_TRUE])))
            fee = MIN_RELAY_FEE_PER_BYTE * len(tx.serialize()) + 1000
            assert total_in > fee
            tx.vout[0].nValue = total_in - fee
            tx.rehash()
            orphans.append(tx)

        self.log.info("Sending %d orphans spending %d outputs each" % (NUM_ORPHANS, INPUTS_PER_ORPHAN))
        for tx in orphans:
            tx_peer.send_message(MsgTx(tx))
        tx_peer.sync_with_ping()

        log_path = os.path.join(node.datadir, "regtest", "debug.log")
        with open(log_path, "r", encoding="utf-8", errors="replace") as log_file:
            log_file.seek(0, os.SEEK_END)
            log_pos = log_file.tell()

        self.log.info("Sending parent and pinging the other peer")
        tx_peer.send_message(MsgTx(parent))
        ping_peer.sync_with_ping(timeout=30)

        expected = [parent.hash] + [tx.hash for tx in orphans]
        wait_until(lambda: set(expected).issubset(set(node.getrawmempool())),
                   timeout=120, err_msg="expected parent and orphans in mempool")

        mempool = set(node.getrawmempool())
        for txid in expected:
            assert txid in mempool
        assert_equal(len(set(expected)), NUM_ORPHANS + 1)

        with open(log_path, "r", encoding="utf-8", errors="replace") as log_file:
            log_file.seek(log_pos)
            new_log = log_file.read().splitlines()

        ping_at = None
        last_orphan_at = None
        accepted = []
        for i, line in enumerate(new_log):
            if "received: ping" in line and "peer=%d" % ping_peer_id in line:
                if ping_at is None:
                    ping_at = i
            if "accepted orphan tx" in line:
                last_orphan_at = i
                accepted.append(line)

        assert_equal(len(accepted), NUM_ORPHANS)
        assert ping_at is not None
        assert last_orphan_at is not None
        assert ping_at < last_orphan_at


if __name__ == '__main__':
    OrphanProcessingTest().main()
