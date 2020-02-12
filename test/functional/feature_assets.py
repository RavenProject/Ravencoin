#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing asset use cases"""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_is_hash_string, assert_does_not_contain_key, assert_raises_rpc_error, JSONRPCException, Decimal

import string


class AssetTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-assetindex'], ['-assetindex'], ['-assetindex']]

    def activate_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0 = self.nodes[0]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])

    def big_test(self):
        self.log.info("Running big test!")
        n0, n1 = self.nodes[0], self.nodes[1]

        self.log.info("Calling issue()...")
        address0 = n0.getnewaddress()
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        n0.issue(asset_name="MY_ASSET", qty=1000, to_address=address0, change_address="",
                 units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for ten confirmations after issue...")
        n0.generate(10)
        self.sync_all()

        self.log.info("Checkout getassetdata()...")
        assetdata = n0.getassetdata("MY_ASSET")
        assert_equal(assetdata["name"], "MY_ASSET")
        assert_equal(assetdata["amount"], 1000)
        assert_equal(assetdata["units"], 4)
        assert_equal(assetdata["reissuable"], 1)
        assert_equal(assetdata["has_ipfs"], 1)
        assert_equal(assetdata["ipfs_hash"], ipfs_hash)

        self.log.info("Checking listmyassets()...")
        myassets = n0.listmyassets(asset="MY_ASSET*", verbose=True)
        assert_equal(len(myassets), 2)
        asset_names = list(myassets.keys())
        assert_equal(asset_names.count("MY_ASSET"), 1)
        assert_equal(asset_names.count("MY_ASSET!"), 1)
        assert_equal(myassets["MY_ASSET"]["balance"], 1000)
        assert_equal(myassets["MY_ASSET!"]["balance"], 1)
        assert_equal(len(myassets["MY_ASSET"]["outpoints"]), 1)
        assert_equal(len(myassets["MY_ASSET!"]["outpoints"]), 1)
        assert_is_hash_string(myassets["MY_ASSET"]["outpoints"][0]["txid"])
        assert_equal(myassets["MY_ASSET"]["outpoints"][0]["txid"], myassets["MY_ASSET!"]["outpoints"][0]["txid"])
        assert (int(myassets["MY_ASSET"]["outpoints"][0]["vout"]) >= 0)
        assert (int(myassets["MY_ASSET!"]["outpoints"][0]["vout"]) >= 0)
        assert_equal(myassets["MY_ASSET"]["outpoints"][0]["amount"], 1000)
        assert_equal(myassets["MY_ASSET!"]["outpoints"][0]["amount"], 1)

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n0.listassetbalancesbyaddress(address0)["MY_ASSET"], 1000)
        assert_equal(n0.listassetbalancesbyaddress(address0)["MY_ASSET!"], 1)

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n0.listaddressesbyasset("MY_ASSET"), n1.listaddressesbyasset("MY_ASSET"))

        self.log.info("Calling transfer()...")
        address1 = n1.getnewaddress()
        n0.transfer(asset_name="MY_ASSET", qty=200, to_address=address1)

        self.log.info("Waiting for ten confirmations after transfer...")
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking listmyassets()...")
        myassets = n1.listmyassets(asset="MY_ASSET*", verbose=True)
        assert_equal(len(myassets), 1)
        asset_names = list(myassets.keys())
        assert_equal(asset_names.count("MY_ASSET"), 1)
        assert_equal(asset_names.count("MY_ASSET!"), 0)
        assert_equal(myassets["MY_ASSET"]["balance"], 200)
        assert_equal(len(myassets["MY_ASSET"]["outpoints"]), 1)
        assert_is_hash_string(myassets["MY_ASSET"]["outpoints"][0]["txid"])
        assert (int(myassets["MY_ASSET"]["outpoints"][0]["vout"]) >= 0)
        assert_equal(n0.listmyassets(asset="MY_ASSET")["MY_ASSET"], 800)

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n1.listassetbalancesbyaddress(address1)["MY_ASSET"], 200)
        changeaddress = None
        assert_equal(n0.listaddressesbyasset("MY_ASSET"), n1.listaddressesbyasset("MY_ASSET"))
        assert_equal(sum(n0.listaddressesbyasset("MY_ASSET").values()), 1000)
        assert_equal(sum(n1.listaddressesbyasset("MY_ASSET").values()), 1000)
        for assaddr in n0.listaddressesbyasset("MY_ASSET").keys():
            if n0.validateaddress(assaddr)["ismine"]:
                changeaddress = assaddr
                assert_equal(n0.listassetbalancesbyaddress(changeaddress)["MY_ASSET"], 800)
        assert (changeaddress is not None)
        assert_equal(n0.listassetbalancesbyaddress(address0)["MY_ASSET!"], 1)

        self.log.info("Burning all units to test reissue on zero units...")
        n0.transfer(asset_name="MY_ASSET", qty=800, to_address="n1BurnXXXXXXXXXXXXXXXXXXXXXXU1qejP")
        n0.generate(1)
        assert_does_not_contain_key("MY_ASSET", n0.listmyassets(asset="MY_ASSET", verbose=True))

        self.log.info("Calling reissue()...")
        address1 = n0.getnewaddress()
        ipfs_hash2 = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        n0.reissue(asset_name="MY_ASSET", qty=2000, to_address=address0, change_address=address1, reissuable=False, new_units=-1, new_ipfs=ipfs_hash2)

        self.log.info("Waiting for ten confirmations after reissue...")
        self.sync_all()
        n0.generate(10)
        self.sync_all()

        self.log.info("Checkout getassetdata()...")
        assetdata = n0.getassetdata("MY_ASSET")
        assert_equal(assetdata["name"], "MY_ASSET")
        assert_equal(assetdata["amount"], 3000)
        assert_equal(assetdata["units"], 4)
        assert_equal(assetdata["reissuable"], 0)
        assert_equal(assetdata["has_ipfs"], 1)
        assert_equal(assetdata["ipfs_hash"], ipfs_hash2)

        self.log.info("Checking listassetbalancesbyaddress()...")
        assert_equal(n0.listassetbalancesbyaddress(address0)["MY_ASSET"], 2000)

        self.log.info("Checking listassets()...")
        n0.issue("RAVEN1", 1000)
        n0.issue("RAVEN2", 1000)
        n0.issue("RAVEN3", 1000)
        n0.generate(1)
        self.sync_all()

        n0.listassets(asset="RAVEN*", verbose=False, count=2, start=-2)

        self.log.info("Creating some sub-assets...")
        n0.issue(asset_name="MY_ASSET/SUB1", qty=1000, to_address=address0, change_address=address0, units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.sync_all()
        self.log.info("Waiting for ten confirmations after issuesubasset...")
        n0.generate(10)
        self.sync_all()

        self.log.info("Checkout getassetdata()...")
        assetdata = n0.getassetdata("MY_ASSET/SUB1")
        assert_equal(assetdata["name"], "MY_ASSET/SUB1")
        assert_equal(assetdata["amount"], 1000)
        assert_equal(assetdata["units"], 4)
        assert_equal(assetdata["reissuable"], 1)
        assert_equal(assetdata["has_ipfs"], 1)
        assert_equal(assetdata["ipfs_hash"], ipfs_hash)

        raven_assets = n0.listassets(asset="RAVEN*", verbose=False, count=2, start=-2)
        assert_equal(len(raven_assets), 2)
        assert_equal(raven_assets[0], "RAVEN2")
        assert_equal(raven_assets[1], "RAVEN3")
        self.sync_all()

    def issue_param_checks(self):
        self.log.info("Checking bad parameter handling!")
        n0 = self.nodes[0]

        # just plain bad asset name
        assert_raises_rpc_error(-8, "Invalid asset name: bad-asset-name", n0.issue, "bad-asset-name")

        # trying to issue things that can't be issued
        assert_raises_rpc_error(-8, "Unsupported asset type: OWNER", n0.issue, "AN_OWNER!")
        assert_raises_rpc_error(-8, "Unsupported asset type: VOTE", n0.issue, "A_VOTE^PEDRO")

        # check bad unique params
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique asset.", n0.issue, "A_UNIQUE#ASSET", 2)
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique asset.", n0.issue, "A_UNIQUE#ASSET", 1, "", "", 1)
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique asset.", n0.issue, "A_UNIQUE#ASSET", 1, "", "", 0, True)

    def chain_assets(self):
        self.log.info("Issuing chained assets in depth issue()...")
        n0, n1 = self.nodes[0], self.nodes[1]

        chain_address = n0.getnewaddress()
        ipfs_hash = "QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"
        chain_string = "CHAIN1"
        n0.issue(asset_name=chain_string, qty=1000, to_address=chain_address, change_address="", units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        for c in string.ascii_uppercase:
            chain_string += '/' + c
            if len(chain_string) > 30:
                break
            n0.issue(asset_name=chain_string, qty=1000, to_address=chain_address, change_address="", units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        n0.generate(1)
        self.sync_all()

        chain_assets = n1.listassets(asset="CHAIN1*", verbose=False)
        assert_equal(len(chain_assets), 13)

        self.log.info("Issuing chained assets in width issue()...")
        chain_address = n0.getnewaddress()
        chain_string = "CHAIN2"
        n0.issue(asset_name=chain_string, qty=1000, to_address=chain_address, change_address="", units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        for c in string.ascii_uppercase:
            asset_name = chain_string + '/' + c

            n0.issue(asset_name=asset_name, qty=1000, to_address=chain_address, change_address="", units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        n0.generate(1)
        self.sync_all()

        chain_assets = n1.listassets(asset="CHAIN2/*", verbose=False)
        assert_equal(len(chain_assets), 26)

        self.log.info("Chaining reissue transactions...")
        address0 = n0.getnewaddress()
        n0.issue(asset_name="CHAIN_REISSUE", qty=1000, to_address=address0, change_address="", units=4, reissuable=True, has_ipfs=False)

        n0.generate(1)
        self.sync_all()

        n0.reissue(asset_name="CHAIN_REISSUE", qty=1000, to_address=address0, change_address="", reissuable=True)
        assert_raises_rpc_error(-4, "Error: The transaction was rejected! Reason given: bad-tx-reissue-chaining-not-allowed", n0.reissue, "CHAIN_REISSUE", 1000, address0, "", True)

        n0.generate(1)
        self.sync_all()

        n0.reissue(asset_name="CHAIN_REISSUE", qty=1000, to_address=address0, change_address="", reissuable=True)

        n0.generate(1)
        self.sync_all()

        assetdata = n0.getassetdata("CHAIN_REISSUE")
        assert_equal(assetdata["name"], "CHAIN_REISSUE")
        assert_equal(assetdata["amount"], 3000)
        assert_equal(assetdata["units"], 4)
        assert_equal(assetdata["reissuable"], 1)
        assert_equal(assetdata["has_ipfs"], 0)

    def ipfs_state(self):
        self.log.info("Checking ipfs hash state changes...")
        n0 = self.nodes[0]

        asset_name1 = "ASSET111"
        asset_name2 = "ASSET222"
        address1 = n0.getnewaddress()
        address2 = n0.getnewaddress()
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        bad_hash = "RncvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"

        ########################################
        # bad hash (isn't a valid multihash sha2-256)
        self.log.info("Testing issue asset with invalid IPFS...")
        try:
            n0.issue(asset_name=asset_name1, qty=1000, to_address=address1, change_address=address2, units=0, reissuable=True, has_ipfs=True, ipfs_hash=bad_hash)
        except JSONRPCException as e:
            if "Invalid IPFS/Txid hash" not in e.error['message']:
                raise AssertionError("Expected substring not found:" + e.error['message'])
        except Exception as e:
            raise AssertionError("Unexpected exception raised: " + type(e).__name__)
        else:
            raise AssertionError("No exception raised")

        ########################################
        # no hash
        self.log.info("Testing issue asset with no IPFS...")
        n0.issue(asset_name=asset_name2, qty=1000, to_address=address1, change_address=address2, units=0, reissuable=True, has_ipfs=False)
        n0.generate(1)
        ad = n0.getassetdata(asset_name2)
        assert_equal(0, ad['has_ipfs'])
        assert_does_not_contain_key('ipfs_hash', ad)

        ########################################
        # reissue w/ bad hash
        self.log.info("Testing re-issue asset with invalid IPFS...")
        try:
            n0.reissue(asset_name=asset_name2, qty=2000, to_address=address1, change_address=address2, reissuable=True, new_units=-1, new_ipfs=bad_hash)
        except JSONRPCException as e:
            if "Invalid IPFS/Txid hash" not in e.error['message']:
                raise AssertionError("Expected substring not found:" + e.error['message'])
        except Exception as e:
            raise AssertionError("Unexpected exception raised: " + type(e).__name__)
        else:
            raise AssertionError("No exception raised")

        ########################################
        # reissue w/ hash
        self.log.info("Testing re-issue asset with valid IPFS...")
        n0.reissue(asset_name=asset_name2, qty=2000, to_address=address1, change_address=address2, reissuable=True, new_units=-1, new_ipfs=ipfs_hash)
        n0.generate(1)
        ad = n0.getassetdata(asset_name2)
        assert_equal(1, ad['has_ipfs'])
        assert_equal(ipfs_hash, ad['ipfs_hash'])

        ########################################
        # invalidate and reconsider
        best = n0.getbestblockhash()
        n0.invalidateblock(n0.getbestblockhash())
        ad = n0.getassetdata(asset_name2)
        assert_equal(0, ad['has_ipfs'])
        assert_does_not_contain_key('ipfs_hash', ad)
        n0.reconsiderblock(best)
        ad = n0.getassetdata(asset_name2)
        assert_equal(1, ad['has_ipfs'])
        assert_equal(ipfs_hash, ad['ipfs_hash'])

    def db_corruption_regression(self):
        self.log.info("Checking db corruption invalidate block...")
        n0 = self.nodes[0]
        asset_name = "DATA_CORRUPT"

        # Test to make sure that undoing a reissue and an issue during a reorg doesn't screw up the database/cache
        n0.issue(asset_name)
        a = n0.generate(1)[0]

        n0.reissue(asset_name, 500, n0.getnewaddress())
        # noinspection PyStatementEffect
        n0.generate(1)[0]

        self.log.info(f"Invalidating {a}...")
        n0.invalidateblock(a)

        assert_equal(0, len(n0.listassets(asset_name, True)))

    def reissue_prec_change(self):
        self.log.info("Testing precision change on reissue...")
        n0 = self.nodes[0]

        asset_name = "PREC_CHANGES"
        address = n0.getnewaddress()

        n0.issue(asset_name, 10, "", "", 0, True, False)
        n0.generate(1)
        assert_equal(0, n0.listassets("*", True)[asset_name]["units"])

        for i in range(0, 8):
            n0.reissue(asset_name, 10.0 ** (-i), address, "", True, i + 1)
            n0.generate(1)
            assert_equal(i + 1, n0.listassets("*", True)[asset_name]["units"])
            assert_raises_rpc_error(-25, "Error: Unable to reissue asset: unit must be larger than current unit selection", n0.reissue, asset_name, 10.0 ** (-i), address, "", True, i)

        n0.reissue(asset_name, 0.00000001, address)
        n0.generate(1)
        assert_equal(Decimal('11.11111111'), n0.listassets("*", True)[asset_name]["amount"])

    def run_test(self):
        self.activate_assets()
        self.big_test()
        self.issue_param_checks()
        self.chain_assets()
        self.ipfs_state()
        self.db_corruption_regression()
        self.reissue_prec_change()


if __name__ == '__main__':
    AssetTest().main()
