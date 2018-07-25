#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the rawtransaction RPCs for asset transactions.
"""
from io import BytesIO

from test_framework.test_framework import RavenTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction

class RawAssetTransactionsTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def issue_transfer_test(self):
        # activate assets
        self.nodes[0].generate(500)
        self.sync_all()

        ########################################
        # issue
        to_address = self.nodes[0].getnewaddress()
        change_address = self.nodes[0].getnewaddress()
        unspent = self.nodes[0].listunspent()[0]
        inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
        outputs = {
            'n1issueAssetXXXXXXXXXXXXXXXXWdnemQ': 500,
            change_address: float(unspent['amount']) - 500.0001,
            to_address: {
                'issue': {
                    'asset_name':       'TEST_ASSET',
                    'asset_quantity':   1000,
                    'units':            0,
                    'reissuable':       0,
                    'has_ipfs':         0,
                }
            }
        }
        tx_issue = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_issue_signed = self.nodes[0].signrawtransaction(tx_issue)
        tx_issue_hash = self.nodes[0].sendrawtransaction(tx_issue_signed['hex'])
        assert_is_hash_string(tx_issue_hash)

        self.nodes[0].generate(10)
        self.sync_all()
        assert_equal(1000, self.nodes[0].listmyassets()['TEST_ASSET'])

        ########################################
        # transfer
        to_address = self.nodes[1].getnewaddress()
        change_asset_address = self.nodes[0].getnewaddress()
        unspent = self.nodes[0].listunspent()[0]
        unspent_asset = self.nodes[0].listmyassets('TEST_ASSET', True)['TEST_ASSET']['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            change_address: float(unspent['amount']) - 0.0001,
            to_address: {
                'transfer': {
                    'TEST_ASSET': 400
                }
            },
            change_asset_address: {
                'transfer': {
                    'TEST_ASSET': float(unspent_asset['amount']) - 400
                }
            }
        }
        tx_transfer = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_transfer_signed = self.nodes[0].signrawtransaction(tx_transfer)

        # try tampering with the sig
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_transfer_signed['hex']))
        tx.deserialize(f)
        script_sig = bytes_to_hex_str(tx.vin[1].scriptSig)
        tampered_sig = script_sig[:-8] + "deadbeef"
        tx.vin[1].scriptSig = hex_str_to_bytes(tampered_sig)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Script failed an OP_EQUALVERIFY operation)",
                                self.nodes[0].sendrawtransaction, tampered_hex)

        tx_transfer_hash = self.nodes[0].sendrawtransaction(tx_transfer_signed['hex'])
        assert_is_hash_string(tx_transfer_hash)

        self.nodes[0].generate(10)
        self.sync_all()
        assert_equal(600, self.nodes[0].listmyassets()['TEST_ASSET'])
        assert_equal(400, self.nodes[1].listmyassets()['TEST_ASSET'])


    def run_test(self):
        self.issue_transfer_test()

if __name__ == '__main__':
    RawAssetTransactionsTest().main()
