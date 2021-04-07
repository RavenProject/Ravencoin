#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test sweeping from an address

- 6 nodes
  * node0 will have a collection of RVN and a few assets
  * node1 will sweep on a specific asset
  * node2 will sweep on a different specific asset
  * node3 will sweep on all RVN
  * node4 will sweep everything else
  * node5 will attempt to sweep, but fail
"""

# Imports should be in PEP8 ordering (std library first, then third party
# libraries then local imports).

from collections import defaultdict

# Avoid wildcard * imports if possible
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_does_not_contain_key, assert_raises_rpc_error, connect_nodes, p2p_port

class FeatureSweepTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.extra_args = [['-assetindex', '-txindex', '-addressindex'] for _ in range(self.num_nodes)]

    def check_asset(self, assets, asset_name, balance):
        asset_names = list(assets.keys())

        assert_equal(asset_names.count(asset_name), 1)
        assert_equal(balance, assets[asset_name]["balance"])

    def prime_src(self, src_node):
        self.log.info("Priming node to be swept from with some RVN and 4 different assets!")

        # Generate the RVN
        self.log.info("> Generating RVN...")
        src_node.generate(1)
        self.sync_all()
        src_node.generate(431)
        self.sync_all()
        assert_equal("active", src_node.getblockchaininfo()['bip9_softforks']['assets']['status'])

        # Issue 3 different assets
        self.log.info("> Generating 4 different assets...")
        addr = src_node.getnewaddress()

        src_node.sendtoaddress(addr, 1000)
        src_node.issue(asset_name="ASSET.1", qty=1)
        src_node.issue(asset_name="ASSET.2", qty=2)
        src_node.issue(asset_name="ASSET.3", qty=3)
        src_node.issue(asset_name="ASSET.4", qty=4)

        self.log.info("> Waiting for ten confirmations after issue...")
        src_node.generate(10)
        self.sync_all()

        # Transfer to the address we will be using
        self.log.info("> Transfer assets to the right address")
        src_node.transfer(asset_name="ASSET.1", qty=1, to_address=addr)
        src_node.transfer(asset_name="ASSET.2", qty=2, to_address=addr)
        src_node.transfer(asset_name="ASSET.3", qty=3, to_address=addr)
        src_node.transfer(asset_name="ASSET.4", qty=4, to_address=addr)

        self.log.info("> Waiting for ten confirmations after transfer...")
        src_node.generate(10)
        self.sync_all()

        # Assert that we have everything correctly set up
        assets = src_node.listmyassets(asset="ASSET.*", verbose=True)

        assert_equal(100000000000, src_node.getaddressbalance(addr)["balance"])
        self.check_asset(assets, "ASSET.1", 1)
        self.check_asset(assets, "ASSET.2", 2)
        self.check_asset(assets, "ASSET.3", 3)
        self.check_asset(assets, "ASSET.4", 4)

        # We return the address for getting its private key
        return addr

    def run_test(self):
        """Main test logic"""
        self.log.info("Starting test!")

        # Split the nodes by name
        src_node   = self.nodes[0]
        ast1_node  = self.nodes[1]
        ast2_node  = self.nodes[2]
        rvn_node   = self.nodes[3]
        all_node   = self.nodes[4]
        fail_node  = self.nodes[5]

        # Activate assets and prime the src node
        asset_addr = self.prime_src(src_node)
        privkey = src_node.dumpprivkey(asset_addr)

        # Sweep single asset
        self.log.info("Testing sweeping of a single asset")
        txid_asset = ast1_node.sweep(privkey=privkey, asset_name="ASSET.2")
        ast1_node.generate(10)
        self.sync_all()

        swept_assets = ast1_node.listmyassets(asset="*", verbose=True)
        swept_keys = list(swept_assets.keys())

        assert_does_not_contain_key("ASSET.1", swept_keys)
        self.check_asset(swept_assets, "ASSET.2", 2)
        assert_does_not_contain_key("ASSET.2", src_node.listmyassets(asset="*", verbose=True).keys())
        assert_does_not_contain_key("ASSET.3", swept_keys)
        assert_does_not_contain_key("ASSET.4", swept_keys)

        # Sweep a different single asset
        self.log.info("Testing sweeping of a different single asset")
        txid_asset = ast2_node.sweep(privkey=privkey, asset_name="ASSET.4")
        ast2_node.generate(10)
        self.sync_all()

        swept_assets = ast2_node.listmyassets(asset="*", verbose=True)
        swept_keys = list(swept_assets.keys())

        assert_does_not_contain_key("ASSET.1", swept_keys)
        assert_does_not_contain_key("ASSET.2", swept_keys)
        assert_does_not_contain_key("ASSET.3", swept_keys)
        self.check_asset(swept_assets, "ASSET.4", 4)
        assert_does_not_contain_key("ASSET.4", src_node.listmyassets(asset="*", verbose=True).keys())

        # Sweep RVN
        self.log.info("Testing sweeping of all RVN")
        txid_rvn = rvn_node.sweep(privkey=privkey, asset_name="RVN")
        rvn_node.generate(10)
        self.sync_all()

        assert (rvn_node.getbalance() > 900)

        # Sweep remaining assets (fail)
        self.log.info("Testing failure of sweeping everything else with insufficient funds")
        assert_raises_rpc_error(-6, f"Please add RVN to address '{asset_addr}' to be able to sweep asset ''", all_node.sweep, privkey)

        # Fund the all_node so that we can fund the transaction
        src_node.sendtoaddress(all_node.getnewaddress(), 100)
        src_node.generate(10)
        self.sync_all()

        # Sweep remaining assets (pass)
        self.log.info("Testing sweeping of everything else")
        txid_all = all_node.sweep(privkey=privkey)
        all_node.generate(10)
        self.sync_all()

        all_assets = all_node.listmyassets(asset="*", verbose=True)

        self.check_asset(all_assets, "ASSET.1", 1)
        assert_does_not_contain_key("ASSET.2", all_assets.keys())
        self.check_asset(all_assets, "ASSET.3", 3)
        assert_does_not_contain_key("ASSET.4", all_assets.keys())

        # Fail with no assets to sweep
        self.log.info("Testing failure of sweeping an address with no assets")
        assert_raises_rpc_error(-26, "No assets to sweep!", all_node.sweep, privkey)

if __name__ == '__main__':
    FeatureSweepTest().main()
