#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2019 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Testing rewards use cases

"""
from test_framework.test_framework import RavenTestFramework
from test_framework.util import *


import string

class RewardsTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-assetindex"], ["-assetindex"], ["-assetindex"]]

    def activate_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()["bip9_softforks"]["assets"]["status"])

    ## Basic functionality test - RVN reward
    ## - create the main owner address
    ## - mine blocks to have enouugh RVN for the reward payments, plus purchasing the asset
    ## - issue the STOCK1 asset to the owner
    ## - create 5 shareholder addresses
    ## - distribute different amounts of the STOCK1 asset to each of the shareholder addresses
    ## - mine some blocks
    ## - retrieve the current chain height
    ## - distribute an RVN reward amongst the shareholders
    ## - verify that each one receives the expected amount of reward RVN
    def basic_test_rvn(self):
        self.log.info("Running basic RVN reward test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK1 asset")
        n0.issue(asset_name="STOCK1", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n0.listassetbalancesbyaddress(ownerAddr0)["STOCK1"], 10000)

        self.log.info("Transferring all assets to a single address for tracking")
        n0.transfer(asset_name="STOCK1", qty=10000, to_address=distAddr0)
        n0.generate(10)
        self.sync_all()
        assert_equal(n0.listassetbalancesbyaddress(distAddr0)["STOCK1"], 10000)

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()
        shareholderAddr3 = n1.getnewaddress()
        shareholderAddr4 = n0.getnewaddress()

        self.log.info("Distributing shares")
        n0.transfer(asset_name="STOCK1", qty=200, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK1", qty=300, to_address=shareholderAddr1, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK1", qty=400, to_address=shareholderAddr2, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK1", qty=500, to_address=shareholderAddr3, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK1", qty=600, to_address=shareholderAddr4, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.generate(10)
        self.sync_all()

        self.log.info("Verifying share distribution")
        ##ownerDetails = n0.listmyassets("STOCK1", True)
        ##self.log.info(f"Owner: {ownerDetails}")
        ##distDetails = n0.listassetbalancesbyaddress(distAddr0)
        ##self.log.info(f"Change: {distDetails}")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["STOCK1"], 200)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK1"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK1"], 400)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr3)["STOCK1"], 500)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr4)["STOCK1"], 600)
        assert_equal(n0.listassetbalancesbyaddress(distAddr0)["STOCK1"], 8000)

        self.log.info("Mining blocks")
        n0.generate(200)
        self.sync_all()

        self.log.info("Providing additional funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 2000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Requesting snapshot of STOCK1 ownership in 100 blocks")
        n0.requestsnapshot(asset_name="STOCK1", block_height=tgtBlockHeight)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK1", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK1")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK1", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK1")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        owner3 = False
        owner4 = False
        owner5 = False
        for ownerAddr in snapShot["owners"]:
            ##self.log.info(f"Found owner {ownerAddr}")
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 200)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
            elif ownerAddr["address"] == shareholderAddr3:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner3 = True
            elif ownerAddr["address"] == shareholderAddr4:
                assert_equal(ownerAddr["amount_owned"], 600)
                owner4 = True
            elif ownerAddr["address"] == distAddr0:
                assert_equal(ownerAddr["amount_owned"], 8000)
                owner5 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)
        assert_equal(owner3, True)
        assert_equal(owner4, True)
        assert_equal(owner5, True)

        ##  listassetbalancesbyaddress only lists the most recently delivered amount
        ##      for the address, which I believe is a bug, since there can only be one
        ##      key in the result object with the asset name.
        ##self.log.info("Moving shares after snapshot")
        ##n0.transfer(asset_name="STOCK1", qty=100, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK1", qty=100, to_address=shareholderAddr1, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK1", qty=100, to_address=shareholderAddr2, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK1", qty=100, to_address=shareholderAddr3, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK1", qty=100, to_address=shareholderAddr4, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.generate(100)
        ##self.sync_all()

        ##self.log.info("Verifying share distribution after snapshot")
        ##assert_equal(n2.listassetbalancesbyaddress(shareholderAddr0)["STOCK1"], 300)
        ##assert_equal(n2.listassetbalancesbyaddress(shareholderAddr1)["STOCK1"], 400)
        ##assert_equal(n1.listassetbalancesbyaddress(shareholderAddr2)["STOCK1"], 500)
        ##assert_equal(n0.listassetbalancesbyaddress(shareholderAddr3)["STOCK1"], 600)
        ##assert_equal(n0.listassetbalancesbyaddress(shareholderAddr4)["STOCK1"], 700)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK1", snapshot_height=tgtBlockHeight, distribution_asset_name="RVN", gross_distribution_amount=2000, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying RVN holdings after payout")
        assert_equal(n0.getreceivedbyaddress(shareholderAddr0, 0), 200)
        assert_equal(n1.getreceivedbyaddress(shareholderAddr1, 0), 300)
        assert_equal(n2.getreceivedbyaddress(shareholderAddr2, 0), 400)
        assert_equal(n1.getreceivedbyaddress(shareholderAddr3, 0), 500)
        assert_equal(n0.getreceivedbyaddress(shareholderAddr4, 0), 600)

    ## Basic functionality test - ASSET reward
    ## - create the main owner address
    ## - mine blocks to have enouugh RVN for the reward fees, plus purchasing the asset
    ## - issue the STOCK2 asset to the owner
    ## - create 5 shareholder addresses
    ## - issue the PAYOUT1 asset to the owner
    ## - distribute different amounts of the PAYOUT1 asset to each of the shareholder addresses
    ## - mine some blocks
    ## - retrieve the current chain height
    ## - distribute reward of PAYOUT1 asset units amongst the shareholders
    ## - verify that each one receives the expected amount of PAYOUT1
    def basic_test_asset(self):
        self.log.info("Running basic ASSET reward test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK2 asset")
        n0.issue(asset_name="STOCK2", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()
        shareholderAddr3 = n1.getnewaddress()
        shareholderAddr4 = n0.getnewaddress()

        self.log.info("Distributing shares")
        n0.transfer(asset_name="STOCK2", qty=200, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK2", qty=300, to_address=shareholderAddr1, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK2", qty=400, to_address=shareholderAddr2, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK2", qty=500, to_address=shareholderAddr3, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.transfer(asset_name="STOCK2", qty=600, to_address=shareholderAddr4, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        n0.generate(10)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["STOCK2"], 200)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK2"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK2"], 400)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr3)["STOCK2"], 500)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr4)["STOCK2"], 600)

        self.log.info("Mining blocks")
        n0.generate(200)
        self.sync_all()

        self.log.info("Issuing PAYOUT1 asset")
        n0.issue(asset_name="PAYOUT1", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=8, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Requesting snapshot of STOCK2 ownership in 100 blocks")
        n0.requestsnapshot(asset_name="STOCK2", block_height=tgtBlockHeight)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK2", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK2")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK2", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK2")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        owner3 = False
        owner4 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 200)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
            elif ownerAddr["address"] == shareholderAddr3:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner3 = True
            elif ownerAddr["address"] == shareholderAddr4:
                assert_equal(ownerAddr["amount_owned"], 600)
                owner4 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)
        assert_equal(owner3, True)
        assert_equal(owner4, True)

        ##  listassetbalancesbyaddress only lists the most recently delivered amount
        ##      for the address, which I believe is a bug, since there can only be one
        ##      key in the result object with the asset name.
        ##self.log.info("Moving shares after snapshot")
        ##n0.transfer(asset_name="STOCK2", qty=100, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK2", qty=100, to_address=shareholderAddr1, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK2", qty=100, to_address=shareholderAddr2, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK2", qty=100, to_address=shareholderAddr3, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.transfer(asset_name="STOCK2", qty=100, to_address=shareholderAddr4, message="", expire_time=0, change_address="", asset_change_address=distAddr0)
        ##n0.generate(100)
        ##self.sync_all()

        ##self.log.info("Verifying share distribution after snapshot")
        ##assert_equal(n2.listassetbalancesbyaddress(shareholderAddr0)["STOCK2"], 300)
        ##assert_equal(n2.listassetbalancesbyaddress(shareholderAddr1)["STOCK2"], 400)
        ##assert_equal(n1.listassetbalancesbyaddress(shareholderAddr2)["STOCK2"], 500)
        ##assert_equal(n0.listassetbalancesbyaddress(shareholderAddr3)["STOCK2"], 600)
        ##assert_equal(n0.listassetbalancesbyaddress(shareholderAddr4)["STOCK2"], 700)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK2", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT1", gross_distribution_amount=2000, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT1 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT1"], 200)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT1"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT1"], 400)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr3)["PAYOUT1"], 500)
        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr4)["PAYOUT1"], 600)

    ## Attempts a payout without an asset snapshot
    def payout_without_snapshot(self):
        self.log.info("Running payout without snapshot test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK3 asset")
        n0.issue(asset_name="STOCK3", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(100)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-1, "Failed to distribute reward",
            n0.distributereward, "STOCK3", tgtBlockHeight, "RVN", 2000, ownerAddr0)

    ## Attempts a payout for an invalid ownership asset
    def payout_with_invalid_ownership_asset(self):
        self.log.info("Running payout with invalid ownership asset test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(100)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-1, "Failed to distribute reward",
            n0.distributereward, "STOCK4", tgtBlockHeight, "RVN", 2000, ownerAddr0)

    ## Attempts a payout for an invalid payout asset
    def payout_with_invalid_payout_asset(self):
        self.log.info("Running payout with invalid payout asset test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK5 asset")
        n0.issue(asset_name="STOCK5", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(100)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-1, "Failed to distribute reward",
            n0.distributereward, "STOCK5", tgtBlockHeight, "PAYOUT2", 2000, ownerAddr0)

    def run_test(self):
        self.activate_assets()
        self.basic_test_rvn()
        self.basic_test_asset()
        self.payout_without_snapshot()
        self.payout_with_invalid_ownership_asset()
        self.payout_with_invalid_payout_asset()


if __name__ == "__main__":
    RewardsTest().main()
