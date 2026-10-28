#!/usr/bin/env python3
"""Verify that a wallet-bound reward resumes once after a node restart."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, Decimal


class RewardRestartTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-assetindex"],
                           ["-assetindex", "-minrewardheight=15", "-debug=rewards"],
                           ["-assetindex"]]

    def run_test(self):
        miner, payer, owner = self.nodes
        miner.generate(432)
        self.sync_all()
        assert_equal("active", miner.getblockchaininfo()["bip9_softforks"]["assets"]["status"])

        recipient = owner.getnewaddress()
        miner.issue("RESTARTREWARD", 1, recipient)
        miner.generate(1)
        self.sync_all()

        snapshot_height = miner.getblockcount() + 1
        payer.requestsnapshot("RESTARTREWARD", snapshot_height)
        miner.generate(31)
        self.sync_all()

        payout = Decimal("2000")
        before = owner.getreceivedbyaddress(recipient, 1)
        payer.distributereward("RESTARTREWARD", snapshot_height, "RVN", payout)
        assert_equal(payer.getdistributestatus(
            "RESTARTREWARD", snapshot_height, "RVN", payout)["Status"], 3)

        self.restart_node(1, self.extra_args[1])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        payer = self.nodes[1]
        assert_equal(payer.getdistributestatus(
            "RESTARTREWARD", snapshot_height, "RVN", payout)["Status"], 3)

        miner.sendtoaddress(payer.getnewaddress(), 3000)
        miner.generate(3)
        self.sync_all()
        # The retry can run before the wallet sees the funding block.
        # Confirm a payout broadcast during the final block separately.
        miner.generate(1)
        self.sync_all()
        assert_equal(owner.getreceivedbyaddress(recipient, 1), before + payout)

        self.restart_node(1, self.extra_args[1])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        miner.generate(3)
        self.sync_all()
        assert_equal(owner.getreceivedbyaddress(recipient, 1), before + payout)

        # A replacement snapshot must stop the old payout permanently. Its
        # recorded status tells the operator to reconcile any prior batches.
        miner.issue("REORGREWARD", 1, recipient)
        miner.generate(1)
        self.sync_all()
        replacement_height = miner.getblockcount() + 1
        payer = self.nodes[1]
        payer.requestsnapshot("REORGREWARD", replacement_height)
        miner.generate(31)
        self.sync_all()
        snapshot_block = miner.getblockhash(replacement_height)
        payer.distributereward("REORGREWARD", replacement_height, "RVN", payout)
        assert_equal(payer.getdistributestatus(
            "REORGREWARD", replacement_height, "RVN", payout)["Status"], 3)

        for node in self.nodes:
            node.invalidateblock(snapshot_block)
        miner.generate(32)
        self.sync_all()
        assert_equal(payer.getdistributestatus(
            "REORGREWARD", replacement_height, "RVN", payout)["Status"], 10)
        miner.sendtoaddress(payer.getnewaddress(), 3000)
        miner.generate(3)
        self.sync_all()
        assert_equal(owner.getreceivedbyaddress(recipient, 1), before + payout)


if __name__ == "__main__":
    RewardRestartTest().main()
