#!/usr/bin/env python3
# Copyright (c) 2021 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test pay-to-asset-hash (P2AH) use cases.

P2AH outputs are spendable not by signature, but by moving committed asset
owner tokens (admin assets) through the spending transaction.
"""

import math
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


def truncate(number, digits=8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper


def get_first_unspent(node, needed=500.1):
    # Find the first unspent with enough required for transaction
    for unspent in node.listunspent():
        if float(unspent['amount']) > needed:
            return unspent
    raise AssertionError("No unspent output larger than %s" % needed)


class AssetAuthTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex', '-fallbackfee=0.0001'], ['-assetindex', '-fallbackfee=0.0001']]

    def activate_assets_and_assetauth(self):
        self.log.info("Generating RVN and activating assets + assetauth...")
        n0 = self.nodes[0]
        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assetauth']['status'])

    def test_create_address(self):
        self.log.info("Testing createassetauthaddress...")
        n0 = self.nodes[0]

        n0.issue("ALPHA", 1000)
        n0.generate(1)
        self.sync_all()

        # Basic 1-of-1
        result = n0.createassetauthaddress(1, ["ALPHA!"])
        assert_equal(result['nrequired'], 1)
        assert_equal(result['total'], 1)
        assert_equal(result['owner_assets'], ["ALPHA!"])
        assert(result['address'].startswith('J'))  # regtest P2AH prefix
        assert(len(result['preimage']) > 0)
        assert(len(result['hash']) == 40)

        # Determinism: same inputs -> same address
        result2 = n0.createassetauthaddress(1, ["ALPHA!"])
        assert_equal(result['address'], result2['address'])
        assert_equal(result['preimage'], result2['preimage'])

        # validateaddress recognizes it
        validation = n0.validateaddress(result['address'])
        assert_equal(validation['isvalid'], True)
        assert_equal(validation['isassetauth'], True)

        # getassetauthinfo round-trips through the preimage hex
        info = n0.getassetauthinfo(result['preimage'])
        assert_equal(info['address'], result['address'])
        assert_equal(info['known'], True)

        # Unknown address (no preimage stored, queried by address) -> known: False
        info2 = self.nodes[1].getassetauthinfo(result['address'])
        assert_equal(info2['known'], False)

        # Error cases
        assert_raises_rpc_error(-8, "not a valid owner asset name",
                                n0.createassetauthaddress, 1, ["ALPHA"])  # not an owner token
        assert_raises_rpc_error(-8, "at least one owner asset",
                                n0.createassetauthaddress, 0, ["ALPHA!"])
        assert_raises_rpc_error(-8, "not enough owner assets",
                                n0.createassetauthaddress, 2, ["ALPHA!"])
        assert_raises_rpc_error(-8, "duplicate owner asset name",
                                n0.createassetauthaddress, 1, ["ALPHA!", "ALPHA!"])

    def test_sorted_canonical_address(self):
        self.log.info("Testing canonical sorted addresses...")
        n0 = self.nodes[0]

        n0.issue("SORT_A", 100)
        n0.issue("SORT_B", 100)
        n0.generate(1)
        self.sync_all()

        # Different input order -> same canonical address
        r1 = n0.createassetauthaddress(1, ["SORT_A!", "SORT_B!"])
        r2 = n0.createassetauthaddress(1, ["SORT_B!", "SORT_A!"])
        assert_equal(r1['address'], r2['address'])
        assert_equal(r1['owner_assets'], ["SORT_A!", "SORT_B!"])

    def test_spend_1of1(self):
        self.log.info("Testing 1-of-1 P2AH spend with owner token movement...")
        n0, n1 = self.nodes[0], self.nodes[1]

        # Register the address with the wallet so UTXOs are tracked
        result = n0.addassetauthaddress(1, ["ALPHA!"])
        p2ah_addr = result['address']

        # Fund the P2AH address
        n0.sendtoaddress(p2ah_addr, 50)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah_addr)
        assert_equal(len(utxos), 1)
        assert_equal(float(utxos[0]['amount']), 50.0)

        # Spend from it
        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah_addr, {dest: 25})
        assert_equal(spend['owner_assets_moved'], ["ALPHA!"])
        assert_equal(len(spend['owner_asset_destinations']), 1)

        # The verify RPC reports it as authorized. Verify while the tx is still in the
        # mempool: after mining, its inputs are spent and can't be looked up without prevtxs
        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)
        assert_equal(verify['inputs'][0]['authorized'], True)
        assert_equal(verify['inputs'][0]['moved'], ["ALPHA!"])

        n0.generate(1)
        self.sync_all()

        # Destination received the funds
        assert_equal(float(n1.getreceivedbyaddress(dest)), 25.0)

        # Decode the spend tx: the P2AH input must reveal the preimage
        rawtx = n0.getrawtransaction(spend['txid'], 1)
        preimage_inputs = [vin for vin in rawtx['vin'] if 'assetAuthPreimage' in vin]
        assert_equal(len(preimage_inputs), 1)
        assert_equal(preimage_inputs[0]['assetAuthPreimage']['owner_assets'], ["ALPHA!"])

    def test_spend_without_movement_rejected(self):
        self.log.info("Testing that spending without owner token movement is rejected...")
        n0 = self.nodes[0]

        result = n0.getassetauthinfo(n0.createassetauthaddress(1, ["ALPHA!"])['preimage'])
        p2ah_addr = result['address']
        preimage = result['preimage']

        # The change of the previous spend (or fund a fresh UTXO)
        n0.sendtoaddress(p2ah_addr, 10)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah_addr)
        utxo = utxos[0]

        # Build a raw tx that spends ONLY the P2AH UTXO (no ALPHA! input/output)
        dest = n0.getnewaddress()
        send_amount = truncate(float(utxo['amount']) - 0.05)
        inputs = [{'txid': utxo['txid'], 'vout': utxo['vout']}]
        outputs = {dest: send_amount}
        rawtx = n0.createrawtransaction(inputs, outputs)

        # Sign with the preimage: script-level signing succeeds (this is what old nodes see)
        prev_tx = n0.getrawtransaction(utxo['txid'], 1)
        spk = prev_tx['vout'][utxo['vout']]['scriptPubKey']['hex']
        prevtxs = [{'txid': utxo['txid'], 'vout': utxo['vout'], 'scriptPubKey': spk,
                    'assetAuthPreimage': preimage, 'amount': float(utxo['amount'])}]
        signed = n0.signrawtransaction(rawtx, prevtxs)
        assert_equal(signed['complete'], True)

        # The verify RPC reports it as NOT authorized
        verify = n0.verifyassetauth(signed['hex'])
        assert_equal(verify['valid'], False)
        assert_equal(verify['inputs'][0]['authorized'], False)
        assert_equal(verify['inputs'][0]['moved'], [])

        # Consensus rejects it
        assert_raises_rpc_error(-26, "bad-txns-assetauth-insufficient-owner-movement",
                                n0.sendrawtransaction, signed['hex'])

    def test_spend_with_wrong_token_rejected(self):
        self.log.info("Testing that the wrong owner token can't authorize a spend...")
        n0 = self.nodes[0]

        n0.issue("WRONG", 100)
        n0.generate(1)
        self.sync_all()

        # P2AH committed to ALPHA!; try to authorize with WRONG!
        utxos = n0.listassetauthutxos(n0.createassetauthaddress(1, ["ALPHA!"])['address'])
        assert(len(utxos) > 0)
        utxo = utxos[0]
        preimage = n0.createassetauthaddress(1, ["ALPHA!"])['preimage']

        # WRONG! owner token UTXO
        wrong_outpoint = n0.listmyassets("WRONG!", True)["WRONG!"]['outpoints'][0]

        dest = n0.getnewaddress()
        wrong_dest = n0.getnewaddress()
        fee_unspent = get_first_unspent(n0, 10)

        send_amount = truncate(float(utxo['amount']) - 0.01)
        change_amount = truncate(float(fee_unspent['amount']) - 0.1)

        inputs = [
            {'txid': utxo['txid'], 'vout': utxo['vout']},
            {'txid': wrong_outpoint['txid'], 'vout': wrong_outpoint['vout']},
            {'txid': fee_unspent['txid'], 'vout': fee_unspent['vout']},
        ]
        outputs = {
            dest: send_amount,
            wrong_dest: {'transfer': {'WRONG!': 1}},
            n0.getnewaddress(): change_amount,
        }
        rawtx = n0.createrawtransaction(inputs, outputs)

        prev_tx = n0.getrawtransaction(utxo['txid'], 1)
        spk = prev_tx['vout'][utxo['vout']]['scriptPubKey']['hex']
        prevtxs = [{'txid': utxo['txid'], 'vout': utxo['vout'], 'scriptPubKey': spk,
                    'assetAuthPreimage': preimage, 'amount': float(utxo['amount'])}]
        signed = n0.signrawtransaction(rawtx, prevtxs)
        assert_equal(signed['complete'], True)

        # WRONG! moves, but it's not the committed owner asset -> rejected
        assert_raises_rpc_error(-26, "bad-txns-assetauth-insufficient-owner-movement",
                                n0.sendrawtransaction, signed['hex'])

    def test_m_of_n(self):
        self.log.info("Testing 2-of-3 multisig P2AH...")
        n0, n1 = self.nodes[0], self.nodes[1]

        n0.issue("BOARD_A", 100)
        n0.issue("BOARD_B", 100)
        n0.issue("BOARD_C", 100)
        n0.generate(1)
        self.sync_all()

        result = n0.addassetauthaddress(2, ["BOARD_A!", "BOARD_B!", "BOARD_C!"])
        p2ah_addr = result['address']
        assert_equal(result['nrequired'], 2)
        assert_equal(result['total'], 3)

        n0.sendtoaddress(p2ah_addr, 10)
        n0.generate(1)
        self.sync_all()

        # The wallet holds all three owner tokens; it should use exactly 2
        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah_addr, {dest: 9})
        assert_equal(len(spend['owner_assets_moved']), 2)

        n0.generate(1)
        self.sync_all()
        assert_equal(float(n1.getreceivedbyaddress(dest)), 9.0)

    def test_p2ah_holding_assets(self):
        self.log.info("Testing P2AH outputs holding assets...")
        n0, n1 = self.nodes[0], self.nodes[1]

        n0.issue("TREASURY", 1000)
        n0.issue("FUNDS", 5000)
        n0.generate(1)
        self.sync_all()

        # Hold FUNDS at a P2AH address controlled by TREASURY!
        result = n0.addassetauthaddress(1, ["TREASURY!"])
        p2ah_addr = result['address']

        n0.transfer("FUNDS", 500, p2ah_addr)
        n0.generate(1)
        self.sync_all()

        # The asset shows up at the P2AH address
        utxos = n0.listassetauthutxos(p2ah_addr)
        asset_utxos = [u for u in utxos if 'asset' in u]
        assert_equal(len(asset_utxos), 1)
        assert_equal(asset_utxos[0]['asset']['name'], "FUNDS")
        assert_equal(float(asset_utxos[0]['asset']['amount']), 500.0)

        # Asset balance is tracked under the P2AH address
        assert_equal(float(n0.listassetbalancesbyaddress(p2ah_addr)["FUNDS"]), 500.0)

        # Spend the asset out of the P2AH address (requires TREASURY! movement)
        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah_addr, {dest: {'transfer': {'FUNDS': 200}}})
        assert_equal(spend['owner_assets_moved'], ["TREASURY!"])

        n0.generate(1)
        self.sync_all()

        # n1 received the asset
        assert_equal(float(n1.listmyassets("FUNDS")["FUNDS"]), 200.0)

    def test_chaining(self):
        self.log.info("Testing chained P2AH authorization via spendassetauth...")
        n0, n1 = self.nodes[0], self.nodes[1]

        # Setup:
        #   ROOT! at a key address (wallet)
        #   LEAF! held AT P2AH(ROOT!)
        #   5 RVN held at P2AH(LEAF!)
        n0.issue("ROOT", 100)
        n0.issue("LEAF", 100)
        n0.generate(1)
        self.sync_all()

        p2ah_root = n0.addassetauthaddress(1, ["ROOT!"])
        p2ah_leaf = n0.addassetauthaddress(1, ["LEAF!"])

        n0.transfer("LEAF!", 1, p2ah_root['address'])
        n0.sendtoaddress(p2ah_leaf['address'], 5)
        n0.generate(1)
        self.sync_all()

        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah_leaf['address'], {dest: 4.9})
        assert_equal(spend['owner_assets_moved'], ["ROOT!", "LEAF!"])

        moved = dict(zip(spend['owner_assets_moved'], spend['owner_asset_destinations']))
        assert moved['ROOT!'] != p2ah_root['address']
        assert_equal(moved['LEAF!'], p2ah_root['address'])

        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)
        for inp in verify['inputs']:
            assert_equal(inp['authorized'], True)

        n0.generate(1)
        self.sync_all()
        assert_equal(float(n1.getreceivedbyaddress(dest)), 4.9)

        # LEAF! is back on P2AH(ROOT!) for another chained spend
        leaf_utxos = [u for u in n0.listassetauthutxos(p2ah_root['address'])
                      if 'asset' in u and u['asset']['name'] == 'LEAF!']
        assert_equal(len(leaf_utxos), 1)

    def test_chain_without_root_rejected(self):
        self.log.info("Testing that a chain without its authorization root is rejected...")
        n0 = self.nodes[0]

        # LEAF! is on P2AH(ROOT!) after the chain test. Try to move it WITHOUT moving ROOT!
        p2ah_root = n0.getassetauthinfo(n0.createassetauthaddress(1, ["ROOT!"])['preimage'])

        leaf_utxo = [u for u in n0.listassetauthutxos(p2ah_root['address'])
                     if 'asset' in u and u['asset']['name'] == 'LEAF!'][0]
        fee_unspent = get_first_unspent(n0, 10)

        leaf_dest = n0.getnewaddress()
        change = n0.getnewaddress()
        change_amount = truncate(float(fee_unspent['amount']) - 0.1)

        inputs = [
            {'txid': leaf_utxo['txid'], 'vout': leaf_utxo['vout']},
            {'txid': fee_unspent['txid'], 'vout': fee_unspent['vout']},
        ]
        outputs = {
            leaf_dest: {'transfer': {'LEAF!': 1}},
            change: change_amount,
        }
        rawtx = n0.createrawtransaction(inputs, outputs)

        leaf_spk = n0.getrawtransaction(leaf_utxo['txid'], 1)['vout'][leaf_utxo['vout']]['scriptPubKey']['hex']
        prevtxs = [{'txid': leaf_utxo['txid'], 'vout': leaf_utxo['vout'], 'scriptPubKey': leaf_spk,
                    'assetAuthPreimage': p2ah_root['preimage'], 'amount': 0}]
        signed = n0.signrawtransaction(rawtx, prevtxs)
        assert_equal(signed['complete'], True)

        # LEAF! moves but ROOT! does not -> the P2AH(ROOT!) input is unauthorized
        assert_raises_rpc_error(-26, "bad-txns-assetauth-insufficient-owner-movement",
                                n0.sendrawtransaction, signed['hex'])

    def test_multiple_p2ah_inputs_one_authorization(self):
        self.log.info("Testing multiple P2AH inputs authorized by one owner token movement...")
        n0 = self.nodes[0]

        n0.issue("MULTI", 100)
        n0.generate(1)
        self.sync_all()

        result = n0.addassetauthaddress(1, ["MULTI!"])
        p2ah_addr = result['address']

        # Create two separate UTXOs at the same P2AH address
        n0.sendtoaddress(p2ah_addr, 5)
        n0.sendtoaddress(p2ah_addr, 7)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah_addr)
        assert_equal(len(utxos), 2)

        # One spendassetauth call spends both UTXOs with a single MULTI! movement
        dest = n0.getnewaddress()
        spend = n0.spendassetauth(p2ah_addr, {dest: 11.5})
        assert_equal(spend['owner_assets_moved'], ["MULTI!"])

        n0.generate(1)
        self.sync_all()

        # Both UTXOs were consumed
        rawtx = n0.getrawtransaction(spend['txid'], 1)
        p2ah_inputs = [vin for vin in rawtx['vin'] if 'assetAuthPreimage' in vin]
        assert_equal(len(p2ah_inputs), 2)

    def test_wallet_persistence(self):
        self.log.info("Testing wallet preimage persistence across restart...")
        n0 = self.nodes[0]

        n0.issue("PERSIST", 100)
        n0.generate(1)
        self.sync_all()

        result = n0.addassetauthaddress(1, ["PERSIST!"])
        p2ah_addr = result['address']

        # Restart the node
        self.restart_node(0, extra_args=self.extra_args[0])

        # The preimage is still known after restart
        info = n0.getassetauthinfo(p2ah_addr)
        assert_equal(info['known'], True)
        assert_equal(info['owner_assets'], ["PERSIST!"])

    def test_pre_activation_rejection(self):
        # Note: this runs FIRST, before activation
        self.log.info("Testing that P2AH outputs are rejected before activation...")
        n0 = self.nodes[0]

        n0.generate(1)
        self.sync_all()

        # createassetauthaddress is a pure function and works before activation
        # (using a syntactically valid owner name; the asset doesn't need to exist)
        result = n0.createassetauthaddress(1, ["PREACTIVATION!"])
        p2ah_addr = result['address']

        # But sending to a P2AH address is rejected by policy/consensus before activation
        assert_raises_rpc_error(None, None, n0.sendtoaddress, p2ah_addr, 1)

    def run_test(self):
        self.test_pre_activation_rejection()
        self.activate_assets_and_assetauth()
        self.test_create_address()
        self.test_sorted_canonical_address()
        self.test_spend_1of1()
        self.test_spend_without_movement_rejected()
        self.test_spend_with_wrong_token_rejected()
        self.test_m_of_n()
        self.test_p2ah_holding_assets()
        self.test_chaining()
        self.test_chain_without_root_rejected()
        self.test_multiple_p2ah_inputs_one_authorization()
        self.test_wallet_persistence()


if __name__ == '__main__':
    AssetAuthTest().main()
