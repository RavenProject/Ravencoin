#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Max Reorg Test
"""

import sys
import time
from test_framework.test_framework import RavenTestFramework
from test_framework.util import connect_all_nodes_bi, set_node_times, assert_equal, connect_nodes_bi, assert_contains_pair, assert_does_not_contain_key
from test_framework.mininode import wait_until


class MaxReorgTest(RavenTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        if len(sys.argv) > 1:
            self.num_nodes = int(sys.argv[1])
        self.max_reorg_depth = 60
        self.min_reorg_peers = 4
        self.min_reorg_age = 60 * 60 * 12
        # self.extra_args = [[f"-maxreorg={self.max_reorg_depth}", f"-minreorgpeers={self.min_reorg_peers}", f"-minreorgage={self.min_reorg_age}"] for i in range(self.num_nodes)]

    def add_options(self, parser):
        parser.add_option("--height", dest="height", default=65, help="The height of good branch when adversary surprises.")
        parser.add_option("--tip_age", dest="tip_age", default=60*5, help="Age of tip of non-adversaries at time of reorg.")
        parser.add_option("--should_reorg", dest="should_reorg", default=0, help="Whether a reorg is expected (0 or 1).")


    def setup_network(self):
        """Make this a fully connected network"""
        self.log.info("Running setup_network")
        self.setup_nodes()

        # Connect every node to every other
        connect_all_nodes_bi(self.nodes)
        self.sync_all()


    def reorg_test(self):
        height = int(self.options.height)
        peers = self.num_nodes
        tip_age = int(self.options.tip_age)
        should_reorg = int(self.options.should_reorg)

        self.log.info(f"Doing a reorg test with height: {height}, peers: {peers}, tip_age: {tip_age}.  " + \
                      f"Should reorg? *{should_reorg}*")

        asset_name = "MOON_STONES"
        adversary = self.nodes[0]
        subject = self.nodes[-1]

        # enough to activate assets
        start = 432

        self.log.info(f"Setting all node times to {tip_age} seconds ago...")
        now = int(round(time.time()))
        set_node_times(self.nodes, now - tip_age)

        self.log.info(f"Mining {start} starter blocks on all nodes and syncing...")
        subject.generate(round(start/2))
        self.sync_all()
        adversary.generate(round(start/2))
        self.sync_all()

        self.log.info("Stopping adversary node...")
        self.stop_node(0)

        self.log.info(f"Subject is issuing asset: {asset_name}...")
        subject.issue(asset_name)

        self.log.info(f"Miners are mining {height} blocks...")
        subject.generate(height)
        wait_until(lambda: [n.getblockcount() for n in self.nodes[1:]] == [height+start] * (peers-1), err_msg="Wait for BlockCount")
        self.log.info("BlockCount: " + str([start] + [n.getblockcount() for n in self.nodes[1:]]))

        self.log.info("Restarting adversary node...")
        self.start_node(0)

        self.log.info(f"Adversary is issuing asset: {asset_name}...")
        adversary.issue(asset_name)

        self.log.info(f"Adversary is mining {height*2} (2 x {height}) blocks over the next ~{tip_age} seconds...")
        interval = round(tip_age / (height * 2)) + 1
        for i in range(0, height*2):
            set_node_times(self.nodes, (now - tip_age) + ((i+1) * interval))
            adversary.generate(1)
        assert(adversary.getblockcount() - start == (subject.getblockcount() - start) * 2)
        besttimes = [n.getblock(n.getbestblockhash())['time'] for n in self.nodes]
        self.log.info("BestTimes: " + str(besttimes))
        self.log.info(f"Adversary: {besttimes[0]}; subject: {besttimes[-1]}; difference: {besttimes[0] - besttimes[-1]}; expected gte: {tip_age}")
        assert(besttimes[0] - besttimes[-1] >= tip_age)

        self.log.info("BlockCount: " + str([n.getblockcount() for n in self.nodes]))

        self.log.info("Reconnecting the network and syncing the chain...")
        for i in range(1, peers):
            connect_nodes_bi(self.nodes, 0, i, should_reorg)

        expected_height = start + height
        subject_owns_asset = True
        if should_reorg > 0:
            self.log.info(f"Expected a reorg -- blockcount should be {expected_height} and subject should own {asset_name} (waiting 5 seconds)...")
            expected_height += height
            subject_owns_asset = False
        else:
            self.log.info(f"Didn't expect a reorg -- blockcount should remain {expected_height} and both subject and adversary should own {asset_name} (waiting 5 seconds)...")

        # noinspection PyBroadException
        try:
            wait_until(lambda: [n.getblockcount() for n in self.nodes] == [expected_height] * peers, timeout=5, err_msg="getblockcount")
        except:
            pass
        self.log.info("BlockCount: " +str([n.getblockcount() for n in self.nodes]))
        assert_equal(subject.getblockcount(), expected_height)
        assert_contains_pair(asset_name + '!', 1, adversary.listmyassets())
        if subject_owns_asset:
            assert_contains_pair(asset_name + '!', 1, subject.listmyassets())
        else:
            assert_does_not_contain_key(asset_name + '!', subject.listmyassets())


    def run_test(self):
        self.log.info(f"Number of peers: {self.num_nodes}")
        self.log.info(f"Chain params: max_reorg_depth: {self.max_reorg_depth}, " + \
                      f"max_reorg_peers: {self.min_reorg_peers}, " + \
                      f"min_reorg_age: {self.min_reorg_age}.")
        self.reorg_test()


if __name__ == '__main__':
    MaxReorgTest().main()
