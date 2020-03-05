#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test the rawtransaction RPCs for asset transactions.
"""

import math
from io import BytesIO
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, assert_is_hash_string, assert_does_not_contain_key, assert_contains_key, assert_contains_pair
from test_framework.mininode import CTransaction, hex_str_to_bytes, bytes_to_hex_str, CScriptReissue, CScriptOwner, CScriptTransfer, CTxOut, CScriptIssue

def truncate(number, digits=8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper


# noinspection PyTypeChecker,PyUnboundLocalVariable,PyUnresolvedReferences
def get_first_unspent(self: object, node: object, needed: float = 500.1) -> object:
    # Find the first unspent with enough required for transaction
    for n in range(0, len(node.listunspent())):
        unspent = node.listunspent()[n]
        if float(unspent['amount']) > needed:
            self.log.info("Found unspent index %d with more than %s available.", n, needed)
            return unspent
    assert (float(unspent['amount']) < needed)


def get_tx_issue_hex(self, node, asset_name, asset_quantity, asset_units=0):
    to_address = node.getnewaddress()
    change_address = node.getnewaddress()
    unspent = get_first_unspent(self, node)
    inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
    outputs = {
        'n1issueAssetXXXXXXXXXXXXXXXXWdnemQ': 500,
        change_address: truncate(float(unspent['amount']) - 500.1),
        to_address: {
            'issue': {
                'asset_name':       asset_name,
                'asset_quantity':   asset_quantity,
                'units':            asset_units,
                'reissuable':       1,
                'has_ipfs':         0,
            }
        }
    }

    tx_issue = node.createrawtransaction(inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


# noinspection PyTypeChecker
class RawAssetTransactionsTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def activate_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0 = self.nodes[0]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])

    def reissue_tampering_test(self):
        self.log.info("Tampering with raw reissues...")

        n0 = self.nodes[0]

        ########################################
        # issue a couple of assets
        asset_name = 'REISSUE_TAMPERING'
        owner_name = f"{asset_name}!"
        alternate_asset_name = 'ANOTHER_ASSET'
        alternate_owner_name = f"{alternate_asset_name}!"
        n0.sendrawtransaction(get_tx_issue_hex(self, n0, asset_name, 1000))
        n0.sendrawtransaction(get_tx_issue_hex(self, n0, alternate_asset_name, 1000))
        n0.generate(1)

        ########################################
        # try a reissue with no owner input
        to_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            'n1ReissueAssetXXXXXXXXXXXXXXWG9NLd': 100,
            n0.getnewaddress(): truncate(float(unspent['amount']) - 100.1),
            to_address: {
                'reissue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1000,
                }
            }
        }
        tx_hex = n0.createrawtransaction(inputs, outputs)
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for asset that you don't have: {owner_name}",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # try a reissue where the owner input doesn't match the asset name
        unspent_asset_owner = n0.listmyassets(alternate_owner_name, True)[alternate_owner_name]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]
        tx_hex = n0.createrawtransaction(inputs, outputs)
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for asset that you don't have: {owner_name}",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # fix it to use the right input
        unspent_asset_owner = n0.listmyassets(owner_name, True)[owner_name]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]
        tx_hex = n0.createrawtransaction(inputs, outputs)

        ########################################
        # try tampering to change the name of the asset being issued
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnr = '72766e72'  # rvnr
        op_drop = '75'
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnr in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                reissue_script_hex = script_hex[script_hex.index(rvnr) + len(rvnr):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(reissue_script_hex))
                reissue = CScriptReissue()
                reissue.deserialize(f)
                reissue.name = alternate_asset_name.encode()
                tampered_reissue = bytes_to_hex_str(reissue.serialize())
                tampered_script = script_hex[:script_hex.index(rvnr)] + rvnr + tampered_reissue + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_hex_bad = bytes_to_hex_str(tx.serialize())
        tx_signed = n0.signrawtransaction(tx_hex_bad)['hex']
        assert_raises_rpc_error(-26, "bad-txns-reissue-owner-outpoint-not-found", n0.sendrawtransaction, tx_signed)

        ########################################
        # try tampering to remove owner output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: rvnt not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_hex_bad = bytes_to_hex_str(tx.serialize())
        tx_signed = n0.signrawtransaction(tx_hex_bad)['hex']
        assert_raises_rpc_error(-26, "bad-txns-reissue-owner-outpoint-not-found",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # try tampering to remove asset output...
        # ...this is actually ok, just an awkward donation to reissue burn address!

        ########################################
        # reissue!
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        tx_hash = n0.sendrawtransaction(tx_signed)
        assert_is_hash_string(tx_hash)
        n0.generate(1)
        assert_equal(2000, n0.listmyassets(asset_name)[asset_name])

    def issue_tampering_test(self):
        self.log.info("Tampering with raw issues...")

        n0 = self.nodes[0]

        ########################################
        # get issue tx
        asset_name = 'TAMPER_TEST_ASSET'
        tx_issue_hex = get_tx_issue_hex(self, n0, asset_name, 1000)

        ########################################
        # try tampering to issue an asset with no owner output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        rvno = '72766e6f'  # rvno
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: rvno not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-asset-transaction",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue an asset with duplicate owner outputs
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        rvno = '72766e6f'  # rvno
        # find the owner output from vout and insert a duplicate back in
        owner_vout = list(filter(lambda out_script: rvno in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))[0]
        tx.vout.insert(-1, owner_vout)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-asset-formatting-check",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue an owner token with no asset
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        rvnq = '72766e71'  # rvnq
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: rvnq not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-asset-transaction",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue a mismatched owner/asset
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        rvno = '72766e6f'  # rvno
        op_drop = '75'
        # change the owner name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvno in bytes_to_hex_str(out.scriptPubKey):
                owner_out = out
                owner_script_hex = bytes_to_hex_str(owner_out.scriptPubKey)
                asset_script_hex = owner_script_hex[owner_script_hex.index(rvno) + len(rvno):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(asset_script_hex))
                owner = CScriptOwner()
                owner.deserialize(f)
                owner.name = b"NOT_MY_ASSET!"
                tampered_owner = bytes_to_hex_str(owner.serialize())
                tampered_script = owner_script_hex[:owner_script_hex.index(rvno)] + rvno + tampered_owner + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-owner-name-doesn't-match",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to make owner output script invalid
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        rvno = '72766e6f'  # rvno
        RVNO = '52564e4f'  # RVNO
        # change the owner output script type to be invalid
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvno in bytes_to_hex_str(out.scriptPubKey):
                owner_script_hex = bytes_to_hex_str(out.scriptPubKey)
                tampered_script = owner_script_hex.replace(rvno, RVNO)
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-op-rvn-asset-not-in-right-script-location",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try to generate and issue an asset that already exists
        tx_duplicate_issue_hex = get_tx_issue_hex(self, n0, asset_name, 42)
        n0.sendrawtransaction(tx_issue_hex)
        n0.generate(1)
        assert_raises_rpc_error(-8, f"Invalid parameter: asset_name '{asset_name}' has already been used", get_tx_issue_hex, self, n0, asset_name, 55)
        assert_raises_rpc_error(-25, f"Missing inputs", n0.sendrawtransaction, tx_duplicate_issue_hex)

    def issue_reissue_transfer_test(self):
        self.log.info("Doing a big issue-reissue-transfer test...")
        n0, n1 = self.nodes[0], self.nodes[1]

        ########################################
        # issue
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
        outputs = {
            'n1issueAssetXXXXXXXXXXXXXXXXWdnemQ': 500,
            change_address: truncate(float(unspent['amount']) - 500.1),
            to_address: {
                'issue': {
                    'asset_name':       'TEST_ASSET',
                    'asset_quantity':   1000,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         0,
                }
            }
        }
        tx_issue = n0.createrawtransaction(inputs, outputs)
        tx_issue_signed = n0.signrawtransaction(tx_issue)
        tx_issue_hash = n0.sendrawtransaction(tx_issue_signed['hex'])
        assert_is_hash_string(tx_issue_hash)
        self.log.info("issue tx: " + tx_issue_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(1000, n0.listmyassets('TEST_ASSET')['TEST_ASSET'])
        assert_equal(1, n0.listmyassets('TEST_ASSET!')['TEST_ASSET!'])

        ########################################
        # reissue
        unspent = get_first_unspent(self, n0)
        unspent_asset_owner = n0.listmyassets('TEST_ASSET!', True)['TEST_ASSET!']['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]

        outputs = {
            'n1ReissueAssetXXXXXXXXXXXXXXWG9NLd': 100,
            change_address: truncate(float(unspent['amount']) - 100.1),
            to_address: {
                'reissue': {
                    'asset_name':       'TEST_ASSET',
                    'asset_quantity':   1000,
                }
            }
        }

        tx_reissue = n0.createrawtransaction(inputs, outputs)
        tx_reissue_signed = n0.signrawtransaction(tx_reissue)
        tx_reissue_hash = n0.sendrawtransaction(tx_reissue_signed['hex'])
        assert_is_hash_string(tx_reissue_hash)
        self.log.info("reissue tx: " + tx_reissue_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(2000, n0.listmyassets('TEST_ASSET')['TEST_ASSET'])
        assert_equal(1, n0.listmyassets('TEST_ASSET!')['TEST_ASSET!'])

        self.sync_all()

        ########################################
        # transfer
        remote_to_address = n1.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset = n0.listmyassets('TEST_ASSET', True)['TEST_ASSET']['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.1),
            remote_to_address: {
                'transfer': {
                    'TEST_ASSET': 400
                }
            },
            to_address: {
                'transfer': {
                    'TEST_ASSET': truncate(float(unspent_asset['amount']) - 400, 0)
                }
            }
        }
        tx_transfer = n0.createrawtransaction(inputs, outputs)
        tx_transfer_signed = n0.signrawtransaction(tx_transfer)
        tx_hex = tx_transfer_signed['hex']

        ########################################
        # try tampering with the sig
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        script_sig = bytes_to_hex_str(tx.vin[1].scriptSig)
        tampered_sig = script_sig[:-8] + "deadbeef"
        tx.vin[1].scriptSig = hex_str_to_bytes(tampered_sig)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Script failed an OP_EQUALVERIFY operation)",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering with the asset script
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'
        # change asset outputs from 400,600 to 500,500
        for i in range(1, 3):
            script_hex = bytes_to_hex_str(tx.vout[i].scriptPubKey)
            f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(rvnt) + len(rvnt):-len(op_drop)]))
            transfer = CScriptTransfer()
            transfer.deserialize(f)
            transfer.amount = 50000000000
            tampered_transfer = bytes_to_hex_str(transfer.serialize())
            tampered_script = script_hex[:script_hex.index(rvnt)] + rvnt + tampered_transfer + op_drop
            tx.vout[i].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)", n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering with asset outs so ins and outs don't add up
        for n in (-20, -2, -1, 1, 2, 20):
            bad_outputs = {
                change_address: truncate(float(unspent['amount']) - 0.0001),
                remote_to_address: {
                    'transfer': {
                        'TEST_ASSET': 400
                    }
                },
                to_address: {
                    'transfer': {
                        'TEST_ASSET': float(unspent_asset['amount']) - (400 + n)
                    }
                }
            }
            tx_bad_transfer = n0.createrawtransaction(inputs, bad_outputs)
            tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)
            tx_bad_hex = tx_bad_transfer_signed['hex']
            assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - Assets would be burnt TEST_ASSET", n0.sendrawtransaction, tx_bad_hex)

        ########################################
        # try tampering with asset outs so they don't use proper units
        for n in (-0.1, -0.00000001, 0.1, 0.00000001):
            bad_outputs = {
                change_address: truncate(float(unspent['amount']) - 0.0001),
                remote_to_address: {
                    'transfer': {
                        'TEST_ASSET': (400 + n)
                    }
                },
                to_address: {
                    'transfer': {
                        'TEST_ASSET': float(unspent_asset['amount']) - (400 + n)
                    }
                }
            }
            tx_bad_transfer = n0.createrawtransaction(inputs, bad_outputs)
            tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)
            tx_bad_hex = tx_bad_transfer_signed['hex']
            assert_raises_rpc_error(-26, "bad-txns-transfer-asset-amount-not-match-units",
                                    n0.sendrawtransaction, tx_bad_hex)

        ########################################
        # try tampering to change the output asset name to one that doesn't exist
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'
        # change asset name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnt in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(rvnt) + len(rvnt):-len(op_drop)]))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.name = b"ASSET_DOES_NOT_EXIST"
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = script_hex[:script_hex.index(rvnt)] + rvnt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-txns-transfer-asset-not-exist",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering to change the output asset name to one that exists
        alternate_asset_name = "ALTERNATE"
        n0.issue(alternate_asset_name)
        n0.generate(1)
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'
        # change asset name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnt in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(rvnt) + len(rvnt):-len(op_drop)]))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.name = alternate_asset_name.encode()
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = script_hex[:script_hex.index(rvnt)] + rvnt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for asset that you don't have: {alternate_asset_name}",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering to remove the asset output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        # remove the transfer output from vout
        bad_vout = list(filter(lambda out_script: rvnt not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-tx-asset-inputs-size-does-not-match-outputs-size",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # send the good transfer
        tx_transfer_hash = n0.sendrawtransaction(tx_hex)
        assert_is_hash_string(tx_transfer_hash)
        self.log.info("transfer tx: " + tx_transfer_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(1600, n0.listmyassets('TEST_ASSET')['TEST_ASSET'])
        assert_equal(1, n0.listmyassets('TEST_ASSET!')['TEST_ASSET!'])
        assert_equal(400, n1.listmyassets('TEST_ASSET')['TEST_ASSET'])

    def unique_assets_test(self):
        self.log.info("Testing unique assets...")
        n0 = self.nodes[0]

        bad_burn = "n1BurnXXXXXXXXXXXXXXXXXXXXXXU1qejP"
        unique_burn = "n1issueUniqueAssetXXXXXXXXXXS4695i"

        root = "RINGU"
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(10)

        asset_tags = ["myprecious1", "bind3", "gold7", "men9"]
        ipfs_hashes = ["QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"] * len(asset_tags)

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset_owner = n0.listmyassets(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 5 * len(asset_tags)

        ############################################
        # try first with bad burn address
        outputs = {
            bad_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue_unique': {
                    'root_name':    root,
                    'asset_tags':   asset_tags,
                    'ipfs_hashes':  ipfs_hashes,
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-unique-asset-burn-outpoints-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to proper burn address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue_unique': {
                    'root_name':    root,
                    'asset_tags':   asset_tags,
                    'ipfs_hashes':  ipfs_hashes,
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        for tag in asset_tags:
            asset_name = f"{root}#{tag}"
            assert_equal(1, n0.listmyassets()[asset_name])
            assert_equal(1, n0.listassets(asset_name, True)[asset_name]['has_ipfs'])
            assert_equal(ipfs_hashes[0], n0.listassets(asset_name, True)[asset_name]['ipfs_hash'])

    def unique_assets_via_issue_test(self):
        self.log.info("Testing unique assets via issue...")
        n0 = self.nodes[0]

        root = "RINGU2"
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(1)

        asset_name = f"{root}#unique"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset_owner = n0.listmyassets(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 5
        outputs = {
            'n1issueUniqueAssetXXXXXXXXXXS4695i': burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   100000000,
                    'units':            0,
                    'reissuable':       0,
                    'has_ipfs':         0,
                }
            },
        }

        ########################################
        # bad qty
        for n in (2, 20, 20000):
            outputs[to_address]['issue']['asset_quantity'] = n
            assert_raises_rpc_error(-8, "Invalid parameter: amount must be 100000000", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['asset_quantity'] = 1

        ########################################
        # bad units
        for n in (1, 2, 3, 4, 5, 6, 7, 8):
            outputs[to_address]['issue']['units'] = n
            assert_raises_rpc_error(-8, "Invalid parameter: units must be 0", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['units'] = 0

        ########################################
        # reissuable
        outputs[to_address]['issue']['reissuable'] = 1
        assert_raises_rpc_error(-8, "Invalid parameter: reissuable must be 0", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['reissuable'] = 0

        ########################################
        # ok
        hex_data = n0.signrawtransaction(n0.createrawtransaction(inputs, outputs))['hex']
        n0.sendrawtransaction(hex_data)
        n0.generate(1)
        assert_equal(1, n0.listmyassets()[root])
        assert_equal(1, n0.listmyassets()[asset_name])
        assert_equal(1, n0.listmyassets()[owner])

    def bad_ipfs_hash_test(self):
        self.log.info("Testing bad ipfs_hash...")
        n0 = self.nodes[0]

        asset_name = 'SOME_OTHER_ASSET_3'
        owner = f"{asset_name}!"
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        bad_hash = "RncvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"

        ########################################
        # issue
        inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
        outputs = {
            'n1issueAssetXXXXXXXXXXXXXXXXWdnemQ': 500,
            change_address: truncate(float(unspent['amount']) - 500.0001),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        bad_hash,
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: ipfs_hash is not valid, or txid hash is not the right length", n0.createrawtransaction, inputs, outputs)

        ########################################
        # reissue
        n0.issue(asset_name=asset_name, qty=1000, to_address=to_address, change_address=change_address, units=0, reissuable=True, has_ipfs=False)
        n0.generate(1)
        unspent_asset_owner = n0.listmyassets(owner, True)[owner]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            'n1ReissueAssetXXXXXXXXXXXXXXWG9NLd': 100,
            change_address: truncate(float(unspent['amount']) - 100.0001),
            to_address: {
                'reissue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1000,
                    'ipfs_hash':        bad_hash,
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: ipfs_hash is not valid, or txid hash is not the right length", n0.createrawtransaction, inputs, outputs)

    def issue_invalid_address_test(self):
        self.log.info("Testing issue with invalid burn and address...")
        n0 = self.nodes[0]

        unique_burn = "n1issueUniqueAssetXXXXXXXXXXS4695i"
        issue_burn = "n1issueAssetXXXXXXXXXXXXXXXXWdnemQ"
        sub_burn = "n1issueSubAssetXXXXXXXXXXXXXbNiH6v"

        asset_name = "BURN_TEST"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # start with invalid burn amount and valid address
        burn = 499
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to invalid burn amount again
        burn = 501
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn amount, but sending it to the sub asset burn address
        burn = 500
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to unique address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn address, and valid burn amount
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

    def issue_sub_invalid_address_test(self):
        self.log.info("Testing issue sub invalid amount and address...")
        n0 = self.nodes[0]

        unique_burn = "n1issueUniqueAssetXXXXXXXXXXS4695i"
        issue_burn = "n1issueAssetXXXXXXXXXXXXXXXXWdnemQ"
        reissue_burn = "n1ReissueAssetXXXXXXXXXXXXXXWG9NLd"
        sub_burn = "n1issueSubAssetXXXXXXXXXXXXXbNiH6v"

        asset_name = "ISSUE_SUB_INVALID"
        owner = f"{asset_name}!"
        n0.issue(asset_name)
        n0.generate(1)
        self.sync_all()

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset_owner = n0.listmyassets(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 99
        asset_name_sub = asset_name + '/SUB1'

        ############################################
        # try first with bad burn amount
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to invalid burn amount again
        burn = 101
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn amount, but sending it to the issue asset burn address
        burn = 100
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to reissue address, should be invalid because it needs to be sub asset burn address
        outputs = {
            reissue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to unique address, should be invalid because it needs to be sub asset burn address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn address, and valid burn amount
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

    def issue_multiple_outputs_test(self):
        self.log.info("Testing issue with extra issues in the tx...")
        n0 = self.nodes[0]

        issue_burn = "n1issueAssetXXXXXXXXXXXXXXXXWdnemQ"

        asset_name = "ISSUE_MULTIPLE_TEST"
        asset_name_multiple = "ISSUE_MULTIPLE_TEST_2"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)

        multiple_to_address = n0.getnewaddress()

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # Try tampering with an asset by adding another issue
        burn = 500
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.001)),
            multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-asset-formatting-check", n0.sendrawtransaction, signed_hex)

    def issue_sub_multiple_outputs_test(self):
        self.log.info("Testing issue with an extra sub asset issue in the tx...")
        n0 = self.nodes[0]

        issue_burn = "n1issueAssetXXXXXXXXXXXXXXXXWdnemQ"
        sub_burn =   "n1issueSubAssetXXXXXXXXXXXXXbNiH6v"

        # create the root asset that the sub asset will try to be created from
        root = "ISSUE_SUB_MULTIPLE_TEST"
        asset_name_multiple_sub = root + '/SUB'
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(1)
        self.sync_all()

        asset_name = "ISSUE_MULTIPLE_TEST"

        to_address = n0.getnewaddress()
        sub_multiple_to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset_owner = n0.listmyassets(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset_owner[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # Try tampering with an asset transaction by having a sub asset issue hidden in the transaction
        burn = 500
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            },
            to_address: {
                'issue': {
                    'asset_name':       asset_name,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-asset-formatting-check", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub asset transaction by having another owner token transfer
        burn = 100
        second_owner_change_address = n0.getnewaddress()
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            second_owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - Assets would be burnt ISSUE_SUB_MULTIPLE_TEST!", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub asset transaction by not having any owner change
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-new-asset-missing-owner-asset", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub asset transaction by not having any owner change
        self.log.info("Testing issue sub asset and tampering with the owner change...")
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-new-asset-missing-owner-asset", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue by changing the owner amount transferred to 2
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 2,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-transfer-owner-amount-was-not-1", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue by changing the owner amount transferred to 0
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 0,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: asset amount can't be equal to or less than zero.", n0.createrawtransaction, inputs, outputs)

        # Create the valid sub asset and broadcast the transaction
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'asset_name':       asset_name_multiple_sub,
                    'asset_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }

        tx_issue_sub_hex = n0.createrawtransaction(inputs, outputs)

        ############################################
        # try tampering to issue sub asset a mismatched the transfer amount to 0
        self.log.info("Testing issue sub asset tamper with the owner change transfer amount...")
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_sub_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'
        # change the transfer amount
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)
                asset_script_hex = transfer_script_hex[transfer_script_hex.index(rvnt) + len(rvnt):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(asset_script_hex))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.amount = 0
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = transfer_script_hex[:transfer_script_hex.index(rvnt)] + rvnt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_transfer = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)['hex']
        assert_raises_rpc_error(-26, "bad-txns-transfer-owner-amount-was-not-1", n0.sendrawtransaction, tx_bad_transfer_signed)

        # Sign and create the valid sub asset transaction
        signed_hex = n0.signrawtransaction(tx_issue_sub_hex)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        assert_equal(1, n0.listmyassets()[root])
        assert_equal(1, n0.listmyassets()[asset_name_multiple_sub])
        assert_equal(1, n0.listmyassets()[asset_name_multiple_sub + '!'])
        assert_equal(1, n0.listmyassets()[owner])

    def transfer_asset_tampering_test(self):
        self.log.info("Testing transfer of asset transaction tampering...")
        n0, n1 = self.nodes[0], self.nodes[1]

        ########################################
        # create the root asset that the sub asset will try to be created from
        root = "TRANSFER_TX_TAMPERING"
        n0.issue(root, 10)
        n0.generate(1)
        self.sync_all()

        to_address = n1.getnewaddress()
        change_address = n0.getnewaddress()
        asset_change = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_asset = n0.listmyassets(root, True)[root]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset[k] for k in ['txid', 'vout']},
        ]

        ########################################
        # Create the valid transfer outputs
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.01),
            to_address: {
                'transfer': {
                    root: 4,
                }
            },
            asset_change: {
                'transfer': {
                    root: 6,
                }
            }
        }

        tx_transfer_hex = n0.createrawtransaction(inputs, outputs)

        ########################################
        # try tampering to issue sub asset a mismatched the transfer amount to 0
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_transfer_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'
        # change the transfer amounts = 0
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)
                asset_script_hex = transfer_script_hex[transfer_script_hex.index(rvnt) + len(rvnt):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(asset_script_hex))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.amount = 0
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = transfer_script_hex[:transfer_script_hex.index(rvnt)] + rvnt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_transfer = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)['hex']
        assert_raises_rpc_error(-26, "Invalid parameter: asset amount can't be equal to or less than zero.",
                                n0.sendrawtransaction, tx_bad_transfer_signed)

        signed_hex = n0.signrawtransaction(tx_transfer_hex)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        assert_equal(6, n0.listmyassets()[root])
        assert_equal(4, n1.listmyassets()[root])
        assert_equal(1, n0.listmyassets()[root + '!'])

    def transfer_asset_inserting_tampering_test(self):
        self.log.info("Testing of asset issue inserting tampering...")
        n0 = self.nodes[0]

        # create the root asset that the sub asset will try to be created from
        root = "TRANSFER_ASSET_INSERTING"
        n0.issue(root, 10)
        n0.generate(1)
        self.sync_all()

        change_address = n0.getnewaddress()
        to_address = n0.getnewaddress()  # back to n0 from n0
        unspent = get_first_unspent(self, n0)
        unspent_asset = n0.listmyassets(root, True)[root]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_asset[k] for k in ['txid', 'vout']},
        ]

        # Create the valid transfer and broadcast the transaction
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.00001),
            to_address: {
                'transfer': {
                    root: 10,
                }
            }
        }

        tx_transfer_hex = n0.createrawtransaction(inputs, outputs)

        # try tampering to issue sub asset a mismatched the transfer amount to 0
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_transfer_hex))
        tx.deserialize(f)
        rvnt = '72766e74'  # rvnt
        op_drop = '75'

        # create a new issue CTxOut
        issue_out = CTxOut()
        issue_out.nValue = 0

        # create a new issue script
        issue_script = CScriptIssue()
        issue_script.name = b'BYTE_ISSUE'
        issue_script.amount = 1
        issue_serialized = bytes_to_hex_str(issue_script.serialize())
        rvnq = '72766e71'  # rvnq

        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if rvnt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)

                # Generate a script that has a valid destination address but switch it with rvnq and the issue_serialized data
                issue_out.scriptPubKey = hex_str_to_bytes(transfer_script_hex[:transfer_script_hex.index(rvnt)] + rvnq + issue_serialized + op_drop)

        tx.vout.insert(0, issue_out)  # Insert the issue transaction at the begin on the vouts

        tx_inserted_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_inserted_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-asset-transaction",
                                n0.sendrawtransaction, tx_bad_transfer_signed)

    def atomic_swaps_test(self):
        self.log.info("Testing atomic swaps...")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        jaina = "JAINA"
        anduin = "ANDUIN"
        jaina_owner = f"{jaina}!"
        anduin_owner = f"{anduin}!"

        starting_amount = 1000

        receive1 = n1.getnewaddress()
        change1 = n1.getnewaddress()
        n0.sendtoaddress(receive1, 50000)
        n0.generate(1)
        self.sync_all()
        n1.issue(jaina, starting_amount)
        n1.generate(1)
        self.sync_all()
        balance1 = float(n1.getwalletinfo()['balance'])

        receive2 = n2.getnewaddress()
        change2 = n2.getnewaddress()
        n0.sendtoaddress(receive2, 50000)
        n0.generate(1)
        self.sync_all()
        n2.issue(anduin, starting_amount)
        n2.generate(1)
        self.sync_all()
        balance2 = float(n2.getwalletinfo()['balance'])

        ########################################
        # rvn for assets

        # n1 buys 400 ANDUIN from n2 for 4000 RVN
        price = 4000
        amount = 400
        fee = 0.01

        unspent1 = get_first_unspent(self, n1, price + fee)
        unspent_amount1 = float(unspent1['amount'])

        unspent_asset2 = n2.listmyassets(anduin, True)[anduin]['outpoints'][0]
        unspent_asset_amount2 = unspent_asset2['amount']
        assert (unspent_asset_amount2 > amount)

        inputs = [
            {k: unspent1[k] for k in ['txid', 'vout']},
            {k: unspent_asset2[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: {
                'transfer': {
                    anduin: amount
                }
            },
            change1: truncate(unspent_amount1 - price - fee),
            receive2: price,
            change2: {
                'transfer': {
                    anduin: truncate(unspent_asset_amount2 - amount, 0)
                }
            },
        }

        unsigned = n1.createrawtransaction(inputs, outputs)
        signed1 = n1.signrawtransaction(unsigned)['hex']
        signed2 = n2.signrawtransaction(signed1)['hex']
        _tx_id = n0.sendrawtransaction(signed2)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        newbalance2 = float(n2.getwalletinfo()['balance'])
        assert_equal(truncate(balance1 - price - fee), newbalance1)
        assert_equal(balance2 + price, newbalance2)

        assert_equal(amount, int(n1.listmyassets()[anduin]))
        assert_equal(starting_amount - amount, int(n2.listmyassets()[anduin]))

        ########################################
        # rvn for owner

        # n2 buys JAINA! from n1 for 20000 RVN
        price = 20000
        amount = 1
        balance1 = newbalance1
        balance2 = newbalance2

        unspent2 = get_first_unspent(self, n2, price + fee)
        unspent_amount2 = float(unspent2['amount'])

        unspent_owner1 = n1.listmyassets(jaina_owner, True)[jaina_owner]['outpoints'][0]
        unspent_owner_amount1 = unspent_owner1['amount']
        assert_equal(amount, unspent_owner_amount1)

        inputs = [
            {k: unspent2[k] for k in ['txid', 'vout']},
            {k: unspent_owner1[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: price,
            receive2: {
                'transfer': {
                    jaina_owner: amount
                }
            },
            change2: truncate(unspent_amount2 - price - fee),
        }

        unsigned = n2.createrawtransaction(inputs, outputs)
        signed2 = n2.signrawtransaction(unsigned)['hex']
        signed1 = n1.signrawtransaction(signed2)['hex']
        _tx_id = n0.sendrawtransaction(signed1)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        newbalance2 = float(n2.getwalletinfo()['balance'])
        assert_equal(balance1 + price, newbalance1)
        assert_equal(truncate(balance2 - price - fee), newbalance2)

        assert_does_not_contain_key(jaina_owner, n1.listmyassets())
        assert_equal(amount, int(n2.listmyassets()[jaina_owner]))

        ########################################
        # assets for assets and owner

        # n1 buys ANDUIN! and 300 ANDUIN from n2 for 900 JAINA
        price = 900
        amount = 300
        amount_owner = 1
        balance1 = newbalance1

        unspent1 = get_first_unspent(self, n1)
        unspent_amount1 = float(unspent1['amount'])
        assert (unspent_amount1 > fee)

        unspent_asset1 = n1.listmyassets(jaina, True)[jaina]['outpoints'][0]
        unspent_asset_amount1 = unspent_asset1['amount']

        unspent_asset2 = n2.listmyassets(anduin, True)[anduin]['outpoints'][0]
        unspent_asset_amount2 = unspent_asset2['amount']

        unspent_owner2 = n2.listmyassets(anduin_owner, True)[anduin_owner]['outpoints'][0]
        unspent_owner_amount2 = unspent_owner2['amount']

        assert (unspent_asset_amount1 > price)
        assert (unspent_asset_amount2 > amount)
        assert_equal(amount_owner, unspent_owner_amount2)

        inputs = [
            {k: unspent1[k] for k in ['txid', 'vout']},
            {k: unspent_asset1[k] for k in ['txid', 'vout']},
            {k: unspent_asset2[k] for k in ['txid', 'vout']},
            {k: unspent_owner2[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: {
                'transfer': {
                    anduin: amount,
                    anduin_owner: amount_owner,
                }
            },
            # output map can't use change1 twice...
            n1.getnewaddress(): truncate(unspent_amount1 - fee),
            change1: {
                'transfer': {
                    jaina: truncate(unspent_asset_amount1 - price)
                }
            },
            receive2: {
                'transfer': {
                    jaina: price
                }
            },
            change2: {
                'transfer': {
                    anduin: truncate(unspent_asset_amount2 - amount, 0)
                }
            },
        }

        unsigned = n1.createrawtransaction(inputs, outputs)
        signed1 = n1.signrawtransaction(unsigned)['hex']
        signed2 = n2.signrawtransaction(signed1)['hex']
        _tx_id = n0.sendrawtransaction(signed2)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        assert_equal(truncate(balance1 - fee), newbalance1)

        assert_does_not_contain_key(anduin_owner, n2.listmyassets())
        assert_equal(amount_owner, int(n1.listmyassets()[anduin_owner]))

        assert_equal(unspent_asset_amount1 - price, n1.listmyassets()[jaina])
        assert_equal(unspent_asset_amount2 - amount, n2.listmyassets()[anduin])

    def getrawtransaction(self):
        self.log.info("Testing asset info in getrawtransaction...")
        n0 = self.nodes[0]

        asset_name = "RAW"
        asset_amount = 1000
        units = 2
        units2 = 4
        reissuable = True
        ipfs_hash = "QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E"
        ipfs_hash2 = "QmQ7DysAQmy92cyQrkb5y1M96pGG1fKxnRkiB19qWSmH75"

        tx_id = n0.issue(asset_name, asset_amount, "", "", units, reissuable, True, ipfs_hash)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        asset_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('asset', asset_out_script)
        asset_section = asset_out_script['asset']
        assert_equal(asset_name, asset_section['name'])
        assert_equal(asset_amount, asset_section['amount'])
        assert_equal(units, asset_section['units'])
        assert_equal(reissuable, asset_section['reissuable'])
        assert_equal(ipfs_hash, asset_section['ipfs_hash'])

        asset_out_script = raw_json['vout'][-2]['scriptPubKey']
        assert_contains_key('asset', asset_out_script)
        asset_section = asset_out_script['asset']
        assert_equal(asset_name + "!", asset_section['name'])
        assert_equal(1, asset_section['amount'])
        assert_does_not_contain_key('units', asset_section)
        assert_does_not_contain_key('reissuable', asset_section)
        assert_does_not_contain_key('ipfs_hash', asset_section)

        address = n0.getnewaddress()
        tx_id = n0.reissue(asset_name, asset_amount, address, "", True, -1, ipfs_hash2)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        asset_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('asset', asset_out_script)
        asset_section = asset_out_script['asset']
        assert_equal(asset_name, asset_section['name'])
        assert_equal(asset_amount, asset_section['amount'])
        assert_does_not_contain_key('units', asset_section)
        assert_equal(ipfs_hash2, asset_section['ipfs_hash'])

        address = n0.getnewaddress()
        tx_id = n0.reissue(asset_name, asset_amount, address, "", False, units2)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        asset_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('asset', asset_out_script)
        asset_section = asset_out_script['asset']
        assert_equal(asset_name, asset_section['name'])
        assert_equal(asset_amount, asset_section['amount'])
        assert_equal(units2, asset_section['units'])
        assert_does_not_contain_key('ipfs_hash', asset_section)

        address = n0.getnewaddress()
        tx_id = n0.transfer(asset_name, asset_amount, address)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        found_asset_out = False
        asset_out_script = ''
        for vout in raw_json['vout']:
            out_script = vout['scriptPubKey']
            if 'asset' in out_script:
                found_asset_out = True
                asset_out_script = out_script
        assert found_asset_out
        asset_section = asset_out_script['asset']
        assert_equal(asset_name, asset_section['name'])
        assert_equal(asset_amount, asset_section['amount'])

    def fundrawtransaction_transfer_outs(self):
        self.log.info("Testing fundrawtransaction with transfer outputs...")
        n0 = self.nodes[0]
        n2 = self.nodes[2]
        asset_name = "DONT_FUND_RVN"
        asset_amount = 100
        rvn_amount = 100

        n2_address = n2.getnewaddress()

        n0.issue("XXX")
        n0.issue("YYY")
        n0.issue("ZZZ")
        n0.generate(1)
        n0.transfer("XXX", 1, n2_address)
        n0.transfer("YYY", 1, n2_address)
        n0.transfer("ZZZ", 1, n2_address)
        n0.generate(1)
        self.sync_all()

        # issue asset
        n0.issue(asset_name, asset_amount)
        n0.generate(1)
        for _ in range(0, 5):
            n0.transfer(asset_name, asset_amount / 5, n2_address)
        n0.generate(1)
        self.sync_all()

        for _ in range(0, 5):
            n0.sendtoaddress(n2_address, rvn_amount / 5)
        n0.generate(1)
        self.sync_all()

        inputs = []
        unspent_asset = n2.listmyassets(asset_name, True)[asset_name]['outpoints'][0]
        inputs.append({k: unspent_asset[k] for k in ['txid', 'vout']})
        n0_address = n0.getnewaddress()
        outputs = {n0_address: {'transfer': {asset_name: asset_amount / 5}}}
        tx = n2.createrawtransaction(inputs, outputs)

        tx_funded = n2.fundrawtransaction(tx)['hex']
        signed = n2.signrawtransaction(tx_funded)['hex']
        n2.sendrawtransaction(signed)
        # no errors, yay

    def fundrawtransaction_nonwallet_transfer_outs(self):
        self.log.info("Testing fundrawtransaction with non-wallet transfer outputs...")
        n0 = self.nodes[0]
        n1 = self.nodes[1]
        n2 = self.nodes[2]
        asset_name = "NODE0_STUFF"
        n1_address = n1.getnewaddress()
        n2_address = n2.getnewaddress()

        # fund n2
        n0.sendtoaddress(n2_address, 1000)
        n0.generate(1)
        self.sync_all()

        # issue
        asset_amount = 100
        n0.issue(asset_name, asset_amount)
        n0.generate(1)
        self.sync_all()

        # have n2 construct transfer to n1_address using n0's utxos
        inputs = []
        unspent_asset = n0.listmyassets(asset_name, True)[asset_name]['outpoints'][0]
        inputs.append({k: unspent_asset[k] for k in ['txid', 'vout']})
        outputs = {n1_address: {'transfer': {asset_name: asset_amount}}}
        tx = n2.createrawtransaction(inputs, outputs)

        # n2 pays postage (fee)
        tx_funded = n2.fundrawtransaction(tx, {"feeRate": 0.02})['hex']

        # n2 signs postage; n0 signs transfer
        signed1 = n2.signrawtransaction(tx_funded)
        signed2 = n0.signrawtransaction(signed1['hex'])

        # send and verify
        n2.sendrawtransaction(signed2['hex'])
        n2.generate(1)
        self.sync_all()
        assert_contains_pair(asset_name, asset_amount, n1.listmyassets())

    def run_test(self):
        self.activate_assets()
        self.issue_reissue_transfer_test()
        self.unique_assets_test()
        self.issue_tampering_test()
        self.reissue_tampering_test()
        self.transfer_asset_tampering_test()
        self.transfer_asset_inserting_tampering_test()
        self.unique_assets_via_issue_test()
        self.bad_ipfs_hash_test()
        self.atomic_swaps_test()
        self.issue_invalid_address_test()
        self.issue_sub_invalid_address_test()
        self.issue_multiple_outputs_test()
        self.issue_sub_multiple_outputs_test()
        self.getrawtransaction()
        self.fundrawtransaction_transfer_outs()
        self.fundrawtransaction_nonwallet_transfer_outs()


if __name__ == '__main__':
    RawAssetTransactionsTest().main()
