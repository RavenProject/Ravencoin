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
        self.num_nodes = 4
        self.extra_args = [["-assetindex", "-debug=rewards"], ["-assetindex", "-minrewardheight=15"], ["-assetindex"], ["-assetindex"]]

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
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

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

        n0.generate(61)

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
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

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

        # Mine 60 blocks to make sure the -minrewardsheight is met
        n0.generate(61)

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
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "Snapshot request not found",
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
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "The asset hasn't been created: STOCK4",
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
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "Wallet doesn't have the ownership token(!) for the distribution asset",
            n0.distributereward, "STOCK5", tgtBlockHeight, "PAYOUT2", 2000, ownerAddr0)

    ## Attempts a payout for an invalid payout asset
    def payout_before_minimum_height_is_reached(self):
        self.log.info("Running payout before minimum rewards height is reached!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK6 asset")
        n0.issue(asset_name="STOCK6", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK6 ownership in 1 blocks")
        n0.requestsnapshot(asset_name="STOCK6", block_height=tgtBlockHeight)

        self.log.info("Skipping forward so that we're 15 blocks ahead of the snapshot height")
        n0.generate(16)
        self.sync_all()

        self.log.info("Initiating failing reward payout because we are only 15 block ahead of the snapshot instead of 60")
        assert_raises_rpc_error(-32600, "For security of the rewards payout, it is recommended to wait until chain is 60 blocks ahead of the snapshot height. You can modify this by using the -minrewardsheight.",
                                n0.distributereward, "STOCK6", tgtBlockHeight, "RVN", 2000, ownerAddr0)

    ## Attempts a payout using a custome rewards height of 15, and they have low rvn balance
    def payout_custom_height_set_with_low_funds(self):
        self.log.info("Running payout before minimum rewards height is reached with custom min height value set!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n1.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(5)
        self.sync_all()
        n1.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK7 asset")
        n1.issue(asset_name="STOCK7", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n1.generate(10)
        self.sync_all()

        shareholderAddr0 = n2.getnewaddress()

        n1.transfer(asset_name="STOCK7", qty=200, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=ownerAddr0)

        n1.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n1.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK7 ownership in 1 blocks")
        n1.requestsnapshot(asset_name="STOCK7", block_height=tgtBlockHeight)

        self.log.info("Skipping forward so that we're 30 blocks ahead of the snapshot height")
        n1.generate(31)
        self.sync_all()

        self.log.info("Initiating reward payout should succeed because -minrewardheight=15 on node1")
        n1.distributereward("STOCK7", tgtBlockHeight, "RVN", 2000, ownerAddr0)

        n1.generate(2)
        self.sync_all()

        assert_equal(n1.getdistributestatus("STOCK7", tgtBlockHeight, "RVN", 2000, ownerAddr0)['Status'], 3)

        n0.sendtoaddress(n1.getnewaddress(), 3000)
        n0.generate(5)
        self.sync_all()

        n1.generate(10)
        self.sync_all()

        assert_equal(n2.getreceivedbyaddress(shareholderAddr0, 1), 2000)

    ## Attempts a payout using a custome rewards height of 15, and they have low rvn balance
    def payout_with_insufficent_asset_amount(self):
        self.log.info("Running payout before minimum rewards height is reached with custom min height value set!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n1.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 2000)
        n0.generate(5)
        self.sync_all()
        n1.generate(10)
        self.sync_all()

        n1.issue(asset_name="STOCK_7.1", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)

        self.log.info("Issuing LOW_ASSET_AMOUNT asset")
        n1.issue(asset_name="LOW_ASSET_AMOUNT", qty=10000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n1.generate(10)
        self.sync_all()

        ## Send the majority of the assets to node0
        assetHolderAddress = n0.getnewaddress()
        n1.transfer(asset_name="LOW_ASSET_AMOUNT", qty=9000, to_address=assetHolderAddress, message="", expire_time=0, change_address="", asset_change_address=ownerAddr0)
        shareholderAddr0 = n2.getnewaddress()
        n1.transfer(asset_name="STOCK_7.1", qty=200, to_address=shareholderAddr0, message="", expire_time=0, change_address="", asset_change_address=ownerAddr0)
        n1.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n1.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK_7.1 ownership in 1 blocks")
        n1.requestsnapshot(asset_name="STOCK_7.1", block_height=tgtBlockHeight)

        self.log.info("Skipping forward so that we're 30 blocks ahead of the snapshot height")
        n1.generate(61)
        self.sync_all()

        self.log.info("Initiating reward payout")
        n1.distributereward("STOCK_7.1", tgtBlockHeight, "LOW_ASSET_AMOUNT", 2000, ownerAddr0)

        n1.generate(2)
        self.sync_all()

        assert_equal(n1.getdistributestatus("STOCK_7.1", tgtBlockHeight, "LOW_ASSET_AMOUNT", 2000, ownerAddr0)['Status'], 5)

        ## node0 transfer back the assets to node1, now the distribution transaction should get created successfully. when the next block is mined
        n0.transfer(asset_name="LOW_ASSET_AMOUNT", qty=9000, to_address=ownerAddr0, message="", expire_time=0, change_address="")
        n0.generate(5)
        self.sync_all()

        n1.generate(10)
        self.sync_all()

        assert_equal(n2.listassetbalancesbyaddress(shareholderAddr0)["LOW_ASSET_AMOUNT"], 2000)

    def listsnapshotrequests(self):
        self.log.info("Testing listsnapshotrequests()...")
        n0, n1 = self.nodes[0], self.nodes[1]


        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(n1.getnewaddress(), 1000)
        n0.generate(5)
        self.sync_all()

        srl = n1.listsnapshotrequests()
        assert_equal(0, len(srl))

        asset_name1 = "LISTME"
        block_height1 = n1.getblockcount() + 100

        asset_name2 = "LISTME2"
        block_height2 = n1.getblockcount() + 200

        # make sure a snapshot can't be created for a non-existent
        assert_raises_rpc_error(-8, "asset does not exist",
                                n1.requestsnapshot, asset_name1, block_height1)
        n1.issue(asset_name1)
        n1.issue(asset_name2)
        n1.generate(1)

        n1.requestsnapshot(asset_name=asset_name1, block_height=block_height1)
        n1.requestsnapshot(asset_name=asset_name1, block_height=block_height2)
        n1.requestsnapshot(asset_name=asset_name2, block_height=block_height2)

        n1.generate(1)
        srl = n1.listsnapshotrequests()
        assert_equal(3, len(srl))
        assert_contains({'asset_name': asset_name1, 'block_height': block_height1}, srl)
        assert_contains({'asset_name': asset_name1, 'block_height': block_height2}, srl)
        assert_contains({'asset_name': asset_name2, 'block_height': block_height2}, srl)

        srl = n1.listsnapshotrequests(asset_name1)
        assert_equal(2, len(srl))
        assert_contains({'asset_name': asset_name1, 'block_height': block_height1}, srl)
        assert_contains({'asset_name': asset_name1, 'block_height': block_height2}, srl)

        srl = n1.listsnapshotrequests(asset_name2)
        assert_equal(1, len(srl))
        assert_contains({'asset_name': asset_name2, 'block_height': block_height2}, srl)

    def basic_test_asset_uneven_distribution(self):
        self.log.info("Running ASSET reward test (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK8 asset")
        n0.issue(asset_name="STOCK8", qty=10000, to_address=distAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(asset_name="STOCK8", from_address=distAddr0, qty=300, to_address=shareholderAddr0, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK8", from_address=distAddr0, qty=300, to_address=shareholderAddr1, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK8", from_address=distAddr0, qty=300, to_address=shareholderAddr2, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)['STOCK8'], 300)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK8"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK8"], 300)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT8 asset")
        n0.issue(asset_name="PAYOUT8", qty=10, to_address=ownerAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK8 ownership in 5 blocks")
        n0.requestsnapshot(asset_name="STOCK8", block_height=tgtBlockHeight)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK8", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK8")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK8", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK8")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK8", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT8", gross_distribution_amount=10, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT8 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT8"], 3)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT8"], 3)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT8"], 3)

    def basic_test_asset_even_distribution(self):
        self.log.info("Running ASSET reward test (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK9 asset")
        n0.issue(asset_name="STOCK9", qty=10000, to_address=distAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(asset_name="STOCK9", from_address=distAddr0, qty=300, to_address=shareholderAddr0, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK9", from_address=distAddr0, qty=300, to_address=shareholderAddr1, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK9", from_address=distAddr0, qty=400, to_address=shareholderAddr2, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)['STOCK9'], 300)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK9"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK9"], 400)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT9 asset")
        n0.issue(asset_name="PAYOUT9", qty=10, to_address=ownerAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK9 ownership in 5 blocks")
        n0.requestsnapshot(asset_name="STOCK9", block_height=tgtBlockHeight)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK9", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK9")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK9", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK9")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK9", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT9", gross_distribution_amount=10, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT9 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT9"], 3)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT9"], 3)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT9"], 4)

    def basic_test_asset_round_down_uneven_distribution(self):
        self.log.info("Running ASSET reward test with uneven distribution (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK10 asset")
        n0.issue(asset_name="STOCK10", qty=10000, to_address=distAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(asset_name="STOCK10", from_address=distAddr0, qty=300, to_address=shareholderAddr0, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK10", from_address=distAddr0, qty=300, to_address=shareholderAddr1, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK10", from_address=distAddr0, qty=500, to_address=shareholderAddr2, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)['STOCK10'], 300)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK10"], 300)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK10"], 500)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT10 asset")
        n0.issue(asset_name="PAYOUT10", qty=10, to_address=ownerAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK10 ownership in 5 blocks")
        n0.requestsnapshot(asset_name="STOCK10", block_height=tgtBlockHeight)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK10", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK10")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK10", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK10")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK10", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT10", gross_distribution_amount=10, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT10 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT10"], 2)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT10"], 2)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT10"], 4)

    def basic_test_asset_round_down_uneven_distribution_2(self):
        self.log.info("Running ASSET reward test with uneven distribution (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK11 asset")
        n0.issue(asset_name="STOCK11", qty=10000, to_address=distAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()
        shareholderAddr3 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(asset_name="STOCK11", from_address=distAddr0, qty=9, to_address=shareholderAddr0, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK11", from_address=distAddr0, qty=3, to_address=shareholderAddr1, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK11", from_address=distAddr0, qty=2, to_address=shareholderAddr2, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK11", from_address=distAddr0, qty=1, to_address=shareholderAddr3, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)

        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)['STOCK11'], 9)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK11"], 3)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK11"], 2)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr3)["STOCK11"], 1)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT11 asset")
        n0.issue(asset_name="PAYOUT11", qty=10, to_address=ownerAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK11 ownership in 5 blocks")
        n0.requestsnapshot(asset_name="STOCK11", block_height=tgtBlockHeight)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK11", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK11")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK11", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK11")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 9)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 3)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 2)
                owner2 = True
            elif ownerAddr["address"] == shareholderAddr3:
                assert_equal(ownerAddr["amount_owned"], 1)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK11", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT11", gross_distribution_amount=10, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT11 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT11"], 6)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT11"], 2)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT11"], 1)

    def basic_test_asset_round_down_uneven_distribution_3(self):
        self.log.info("Running ASSET reward test with uneven distribution (units = 1)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK12 asset")
        n0.issue(asset_name="STOCK12", qty=10000, to_address=distAddr0, change_address="", \
                 units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholderAddr0 = n0.getnewaddress()
        shareholderAddr1 = n1.getnewaddress()
        shareholderAddr2 = n2.getnewaddress()
        shareholderAddr3 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(asset_name="STOCK12", from_address=distAddr0, qty=9, to_address=shareholderAddr0, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK12", from_address=distAddr0, qty=3, to_address=shareholderAddr1, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK12", from_address=distAddr0, qty=2, to_address=shareholderAddr2, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
        n0.transferfromaddress(asset_name="STOCK12", from_address=distAddr0, qty=1, to_address=shareholderAddr3, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)

        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)['STOCK12'], 9)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["STOCK12"], 3)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["STOCK12"], 2)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr3)["STOCK12"], 1)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT12 asset")
        n0.issue(asset_name="PAYOUT12", qty=10, to_address=ownerAddr0, change_address="", \
                 units=1, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK12 ownership in 5 blocks")
        n0.requestsnapshot(asset_name="STOCK12", block_height=tgtBlockHeight)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="STOCK12", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "STOCK12")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="STOCK12", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "STOCK12")
        assert_equal(snapShot["height"], tgtBlockHeight)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snapShot["owners"]:
            if ownerAddr["address"] == shareholderAddr0:
                assert_equal(ownerAddr["amount_owned"], 9)
                owner0 = True
            elif ownerAddr["address"] == shareholderAddr1:
                assert_equal(ownerAddr["amount_owned"], 3)
                owner1 = True
            elif ownerAddr["address"] == shareholderAddr2:
                assert_equal(ownerAddr["amount_owned"], 2)
                owner2 = True
            elif ownerAddr["address"] == shareholderAddr3:
                assert_equal(ownerAddr["amount_owned"], 1)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(asset_name="STOCK12", snapshot_height=tgtBlockHeight, distribution_asset_name="PAYOUT12", gross_distribution_amount=10, exception_addresses=distAddr0)
        n0.generate(10)
        self.sync_all()

        ##  Inexplicably, order matters here. We need to verify the amount
        ##      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT12 holdings after payout")
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr0)["PAYOUT12"], 6)
        assert_equal(n1.listassetbalancesbyaddress(shareholderAddr1)["PAYOUT12"], 2)
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr2)["PAYOUT12"], Decimal(str(1.3)))
        assert_equal(n0.listassetbalancesbyaddress(shareholderAddr3)["PAYOUT12"], Decimal(str(0.6)))

    def test_rvn_bulk(self):
        self.log.info("Running basic RVN reward test!")
        n0, n1, n2, n3 = self.nodes[0], self.nodes[1], self.nodes[2], self.nodes[3]

        self.log.info("Creating owner address")
        ownerAddr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {ownerAddr0}")

        self.log.info("Creating distributor address")
        distAddr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {distAddr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 1000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Issuing BULK1 asset")
        n0.issue(asset_name="BULK1", qty=100000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.issue(asset_name="TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1", qty=100000, to_address=ownerAddr0, change_address="", \
                 units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n0.listassetbalancesbyaddress(ownerAddr0)["BULK1"], 100000)

        self.log.info("Transferring all assets to a single address for tracking")
        n0.transfer(asset_name="BULK1", qty=100000, to_address=distAddr0)
        n0.generate(10)
        self.sync_all()
        assert_equal(n0.listassetbalancesbyaddress(distAddr0)["BULK1"], 100000)

        self.log.info("Creating shareholder addresses")
        address_list = [None]*9999
        for i in range(0, 9999, 3):
            address_list[i] = n1.getnewaddress()
            address_list[i+1] = n2.getnewaddress()
            address_list[i+2] = n3.getnewaddress()

        self.log.info("Distributing shares")
        count = 0
        for address in address_list:
            n0.transferfromaddress(asset_name="BULK1", from_address=distAddr0, qty=10, to_address=address, message="", expire_time=0, rvn_change_address="", asset_change_address=distAddr0)
            count += 1
            if count > 190:
                n0.generate(1)
                count = 0
                self.sync_all()

        n0.generate(1)
        self.sync_all()

        self.log.info("Verifying share distribution")
        for i in range(0, 9999, 3):
            assert_equal(n1.listassetbalancesbyaddress(address_list[i])["BULK1"], 10)
            assert_equal(n2.listassetbalancesbyaddress(address_list[i+1])["BULK1"], 10)
            assert_equal(n3.listassetbalancesbyaddress(address_list[i+2])["BULK1"], 10)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Providing additional funding")
        self.nodes[0].sendtoaddress(ownerAddr0, 2000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgtBlockHeight = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of BULK1 ownership in 100 blocks")
        n0.requestsnapshot(asset_name="BULK1", block_height=tgtBlockHeight)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(66)
        self.sync_all()

        self.log.info("Retrieving snapshot request")
        snapShotReq = n0.getsnapshotrequest(asset_name="BULK1", block_height=tgtBlockHeight)
        assert_equal(snapShotReq["asset_name"], "BULK1")
        assert_equal(snapShotReq["block_height"], tgtBlockHeight)

        self.log.info("Retrieving snapshot for ownership validation")
        snapShot = n0.getsnapshot(asset_name="BULK1", block_height=tgtBlockHeight)
        assert_equal(snapShot["name"], "BULK1")
        assert_equal(snapShot["height"], tgtBlockHeight)
        for ownerAddr in snapShot["owners"]:
            assert_equal(ownerAddr["amount_owned"], 10)

        self.log.info("Initiating reward payout")
        result = n0.distributereward(asset_name="BULK1", snapshot_height=tgtBlockHeight, distribution_asset_name="TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1", gross_distribution_amount=100000, exception_addresses=distAddr0, change_address="", dry_run=False)
        # print(result)
        n0.generate(10)
        self.sync_all()

        # 10000 / 999 = 10.01001001
        self.log.info("Checking reward payout")
        for i in range(0, 9999, 3):
            assert_equal(n1.listassetbalancesbyaddress(address_list[i])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))
            assert_equal(n2.listassetbalancesbyaddress(address_list[i+1])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))
            assert_equal(n3.listassetbalancesbyaddress(address_list[i+2])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))

    def run_test(self):
        self.activate_assets()
        self.basic_test_rvn()
        self.basic_test_asset()
        self.payout_without_snapshot()
        self.payout_with_invalid_ownership_asset()
        self.payout_with_invalid_payout_asset()
        self.payout_before_minimum_height_is_reached()
        self.listsnapshotrequests()
        self.payout_custom_height_set_with_low_funds()
        self.payout_with_insufficent_asset_amount()
        self.basic_test_asset_uneven_distribution()
        self.basic_test_asset_even_distribution()
        self.basic_test_asset_round_down_uneven_distribution()
        self.basic_test_asset_round_down_uneven_distribution_2()
        self.basic_test_asset_round_down_uneven_distribution_3()
        #self.test_asset_bulk()


if __name__ == "__main__":
    RewardsTest().main()
