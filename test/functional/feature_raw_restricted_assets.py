#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017-2018 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test restricted asset related RPC commands."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import *
import math
from pprint import pprint

BURN_ADDRESSES = {
    'issue_restricted': 'n1issueRestrictedXXXXXXXXXXXXZVT9V'
}

BURN_AMOUNTS = {
    'issue_restricted': 1500
}

FEE_AMOUNT = 0.01

def truncate(number, digits = 8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper

def get_tx_issue_hex(node, to_address, asset_name, \
                     asset_quantity=1000, verifier_string="true", units=0, reissuable=1, has_ipfs=0, \
                     ipfs_hash="", owner_change_address=""):
    change_address = node.getnewaddress()

    rvn_unspent = next(u for u in node.listunspent() if u['amount'] > BURN_AMOUNTS['issue_restricted'])
    rvn_inputs = [{k: rvn_unspent[k] for k in ['txid', 'vout']}]

    owner_asset_name = asset_name[1:] + '!'
    owner_unspent = node.listmyassets(owner_asset_name, True)[owner_asset_name]['outpoints'][0]
    owner_inputs = [{k: owner_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        BURN_ADDRESSES['issue_restricted']: BURN_AMOUNTS['issue_restricted'],
        change_address: truncate(float(rvn_unspent['amount']) - BURN_AMOUNTS['issue_restricted'] - FEE_AMOUNT),
        to_address: {
            'issue_restricted': {
                'asset_name':       asset_name,
                'asset_quantity':   asset_quantity,
                'verifier_string':  verifier_string,
                'units':            units,
                'reissuable':       reissuable,
                'has_ipfs':         has_ipfs
            }
        }
    }
    if has_ipfs == 1:
        outputs[to_address]['issue_restricted']['ipfs_hash'] = ipfs_hash
    if len(owner_change_address) > 0:
        outputs[to_address]['issue_restricted']['owner_change_address'] = owner_change_address

    tx_issue = node.createrawtransaction(rvn_inputs + owner_inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


class RawRestrictedAssetsTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def activate_restricted_assets(self):
        self.log.info("Generating RVN and activating restricted assets...")
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        n1 = self.nodes[1]
        n0.sendtoaddress(n1.getnewaddress(), 2500)
        n0.generate(1)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['restricted_assets']['status'])

    def issue_restricted_test(self):
        self.log.info("Testing raw issue_restricted...")
        n0 = self.nodes[0]

        base_asset_name = "ISSUE_RESTRICTED_TEST"
        asset_name = f"${base_asset_name}"
        qty = 10000
        verifier = "true"
        to_address = n0.getnewaddress()

        n0.issue(base_asset_name)
        n0.generate(1)

        txid = n0.sendrawtransaction(get_tx_issue_hex(n0, to_address, asset_name, qty, verifier))
        n0.generate(1)

        #verify
        assert_equal(64, len(txid))
        assert_equal(qty, n0.listmyassets(asset_name, True)[asset_name]['balance'])
        asset_data = n0.getassetdata(asset_name)
        assert_equal(qty, asset_data['amount'])
        assert_equal(0, asset_data['units'])
        assert_equal(1, asset_data['reissuable'])
        assert_equal(0, asset_data['has_ipfs'])
        assert_equal('true', asset_data['verifier_string'])

    def run_test(self):
        self.activate_restricted_assets()

        self.issue_restricted_test()

if __name__ == '__main__':
    RawRestrictedAssetsTest().main()