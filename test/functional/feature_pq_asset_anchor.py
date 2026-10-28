#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise the dependent RIP-25 asset rule with a real ML-DSA anchor spend."""

import configparser
from decimal import Decimal
import os
import shutil

from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import byte_to_base58
from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework import util as test_util
from test_framework.util import assert_equal, assert_raises_rpc_error


ASSET_OPCODE = 0xc0


class PQAssetAnchorTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex', '-vbparams=pq_assets:0:999999999999']]

    @staticmethod
    def output_index(raw, predicate):
        tx = from_hex(CTransaction(), raw)
        matches = [index for index, output in enumerate(tx.vout)
                   if predicate(output.scriptPubKey)]
        assert_equal(len(matches), 1)
        return matches[0]

    @staticmethod
    def is_asset_script(script):
        if len(script) <= 31 or script[25] != ASSET_OPCODE:
            return False
        classical = (script[:3] == b'\x76\xa9\x14' and
                     script[23:25] == b'\x88\xac')
        pq_only = (script[:3] == b'\x51\x51\x14' and
                   script[23:25] == b'\x75\x75')
        return classical or pq_only

    def run_test(self):
        colliding_anchor = b'\x52\x20' + b'\x00' * 23 + bytes([ASSET_OPCODE]) + b'\x00' * 8
        assert_equal(len(colliding_anchor), 34)
        assert_equal(self.is_asset_script(colliding_anchor), False)
        legacy_asset = b'\x76\xa9\x14' + b'\x00' * 20 + b'\x88\xac\xc0' + b'\x00' * 6
        pq_asset = b'\x51\x51\x14' + b'\x00' * 20 + b'\x75\x75\xc0' + b'\x00' * 6
        assert_equal(self.is_asset_script(legacy_asset), True)
        assert_equal(self.is_asset_script(pq_asset), True)

        node = self.nodes[0]
        asset_name = 'PQANCHORTEST'
        legacy_address = node.getnewaddress()
        pq_address = node.getnewpqaddress()
        pq_script = bytes.fromhex(node.validateaddress(pq_address)['scriptPubKey'])
        assert_equal(pq_script[:2], b'\x52\x20')
        program = pq_script[2:]
        assert_equal(len(program), 32)

        node.generate(861)
        node.issue(asset_name, 1, legacy_address)
        preactivation_asset = 'PQEARLYPREFIX'
        node.issue(preactivation_asset, 1, legacy_address)
        restricted_base = 'PQRESTRICTBASE'
        node.issue(restricted_base, 1, legacy_address)
        node.issuerestrictedasset('$' + restricted_base, 1, 'true',
                                  legacy_address)
        node.generate(1)
        assert_equal(node.getblockcount(), 862)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['pq_assets']['status'], 'active')
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')
        assert_raises_rpc_error(-4, 'PQ asset rules are not active',
                                node.getnewpqassetaddress)

        node.generate(1)
        assert_equal(node.getblockcount(), 863)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')

        assert_raises_rpc_error(-5, 'canonical PQ-only asset destination', node.issue,
                                'PQAFTERACTIVE', 1, legacy_address)

        # A rollback to the last pre-activation tip permits a legacy issue.
        # Reconsidering the boundary must remove that transaction from mempool.
        boundary_hash = node.getblockhash(863)
        node.invalidateblock(boundary_hash)
        assert_equal(node.getblockcount(), 862)
        rollback_txid = node.issue('PQROLLBACK', 1, legacy_address)[0]
        pending_reissue = node.reissue(asset_name, 1, legacy_address)[0]
        pending_reissue_raw = node.gettransaction(pending_reissue)['hex']
        assert rollback_txid in node.getrawmempool()
        assert pending_reissue in node.getrawmempool()
        node.reconsiderblock(boundary_hash)
        assert_equal(node.getblockcount(), 863)
        assert rollback_txid not in node.getrawmempool()
        assert pending_reissue not in node.getrawmempool()

        # A boundary eviction must also release the global reissue lock.
        # A second reissue at the old-policy tip must not be blocked by a
        # transaction that no longer exists in the mempool.
        node.invalidateblock(boundary_hash)
        assert_equal(node.getblockcount(), 862)
        replacement = from_hex(CTransaction(), pending_reissue_raw)
        replacement.nLockTime = 1
        replacement_signed = node.signrawtransaction(to_hex(replacement))
        assert_equal(replacement_signed['complete'], True)
        replacement_reissue = node.sendrawtransaction(replacement_signed['hex'])
        assert replacement_reissue != pending_reissue
        assert replacement_reissue in node.getrawmempool()
        node.reconsiderblock(boundary_hash)
        assert_equal(node.getblockcount(), 863)
        assert replacement_reissue not in node.getrawmempool()

        # A stored pre-activation issue must also fail in a candidate block
        # whose parent has the effective asset rule, not only at RPC relay.
        rollback_raw = node.gettransaction(rollback_txid)['hex']
        for txin in node.decoderawtransaction(rollback_raw)['vin']:
            assert node.gettxout(txin['txid'], txin['vout']) is not None
        config = configparser.ConfigParser()
        config.read(self.options.configfile)
        test_util.x16r_hash_cmd = os.path.join(
            config['environment']['BUILDDIR'], 'src', 'test',
            'test_raven_hash' + config['environment']['EXEEXT'])
        parent_hash = node.getbestblockhash()
        block_time = node.getblockheader(parent_hash)['time'] + 1
        invalid_block = create_block(int(parent_hash, 16), create_coinbase(864), block_time)
        invalid_block.nVersion = node.getblockheader(parent_hash)['version']
        invalid_block.vtx.append(from_hex(CTransaction(), rollback_raw))
        invalid_block.hashMerkleRoot = invalid_block.calc_merkle_root()
        invalid_block.solve()
        assert_equal(node.submitblock(invalid_block.serialize().hex()),
                     'bad-pq-asset-output')

        # An active-tip transfer can be held in the mempool while a reorg
        # returns the next block to pre-activation rules. The lookalike must
        # leave the mempool and must not be relayed again at the old tip.
        early_outpoint = node.listmyassets(preactivation_asset, True)[
            preactivation_asset]['outpoints'][0]
        early_native = next(coin for coin in node.listunspent()
                            if coin['amount'] > Decimal('2'))
        early_descriptor = node.getnewpqassetaddress()
        early_inputs = [
            {'txid': early_outpoint['txid'], 'vout': early_outpoint['vout']},
            {'txid': early_native['txid'], 'vout': early_native['vout']},
        ]
        early_outputs = {
            early_descriptor: {'transfer': {preactivation_asset: 1}},
            legacy_address: early_native['amount'] - Decimal('0.01'),
        }
        early_raw = node.createrawtransaction(early_inputs, early_outputs)
        early_signed = node.signrawtransaction(early_raw)
        assert_equal(early_signed['complete'], True)
        early_txid = node.sendrawtransaction(early_signed['hex'])
        assert early_txid in node.getrawmempool()
        node.invalidateblock(boundary_hash)
        assert_equal(node.getblockcount(), 862)
        assert early_txid not in node.getrawmempool()
        assert_raises_rpc_error(-26, 'premature-pq-asset',
                                node.sendrawtransaction, early_signed['hex'])
        node.reconsiderblock(boundary_hash)
        assert_equal(node.getblockcount(), 863)

        # Historical blocks remain valid. Mine the same lookalike directly
        # before the rule activates, then prove its spend needs no anchor.
        node.invalidateblock(boundary_hash)
        parent_hash = node.getbestblockhash()
        block_time = node.getblockheader(parent_hash)['time'] + 1
        historical_block = create_block(int(parent_hash, 16),
                                        create_coinbase(863), block_time)
        historical_block.nVersion = node.getblockheader(boundary_hash)['version']
        historical_block.vtx.append(from_hex(CTransaction(), early_signed['hex']))
        historical_block.hashMerkleRoot = historical_block.calc_merkle_root()
        historical_block.solve()
        assert_equal(node.submitblock(historical_block.serialize().hex()), None)
        assert_equal(node.getblockcount(), 863)
        assert preactivation_asset not in node.listmyassets(preactivation_asset, True)

        early_asset_index = self.output_index(
            early_signed['hex'],
            self.is_asset_script)
        early_fee_coin = next(coin for coin in node.listunspent()
                              if coin['amount'] > Decimal('2'))
        historical_spend = node.createrawtransaction(
            [{'txid': early_txid, 'vout': early_asset_index},
             {'txid': early_fee_coin['txid'], 'vout': early_fee_coin['vout']}],
            {early_descriptor: {'transfer': {preactivation_asset: 1}},
             legacy_address: early_fee_coin['amount'] - Decimal('0.01')})
        historical_spend = node.signrawtransaction(historical_spend)
        assert_equal(historical_spend['complete'], True)
        historical_spend_tx = from_hex(CTransaction(), historical_spend['hex'])
        assert_equal(historical_spend_tx.vin[0].scriptSig, b'')
        assert historical_spend_tx.wit.is_null()
        historical_spend_txid = node.sendrawtransaction(historical_spend['hex'])
        assert historical_spend_txid in node.getrawmempool()

        asset_outpoint = node.listmyassets(asset_name, True)[asset_name]['outpoints'][0]
        native_coin = next(coin for coin in node.listunspent()
                           if coin['amount'] > Decimal('2'))
        native_change = node.getnewaddress()
        migration_inputs = [
            {'txid': asset_outpoint['txid'], 'vout': asset_outpoint['vout']},
            {'txid': native_coin['txid'], 'vout': native_coin['vout']},
        ]
        migration_descriptor = node.getnewpqassetaddress()
        _, pq_address = migration_descriptor.split('|')
        pq_script = bytes.fromhex(node.validateaddress(pq_address)['scriptPubKey'])
        program = pq_script[2:]
        migration_outputs = {
            migration_descriptor: {'transfer': {asset_name: 1}},
            pq_address: Decimal('1'),
            native_change: native_coin['amount'] - Decimal('1.01'),
        }
        migration_raw = node.createrawtransaction(migration_inputs, migration_outputs)
        migration_tx = from_hex(CTransaction(), migration_raw)
        migration_asset_scripts = [output.scriptPubKey for output in migration_tx.vout
                                   if self.is_asset_script(output.scriptPubKey)]
        assert_equal(len(migration_asset_scripts), 1)
        assert_equal(migration_asset_scripts[0][:3], b'\x51\x51\x14')
        assert_equal(migration_asset_scripts[0][23:25], b'\x75\x75')
        migration_signed = node.signrawtransaction(migration_raw)
        assert_equal(migration_signed['complete'], True)
        migration_txid = node.sendrawtransaction(migration_signed['hex'])
        node.generate(1)
        assert_equal(node.getblockcount(), 864)
        assert_equal(node.listmyassets(asset_name, True)[asset_name]['balance'], 1)
        protected_early = node.listmyassets(preactivation_asset, True)[
            preactivation_asset]
        assert_equal(protected_early['balance'], 1)
        protected_early_outpoint = protected_early['outpoints'][0]
        no_early_anchor = node.createrawtransaction(
            [{'txid': protected_early_outpoint['txid'],
              'vout': protected_early_outpoint['vout']}],
            {early_descriptor: {'transfer': {preactivation_asset: 1}}})
        no_early_anchor = node.signrawtransaction(no_early_anchor)
        assert_equal(no_early_anchor['complete'], True)
        assert_raises_rpc_error(-26, 'bad-pq-asset-anchor',
                                node.sendrawtransaction, no_early_anchor['hex'])

        asset_index = self.output_index(
            migration_signed['hex'],
            self.is_asset_script)
        anchor_index = self.output_index(
            migration_signed['hex'], lambda script: script == pq_script)
        asset_input = {'txid': migration_txid, 'vout': asset_index}
        anchor_input = {'txid': migration_txid, 'vout': anchor_index}

        no_anchor = node.createrawtransaction(
            [asset_input], {migration_descriptor: {'transfer': {asset_name: 1}}})
        no_anchor = node.signrawtransaction(no_anchor)
        assert_equal(no_anchor['complete'], True)
        assert_raises_rpc_error(-26, 'bad-pq-asset-anchor',
                                node.sendrawtransaction, no_anchor['hex'])
        parent_hash = node.getbestblockhash()
        block_time = node.getblockheader(parent_hash)['time'] + 1
        missing_anchor_block = create_block(int(parent_hash, 16),
                                            create_coinbase(865), block_time)
        missing_anchor_block.nVersion = node.getblockheader(parent_hash)['version']
        missing_anchor_block.vtx.append(from_hex(CTransaction(), no_anchor['hex']))
        missing_anchor_block.hashMerkleRoot = missing_anchor_block.calc_merkle_root()
        missing_anchor_block.solve()
        assert_equal(node.submitblock(missing_anchor_block.serialize().hex()),
                     'bad-pq-asset-anchor')

        spend_raw = node.createrawtransaction(
            [asset_input, anchor_input],
            {migration_descriptor: {'transfer': {asset_name: 1}},
             pq_address: Decimal('0.9')})
        spend_signed = node.signrawtransaction(spend_raw)
        assert_equal(spend_signed['complete'], True)
        signed_tx = from_hex(CTransaction(), spend_signed['hex'])
        assert_equal(len(signed_tx.wit.vtxinwit[1].scriptWitness.stack), 2)

        damaged = from_hex(CTransaction(), spend_signed['hex'])
        signature = bytearray(damaged.wit.vtxinwit[1].scriptWitness.stack[0])
        signature[0] ^= 1
        damaged.wit.vtxinwit[1].scriptWitness.stack[0] = bytes(signature)
        assert_raises_rpc_error(-26, 'PQ ML-DSA-44 signature verification failed',
                                node.sendrawtransaction,
                                damaged.serialize_with_witness().hex())

        spend_txid = node.sendrawtransaction(spend_signed['hex'])
        mined = node.generate(1)
        assert spend_txid in node.getblock(mined[0])['tx']

        # A raw spend can pay the exact fee from the anchor and return the
        # protected asset without native PQ change. The next spend still
        # needs a new matching anchor; the asset remains in the wallet.
        spent_asset_index = self.output_index(
            spend_signed['hex'],
            self.is_asset_script)
        spent_anchor_index = self.output_index(
            spend_signed['hex'], lambda script: script == pq_script)
        exact_fee_raw = node.createrawtransaction(
            [{'txid': spend_txid, 'vout': spent_asset_index},
             {'txid': spend_txid, 'vout': spent_anchor_index}],
            {migration_descriptor: {'transfer': {asset_name: 1}},
             legacy_address: Decimal('0.8')})
        exact_fee_signed = node.signrawtransaction(exact_fee_raw)
        assert_equal(exact_fee_signed['complete'], True)
        exact_fee_tx = from_hex(CTransaction(), exact_fee_signed['hex'])
        assert_equal(len(exact_fee_tx.vout), 2)
        assert not any(output.scriptPubKey == pq_script
                       for output in exact_fee_tx.vout)
        node.sendrawtransaction(exact_fee_signed['hex'])
        node.generate(1)
        assert_equal(node.listmyassets(asset_name, True)[asset_name]['balance'], 1)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, asset_name,
                                migration_descriptor, 1, migration_descriptor)
        node.sendtoaddress(pq_address, Decimal('1'))
        node.generate(1)
        node.transferfromaddress(asset_name, migration_descriptor, 1,
                                 migration_descriptor)
        node.generate(1)

        # Wallet issuance must tag both the root asset and its owner token.
        composite_address = node.getnewpqassetaddress()
        assert_equal(composite_address.count('|'), 1)
        classical_part, pq_part = composite_address.split('|')
        assert_equal(node.validateaddress(classical_part)['ismine'], False)
        assert_equal(node.validateaddress(pq_part)['ismine'], True)
        descriptor_script = bytes.fromhex(node.validateaddress(pq_part)['scriptPubKey'])
        assert_equal(descriptor_script[:2], b'\x52\x20')
        descriptor_program = descriptor_script[2:]
        root_txid = node.issue('PQROOTRECIPIENT', 10, composite_address)[0]
        root_tx = from_hex(CTransaction(), node.getrawtransaction(root_txid))
        tagged_asset_outputs = [output for output in root_tx.vout
                                if len(output.scriptPubKey) >= 32 and
                                output.scriptPubKey[-32:] == descriptor_program]
        assert_equal(len(tagged_asset_outputs), 2)
        for output in tagged_asset_outputs:
            assert_equal(output.scriptPubKey[25], ASSET_OPCODE)
            assert_equal(output.scriptPubKey[:3], b'\x51\x51\x14')
        node.generate(1)
        for owned_asset, expected_balance in (('PQROOTRECIPIENT', 10),
                                              ('PQROOTRECIPIENT!', 1)):
            owned_info = node.listmyassets(owned_asset, True)[owned_asset]
            assert_equal(owned_info['balance'], expected_balance)
            assert_equal(owned_info['outpoints'][0]['txid'], root_txid)

        # Spending the issued asset must consume a funded native input with
        # the source program, even when the recipient uses a different key.
        destination_descriptor = node.getnewpqassetaddress()
        _, destination_pq = destination_descriptor.split('|')
        destination_script = bytes.fromhex(node.validateaddress(destination_pq)['scriptPubKey'])
        destination_program = destination_script[2:]
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transfer, 'PQROOTRECIPIENT', 1,
                                destination_descriptor)
        source_anchor_txid = node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        source_anchor_vout = self.output_index(
            node.getrawtransaction(source_anchor_txid),
            lambda script: script == descriptor_script)
        other_native_coins = [
            {'txid': coin['txid'], 'vout': coin['vout']}
            for coin in node.listunspent()
            if (coin['txid'], coin['vout']) !=
            (source_anchor_txid, source_anchor_vout) and not coin.get('assetName')
        ]
        assert other_native_coins
        assert_equal(node.lockunspent(False, other_native_coins), True)
        transfer_txid = node.transfer('PQROOTRECIPIENT', 1,
                                    destination_descriptor)[0]
        transfer_tx = from_hex(CTransaction(), node.getrawtransaction(transfer_txid))
        anchor_vin = [index for index, txin in enumerate(transfer_tx.vin)
                      if txin.prevout.hash == int(source_anchor_txid, 16) and
                      txin.prevout.n == source_anchor_vout]
        assert_equal(len(anchor_vin), 1)
        assert_equal(len(transfer_tx.wit.vtxinwit[anchor_vin[0]].scriptWitness.stack), 2)
        transferred = [output for output in transfer_tx.vout
                       if len(output.scriptPubKey) > 57 and
                       output.scriptPubKey[25] == ASSET_OPCODE and
                       output.scriptPubKey[-32:] == destination_program]
        assert_equal(len(transferred), 1)
        protected_change = [output for output in transfer_tx.vout
                            if len(output.scriptPubKey) > 57 and
                            output.scriptPubKey[25] == ASSET_OPCODE and
                            output.scriptPubKey[-32:] == descriptor_program]
        assert_equal(len(protected_change), 1)
        refreshed_anchor_vouts = [index for index, output in enumerate(transfer_tx.vout)
                                  if output.scriptPubKey == descriptor_script and
                                  output.nValue > 0]
        assert_equal(len(refreshed_anchor_vouts), 1)
        node.generate(1)
        transfer_details = node.gettransaction(transfer_txid)['asset_details']
        assert any(detail.get('category') == 'send' and
                   detail.get('destination') == destination_descriptor
                   for detail in transfer_details)
        assert not any(detail.get('category') == 'receive' and
                       detail.get('address') == pq_part
                       for detail in node.gettransaction(transfer_txid)['details'])
        self.restart_node(0, self.extra_args[0])
        assert not any(detail.get('category') == 'receive' and
                       detail.get('address') == pq_part
                       for detail in node.gettransaction(transfer_txid)['details'])
        second_transfer_txid = node.transferfromaddress(
            'PQROOTRECIPIENT', composite_address, 1,
            destination_descriptor)[0]
        second_transfer = from_hex(CTransaction(),
                                   node.getrawtransaction(second_transfer_txid))
        refreshed_anchor_vin = [index for index, txin in enumerate(second_transfer.vin)
                                if txin.prevout.hash == int(transfer_txid, 16) and
                                txin.prevout.n == refreshed_anchor_vouts[0]]
        assert_equal(len(refreshed_anchor_vin), 1)
        assert_equal(len(second_transfer.wit.vtxinwit[refreshed_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        next_anchor_vouts = [index for index, output in enumerate(second_transfer.vout)
                             if output.scriptPubKey == descriptor_script and
                             output.nValue > 0]
        assert_equal(len(next_anchor_vouts), 1)
        node.generate(1)
        assert_equal(node.lockunspent(False, [
            {'txid': second_transfer_txid, 'vout': next_anchor_vouts[0]}]), True)
        assert_equal(node.listmyassets('PQROOTRECIPIENT', True)
                     ['PQROOTRECIPIENT']['balance'], 10)

        # Reissuance must protect both the newly created units and the
        # returned owner token. The owner return stays with its original
        # PQ program, even when new units use a different program.
        assert_raises_rpc_error(-5, 'canonical PQ-only asset destination',
                                node.reissue, 'PQROOTRECIPIENT', 2,
                                legacy_address)
        assert_raises_rpc_error(-4, 'funded matching PQ anchor',
                                node.reissue, 'PQROOTRECIPIENT', 2,
                                destination_descriptor)
        reissue_anchor_txid = node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        reissue_anchor_vout = self.output_index(
            node.getrawtransaction(reissue_anchor_txid),
            lambda script: script == descriptor_script)
        reissue_txid = node.reissue('PQROOTRECIPIENT', 2,
                                   destination_descriptor)[0]
        reissue_tx = from_hex(CTransaction(), node.getrawtransaction(reissue_txid))
        reissue_anchor_vin = [index for index, txin in enumerate(reissue_tx.vin)
                              if txin.prevout.hash == int(reissue_anchor_txid, 16) and
                              txin.prevout.n == reissue_anchor_vout]
        assert_equal(len(reissue_anchor_vin), 1)
        assert_equal(len(reissue_tx.wit.vtxinwit[reissue_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in reissue_tx.vout if len(output.scriptPubKey) > 57), 1)
        asset_id_script = bytes.fromhex(
            node.validateaddress(classical_part)['scriptPubKey'])
        pq_only_script = b'\x51\x51\x14' + asset_id_script[3:23] + b'\x75\x75'
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in reissue_tx.vout if len(output.scriptPubKey) > 57), 1)
        reissue_refresh_vouts = [index for index, output in enumerate(reissue_tx.vout)
                                 if output.scriptPubKey == descriptor_script and
                                 output.nValue > 0]
        assert_equal(len(reissue_refresh_vouts), 1)
        node.generate(1)
        other_reissue_anchors = [
            {'txid': coin['txid'], 'vout': coin['vout']}
            for coin in node.listunspent(0)
            if coin['scriptPubKey'] == descriptor_script.hex() and
            (coin['txid'], coin['vout']) !=
            (reissue_txid, reissue_refresh_vouts[0])]
        if other_reissue_anchors:
            assert_equal(node.lockunspent(False, other_reissue_anchors), True)
        assert_equal(node.listmyassets('PQROOTRECIPIENT', True)
                     ['PQROOTRECIPIENT']['balance'], 12)
        assert_equal(node.listmyassets('PQROOTRECIPIENT!', True)
                     ['PQROOTRECIPIENT!']['balance'], 1)

        # A subasset uses the root owner token as authority. Its return and
        # the new subasset and sub-owner outputs must all remain tagged.
        sub_name = 'PQROOTRECIPIENT/SUB'
        sub_issue_txid = node.issue(sub_name, 3, destination_descriptor)[0]
        sub_issue = from_hex(CTransaction(), node.getrawtransaction(sub_issue_txid))
        sub_anchor_vin = [index for index, txin in enumerate(sub_issue.vin)
                          if txin.prevout.hash == int(reissue_txid, 16) and
                          txin.prevout.n == reissue_refresh_vouts[0]]
        assert_equal(len(sub_anchor_vin), 1)
        assert_equal(len(sub_issue.wit.vtxinwit[sub_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        sub_refresh_vouts = [index for index, output in enumerate(sub_issue.vout)
                             if output.scriptPubKey == descriptor_script and
                             output.nValue > 0]
        assert_equal(len(sub_refresh_vouts), 1)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in sub_issue.vout
                         if len(output.scriptPubKey) > 57), 1)
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in sub_issue.vout
                         if len(output.scriptPubKey) > 57), 2)
        node.generate(1)
        assert_equal(node.lockunspent(False, [
            {'txid': sub_issue_txid, 'vout': sub_refresh_vouts[0]}]), True)
        assert_equal(node.listmyassets(sub_name, True)[sub_name]['balance'], 3)
        assert_equal(node.listmyassets(sub_name + '!', True)
                     [sub_name + '!']['balance'], 1)

        # A batched unique issue returns the same protected parent owner
        # token while placing every unique asset under the recipient program.
        assert_raises_rpc_error(-4, 'funded matching PQ anchor',
                                node.issueunique, 'PQROOTRECIPIENT',
                                ['first', 'second'], None,
                                destination_descriptor)
        unique_anchor_txid = node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        unique_anchor_vout = self.output_index(
            node.getrawtransaction(unique_anchor_txid),
            lambda script: script == descriptor_script)
        unique_txid = node.issueunique(
            'PQROOTRECIPIENT', ['first', 'second'], None,
            destination_descriptor)[0]
        unique_tx = from_hex(CTransaction(), node.getrawtransaction(unique_txid))
        unique_anchor_vin = [index for index, txin in enumerate(unique_tx.vin)
                             if txin.prevout.hash == int(unique_anchor_txid, 16) and
                             txin.prevout.n == unique_anchor_vout]
        assert_equal(len(unique_anchor_vin), 1)
        assert_equal(len(unique_tx.wit.vtxinwit[unique_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in unique_tx.vout
                         if len(output.scriptPubKey) > 57), 1)
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in unique_tx.vout
                         if len(output.scriptPubKey) > 57), 2)
        node.generate(1)
        for tag in ('first', 'second'):
            unique_name = 'PQROOTRECIPIENT#' + tag
            assert_equal(node.listmyassets(unique_name, True)
                         [unique_name]['balance'], 1)

        # A legacy root owner can first be migrated to a protected output.
        # The restricted-asset reissue then returns that protected owner token.
        assert_equal(node.lockunspent(True), True)
        assert_raises_rpc_error(-4, 'migrate the legacy authority first',
                                node.reissuerestrictedasset,
                                '$' + restricted_base, 1,
                                destination_descriptor)
        migrate_owner_txid = node.transfer(
            restricted_base + '!', 1, composite_address)[0]
        node.generate(1)
        migrated_owner = from_hex(
            CTransaction(), node.getrawtransaction(migrate_owner_txid))
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in migrated_owner.vout
                         if len(output.scriptPubKey) > 57), 1)
        restricted_anchor_txid = node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        restricted_anchor_vout = self.output_index(
            node.getrawtransaction(restricted_anchor_txid),
            lambda script: script == descriptor_script)
        available_restricted_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == descriptor_script.hex()
        }
        assert (restricted_anchor_txid, restricted_anchor_vout) in available_restricted_anchors
        restricted_reissue_txid = node.reissuerestrictedasset(
            '$' + restricted_base, 1, destination_descriptor)[0]
        restricted_reissue = from_hex(
            CTransaction(), node.getrawtransaction(restricted_reissue_txid))
        restricted_anchor_vin = [index for index, txin in
                                 enumerate(restricted_reissue.vin)
                                 if (format(txin.prevout.hash, '064x'),
                                     txin.prevout.n) in available_restricted_anchors]
        assert_equal(len(restricted_anchor_vin), 1)
        assert_equal(len(restricted_reissue.wit.vtxinwit[restricted_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in restricted_reissue.vout
                         if len(output.scriptPubKey) > 57), 1)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in restricted_reissue.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets('$' + restricted_base, True)
                     ['$' + restricted_base]['balance'], 2)

        # Owner tokens use the same protected transfer and anchor rules.
        owner_anchors = [
            {'txid': coin['txid'], 'vout': coin['vout']}
            for coin in node.listunspent(0)
            if coin['scriptPubKey'] == descriptor_script.hex()]
        if owner_anchors:
            assert_equal(node.lockunspent(False, owner_anchors), True)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transfer, 'PQROOTRECIPIENT!', 1,
                                destination_descriptor)
        assert_equal(node.lockunspent(True), True)
        node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        owner_transfer_txid = node.transfer(
            'PQROOTRECIPIENT!', 1, destination_descriptor)[0]
        owner_transfer = from_hex(
            CTransaction(), node.getrawtransaction(owner_transfer_txid))
        owner_outputs = [
            output for output in owner_transfer.vout
            if len(output.scriptPubKey) > 57 and
            output.scriptPubKey[25] == ASSET_OPCODE and
            output.scriptPubKey[-32:] == destination_program
        ]
        assert_equal(len(owner_outputs), 1)
        node.generate(1)
        assert_equal(node.listmyassets('PQROOTRECIPIENT!', True)
                     ['PQROOTRECIPIENT!']['balance'], 1)

        # An explicit asset change descriptor must preserve its own PQ key.
        assert_equal(node.lockunspent(True), True)
        node.sendtoaddress(pq_part, Decimal('1'))
        node.sendtoaddress(destination_pq, Decimal('1'))
        node.generate(1)
        explicit_change = node.getnewpqassetaddress()
        _, explicit_change_pq = explicit_change.split('|')
        explicit_change_script = bytes.fromhex(
            node.validateaddress(explicit_change_pq)['scriptPubKey'])
        next_destination = node.getnewpqassetaddress()
        explicit_transfer_txid = node.transferfromaddress(
            'PQROOTRECIPIENT', composite_address, 7,
            next_destination, '', 0, '', explicit_change)[0]
        explicit_transfer = from_hex(
            CTransaction(), node.getrawtransaction(explicit_transfer_txid))
        explicit_change_outputs = [
            output for output in explicit_transfer.vout
            if len(output.scriptPubKey) > 57 and
            output.scriptPubKey[25] == ASSET_OPCODE and
            output.scriptPubKey[-32:] == explicit_change_script[2:]
        ]
        assert_equal(len(explicit_change_outputs), 1)
        node.generate(1)
        assert_equal(node.listmyassets('PQROOTRECIPIENT', True)
                     ['PQROOTRECIPIENT']['balance'], 12)

        # A root qualifier has no parent authority input but its output must
        # still carry the recipient's canonical PQ program.
        assert_equal(node.lockunspent(True), True)
        qualifier_name = '#PQQUALROOT'
        qualifier_txid = node.issuequalifierasset(
            qualifier_name, 1, destination_descriptor)[0]
        qualifier_tx = from_hex(
            CTransaction(), node.getrawtransaction(qualifier_txid))
        assert_equal(sum(output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in qualifier_tx.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)

        # Restricted issuance is authorized by ROOT!, not by $ROOT!.
        # That owner token now uses the destination program after transfer.
        node.sendtoaddress(destination_pq, Decimal('1'))
        node.generate(1)
        available_issue_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == destination_script.hex()
        }
        assert available_issue_anchors
        restricted_issue_name = '$PQROOTRECIPIENT'
        restricted_issue_txid = node.issuerestrictedasset(
            restricted_issue_name, 1, 'true', composite_address)[0]
        restricted_issue = from_hex(
            CTransaction(), node.getrawtransaction(restricted_issue_txid))
        issue_anchor_vin = [index for index, txin in enumerate(restricted_issue.vin)
                            if (format(txin.prevout.hash, '064x'),
                                txin.prevout.n) in available_issue_anchors]
        assert_equal(len(issue_anchor_vin), 1)
        assert_equal(len(restricted_issue.wit.vtxinwit[issue_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in restricted_issue.vout
                         if len(output.scriptPubKey) > 57), 1)
        destination_classical, _ = destination_descriptor.split('|')
        destination_id_script = bytes.fromhex(
            node.validateaddress(destination_classical)['scriptPubKey'])
        destination_pq_only_script = (
            b'\x51\x51\x14' + destination_id_script[3:23] + b'\x75\x75')
        assert_equal(sum(output.scriptPubKey[:25] == destination_pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in restricted_issue.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets(restricted_issue_name, True)
                     [restricted_issue_name]['balance'], 1)

        # A sub-qualifier returns its parent qualifier authority, not an
        # owner token, under the parent key's existing PQ program.
        node.sendtoaddress(destination_pq, Decimal('1'))
        node.generate(1)
        available_qualifier_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == destination_script.hex()
        }
        assert available_qualifier_anchors
        sub_qualifier_name = '#PQQUALROOT/#SUB'
        sub_qualifier_txid = node.issuequalifierasset(
            sub_qualifier_name, 1, composite_address)[0]
        sub_qualifier_tx = from_hex(
            CTransaction(), node.getrawtransaction(sub_qualifier_txid))
        qualifier_anchor_vin = [index for index, txin in
                                enumerate(sub_qualifier_tx.vin)
                                if (format(txin.prevout.hash, '064x'),
                                    txin.prevout.n) in available_qualifier_anchors]
        assert_equal(len(qualifier_anchor_vin), 1)
        assert_equal(len(sub_qualifier_tx.wit.vtxinwit[qualifier_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[:25] == destination_pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == destination_program
                         for output in sub_qualifier_tx.vout
                         if len(output.scriptPubKey) > 57), 1)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in sub_qualifier_tx.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)
        assert_equal(node.listmyassets(sub_qualifier_name, True)
                     [sub_qualifier_name]['balance'], 1)

        # The dedicated qualifier transfer RPC must use the same protected
        # asset transfer builder as a normal asset send.
        node.sendtoaddress(destination_pq, Decimal('1'))
        node.generate(1)
        available_transfer_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == destination_script.hex()
        }
        assert available_transfer_anchors
        qualifier_transfer_txid = node.transferqualifier(
            qualifier_name, 1, composite_address)[0]
        qualifier_transfer = from_hex(
            CTransaction(), node.getrawtransaction(qualifier_transfer_txid))
        qualifier_transfer_anchor_vin = [index for index, txin in
                                         enumerate(qualifier_transfer.vin)
                                         if (format(txin.prevout.hash, '064x'),
                                             txin.prevout.n) in available_transfer_anchors]
        assert_equal(len(qualifier_transfer_anchor_vin), 1)
        assert_equal(len(qualifier_transfer.wit.vtxinwit[
            qualifier_transfer_anchor_vin[0]].scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in qualifier_transfer.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)

        # Tag administration consumes a protected qualifier and must return
        # its authority under a protected output, even without change_address.
        node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        admin_anchor_outpoints = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == descriptor_script.hex()
        }
        assert admin_anchor_outpoints
        tag_txid = node.addtagtoaddress(qualifier_name, classical_part)[0]
        tag_tx = from_hex(CTransaction(), node.getrawtransaction(tag_txid))
        tag_anchor_vin = [index for index, txin in enumerate(tag_tx.vin)
                          if (format(txin.prevout.hash, '064x'), txin.prevout.n)
                          in admin_anchor_outpoints]
        assert_equal(len(tag_anchor_vin), 1)
        assert_equal(len(tag_tx.wit.vtxinwit[tag_anchor_vin[0]]
                         .scriptWitness.stack), 2)
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in tag_tx.vout
                         if len(output.scriptPubKey) > 57), 1)
        assert_equal(sum(output.scriptPubKey == descriptor_script and
                         output.nValue > 0 for output in tag_tx.vout), 1)
        node.generate(1)
        assert_equal(qualifier_name in node.listtagsforaddress(classical_part), True)
        my_tag = [entry for entry in node.viewmytaggedaddresses()
                  if entry['Tag Name'] == qualifier_name and
                  entry['Address'] == classical_part]
        assert_equal(len(my_tag), 1)
        assert 'Assigned' in my_tag[0]

        # Qualifier lookup must use the asset identifier inside the PQ
        # descriptor. A recipient without the tag fails the same verifier.
        assert_raises_rpc_error(
            None, 'bad-txns-null-verifier-address-failed-verification',
            node.reissuerestrictedasset, restricted_issue_name, 1,
            destination_descriptor, True, qualifier_name)
        node.sendtoaddress(destination_pq, Decimal('1'))
        node.generate(1)
        qualified_reissue_txid = node.reissuerestrictedasset(
            restricted_issue_name, 1, composite_address,
            True, qualifier_name)[0]
        qualified_reissue = from_hex(
            CTransaction(), node.getrawtransaction(qualified_reissue_txid))
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in qualified_reissue.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_raises_rpc_error(
            None, 'bad-txns-null-verifier-address-failed-verification',
            node.transfer, restricted_issue_name, 1,
            destination_descriptor)
        assert_raises_rpc_error(
            None, 'Change address can not be sent to',
            node.transfer, restricted_issue_name, 1,
            composite_address, '', 0, '', destination_descriptor)
        node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        qualified_transfer_txid = node.transfer(
            restricted_issue_name, 1, composite_address)[0]
        qualified_transfer = from_hex(
            CTransaction(), node.getrawtransaction(qualified_transfer_txid))
        assert_equal(sum(output.scriptPubKey[:25] == pq_only_script and
                         output.scriptPubKey[25] == ASSET_OPCODE and
                         output.scriptPubKey[-32:] == descriptor_program
                         for output in qualified_transfer.vout
                         if len(output.scriptPubKey) > 57), 1)
        node.generate(1)
        assert_equal(node.listmyassets(restricted_issue_name, True)
                     [restricted_issue_name]['balance'], 2)

        def assert_protected_admin_return(method, arguments, anchor_address,
                                          anchor_script, return_script, return_program):
            node.sendtoaddress(anchor_address, Decimal('1'))
            node.generate(1)

            available_anchors = {
                (coin['txid'], coin['vout']) for coin in node.listunspent()
                if coin['scriptPubKey'] == anchor_script.hex()
            }
            assert available_anchors
            txid = getattr(node, method)(*arguments)[0]
            tx = from_hex(CTransaction(), node.getrawtransaction(txid))
            anchor_vin = [index for index, txin in enumerate(tx.vin)
                          if (format(txin.prevout.hash, '064x'), txin.prevout.n)
                          in available_anchors]
            assert_equal(len(anchor_vin), 1)
            assert_equal(len(tx.wit.vtxinwit[anchor_vin[0]]
                             .scriptWitness.stack), 2)
            assert_equal(sum(output.scriptPubKey[:25] == return_script and
                             output.scriptPubKey[25] == ASSET_OPCODE and
                             output.scriptPubKey[-32:] == return_program
                             for output in tx.vout
                             if len(output.scriptPubKey) > 57), 1)
            node.generate(1)

        unpaired_asset_id = byte_to_base58(bytes.fromhex('01' * 20), 111)
        assert_equal(node.validateaddress(unpaired_asset_id)['ismine'], False)
        assert_protected_admin_return(
            'addtagtoaddress', (qualifier_name, unpaired_asset_id,
                                composite_address),
            pq_part, descriptor_script, pq_only_script, descriptor_program)
        assert_equal(qualifier_name in node.listtagsforaddress(unpaired_asset_id),
                     True)
        assert_equal(any(entry['Address'] == unpaired_asset_id for entry in
                         node.viewmytaggedaddresses()), False)

        assert_raises_rpc_error(-5, 'canonical PQ-only asset destination',
                                node.removetagfromaddress, qualifier_name,
                                classical_part, classical_part)
        assert_protected_admin_return(
            'removetagfromaddress', (qualifier_name, classical_part,
                                     composite_address),
            pq_part, descriptor_script, pq_only_script, descriptor_program)
        my_tag = [entry for entry in node.viewmytaggedaddresses()
                  if entry['Tag Name'] == qualifier_name and
                  entry['Address'] == classical_part]
        assert_equal(len(my_tag), 1)
        assert 'Removed' in my_tag[0]
        for method, arguments in (
                ('freezeaddress', (restricted_issue_name, classical_part,
                                   destination_descriptor)),
                ('unfreezeaddress', (restricted_issue_name, classical_part)),
                ('freezerestrictedasset', (restricted_issue_name,)),
                ('unfreezerestrictedasset', (restricted_issue_name,))):
            assert_protected_admin_return(
                method, arguments, destination_pq, destination_script,
                destination_pq_only_script, destination_program)
            if method == 'freezeaddress':
                assert_equal(restricted_issue_name in node.listaddressrestrictions(
                    classical_part), True)
                my_restriction = [entry for entry in
                                  node.viewmyrestrictedaddresses()
                                  if entry['Asset Name'] == restricted_issue_name and
                                  entry['Address'] == classical_part]
                assert_equal(len(my_restriction), 1)
                assert 'Restricted' in my_restriction[0]
            elif method == 'unfreezeaddress':
                my_restriction = [entry for entry in
                                  node.viewmyrestrictedaddresses()
                                  if entry['Asset Name'] == restricted_issue_name and
                                  entry['Address'] == classical_part]
                assert_equal(len(my_restriction), 1)
                assert 'Derestricted' in my_restriction[0]

        # Raw asset construction must tag every asset output. The separate
        # owner/root return field must retain its own descriptor program.
        dummy_input = [{'txid': '00' * 32, 'vout': 0}]
        primary = (pq_only_script, descriptor_program)
        alternate = (destination_pq_only_script, destination_program)

        def assert_raw_asset_programs(asset_object, expected):
            raw = node.createrawtransaction(
                dummy_input, {composite_address: asset_object})
            tx = from_hex(CTransaction(), raw)
            actual = sorted((output.scriptPubKey[:25], output.scriptPubKey[-32:])
                            for output in tx.vout
                            if len(output.scriptPubKey) > 57 and
                            output.scriptPubKey[25] == ASSET_OPCODE)
            assert_equal(actual, sorted(expected))

        assert_raw_asset_programs(
            {'issue': {'asset_name': 'PQRAWROOT', 'asset_quantity': 1,
                       'units': 0, 'reissuable': 1, 'has_ipfs': 0}},
            [primary, primary])
        assert_raw_asset_programs(
            {'issue_unique': {'root_name': 'PQROOTRECIPIENT',
                              'asset_tags': ['rawone', 'rawtwo']}},
            [primary, primary, primary])
        assert_raw_asset_programs(
            {'reissue': {'asset_name': 'PQROOTRECIPIENT', 'asset_quantity': 1,
                         'owner_change_address': destination_descriptor}},
            [primary, alternate])
        assert_raw_asset_programs(
            {'transfer': {'PQROOTRECIPIENT': 1}}, [primary])
        assert_raw_asset_programs(
            {'transferwithmessage': {'PQROOTRECIPIENT': 1,
                                     'message': 'ab' * 32,
                                     'expire_time': 0}}, [primary])
        assert_raw_asset_programs(
            {'issue_restricted': {'asset_name': '$PQRAWROOT',
                                  'asset_quantity': 1, 'verifier_string': 'true',
                                  'units': 0, 'reissuable': 1, 'has_ipfs': 0,
                                  'owner_change_address': destination_descriptor}},
            [primary, alternate])
        assert_raw_asset_programs(
            {'reissue_restricted': {'asset_name': restricted_issue_name,
                                    'asset_quantity': 1,
                                    'owner_change_address': destination_descriptor}},
            [primary, alternate])
        assert_raw_asset_programs(
            {'issue_qualifier': {'asset_name': '#PQRAWQUAL',
                                 'asset_quantity': 1, 'has_ipfs': 0}},
            [primary])
        assert_raw_asset_programs(
            {'issue_qualifier': {'asset_name': '#PQQUALROOT/#RAW',
                                 'asset_quantity': 1, 'has_ipfs': 0,
                                 'root_change_address': destination_descriptor}},
            [primary, alternate])
        assert_raw_asset_programs(
            {'tag_addresses': {'qualifier': qualifier_name,
                               'addresses': [classical_part]}}, [primary])
        assert_raw_asset_programs(
            {'untag_addresses': {'qualifier': qualifier_name,
                                 'addresses': [classical_part]}}, [primary])
        assert_raw_asset_programs(
            {'freeze_addresses': {'asset_name': restricted_issue_name,
                                  'addresses': [classical_part]}}, [primary])
        assert_raw_asset_programs(
            {'unfreeze_addresses': {'asset_name': restricted_issue_name,
                                    'addresses': [classical_part]}}, [primary])
        assert_raw_asset_programs(
            {'freeze_asset': {'asset_name': restricted_issue_name}}, [primary])
        assert_raw_asset_programs(
            {'unfreeze_asset': {'asset_name': restricted_issue_name}}, [primary])
        assert_raises_rpc_error(
            -5, 'canonical PQ-only asset destination',
            node.createrawtransaction, dummy_input,
            {classical_part: {'transfer': {'PQROOTRECIPIENT': 1}}})
        assert_raises_rpc_error(
            None, 'owner_change_address must be a canonical PQ-only',
            node.createrawtransaction, dummy_input,
            {composite_address: {
                'reissue': {'asset_name': 'PQROOTRECIPIENT',
                            'asset_quantity': 1,
                            'owner_change_address': classical_part}}})
        mixed_raw = node.createrawtransaction(
            dummy_input,
            {composite_address: {'transfer': {'PQROOTRECIPIENT': 1}},
             classical_part: Decimal('1')})
        assert_equal(len(from_hex(CTransaction(), mixed_raw).vout), 2)
        alternate_program_descriptor = classical_part + '|' + destination_pq
        assert_raises_rpc_error(
            -5, 'canonical PQ-only asset destination',
            node.createrawtransaction, dummy_input,
            {alternate_program_descriptor: {'transfer': {'PQROOTRECIPIENT': 1}}})
        split_raw = node.createrawtransaction(
            dummy_input,
            {composite_address: {'transfer': {'PQROOTRECIPIENT': 1}},
             destination_descriptor: {'transfer': {'PQROOTRECIPIENT': 1}}})
        split_tx = from_hex(CTransaction(), split_raw)
        assert_equal(sorted(output.scriptPubKey[-32:] for output in split_tx.vout),
                     sorted((descriptor_program, destination_program)))

        self.test_anchor_availability_limits(node)

        # A backed-up encrypted wallet must recover the PQ key and its asset
        # identifier, then sign a matching PQ anchor after a fresh restart.
        passphrase = 'regtest-only-pq-asset-passphrase'
        node.node_encrypt_wallet(passphrase)
        self.start_node(0)
        assert_equal(node.validateaddress(classical_part)['ismine'], False)
        assert_equal(node.validateaddress(pq_part)['ismine'], True)
        assert_equal(any(entry['Address'] == unpaired_asset_id for entry in
                         node.viewmytaggedaddresses()), False)
        assert_equal(any(entry['Address'] == classical_part and
                         entry['Tag Name'] == qualifier_name and
                         'Removed' in entry for entry in
                         node.viewmytaggedaddresses()), True)
        assert_equal(any(entry['Address'] == classical_part and
                         entry['Asset Name'] == restricted_issue_name and
                         'Derestricted' in entry for entry in
                         node.viewmyrestrictedaddresses()), True)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)
        assert_raises_rpc_error(-13, 'walletpassphrase first',
                                node.transferqualifier, qualifier_name, 1,
                                composite_address)
        backup_path = os.path.join(node.datadir, 'pq-asset-wallet.bak')
        node.backupwallet(backup_path)
        self.stop_node(0)
        shutil.copyfile(backup_path,
                        os.path.join(node.datadir, 'regtest', 'wallet.dat'))
        self.start_node(0)
        assert_equal(node.validateaddress(classical_part)['ismine'], False)
        assert_equal(node.validateaddress(pq_part)['ismine'], True)
        assert_equal(any(entry['Address'] == classical_part and
                         entry['Tag Name'] == qualifier_name and
                         'Removed' in entry for entry in
                         node.viewmytaggedaddresses()), True)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)
        node.walletpassphrase(passphrase, 600)
        node.sendtoaddress(pq_part, Decimal('1'))
        node.generate(1)
        recovered_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == descriptor_script.hex()
        }
        recovered_txid = node.transferqualifier(
            qualifier_name, 1, composite_address)[0]
        recovered_tx = from_hex(CTransaction(),
                                node.getrawtransaction(recovered_txid))
        recovered_vin = [index for index, txin in enumerate(recovered_tx.vin)
                         if (format(txin.prevout.hash, '064x'), txin.prevout.n)
                         in recovered_anchors]
        assert_equal(len(recovered_vin), 1)
        assert_equal(len(recovered_tx.wit.vtxinwit[recovered_vin[0]]
                         .scriptWitness.stack), 2)
        node.generate(1)
        assert_equal(node.listmyassets(qualifier_name, True)
                     [qualifier_name]['balance'], 1)

    def test_anchor_availability_limits(self, node):
        assert_equal(node.lockunspent(True), True)

        # An ordinary RVN payment can select the only funded anchor. Asset
        # custody persists and a payment to the same PQ address restores it.
        drain_name = 'PQANCHORDRAIN'
        drain_descriptor = node.getnewpqassetaddress()
        _, drain_pq = drain_descriptor.split('|')
        drain_script = bytes.fromhex(
            node.validateaddress(drain_pq)['scriptPubKey'])
        node.issue(drain_name, 2, drain_descriptor)
        drain_anchor_txid = node.sendtoaddress(drain_pq, Decimal('1'))
        node.generate(1)
        drain_anchor = (drain_anchor_txid, self.output_index(
            node.getrawtransaction(drain_anchor_txid),
            lambda script: script == drain_script))
        other_native = [
            {'txid': coin['txid'], 'vout': coin['vout']}
            for coin in node.listunspent()
            if not coin.get('assetName') and
            (coin['txid'], coin['vout']) != drain_anchor
        ]
        assert other_native
        assert_equal(node.lockunspent(False, other_native), True)
        drain_txid = node.sendtoaddress(node.getnewaddress(), Decimal('0.5'))
        drain_tx = from_hex(CTransaction(), node.getrawtransaction(drain_txid))
        assert drain_anchor in self.input_outpoints(drain_tx)
        assert not any(output.scriptPubKey == drain_script
                       for output in drain_tx.vout)
        node.generate(1)
        assert_equal(node.listmyassets(drain_name, True)
                     [drain_name]['balance'], 2)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, drain_name,
                                drain_descriptor, 1, drain_descriptor)
        assert_equal(node.lockunspent(True), True)
        node.sendtoaddress(drain_pq, Decimal('1'))
        node.generate(1)
        node.transferfromaddress(drain_name, drain_descriptor, 1,
                                 drain_descriptor)
        node.generate(1)

        # An explicit RVN change address is honored even when it consumes
        # the sole anchor for an asset that is returned to its source pair.
        explicit_name = 'PQEXPLICITCHG'
        explicit_descriptor = node.getnewpqassetaddress()
        _, explicit_pq = explicit_descriptor.split('|')
        explicit_script = bytes.fromhex(
            node.validateaddress(explicit_pq)['scriptPubKey'])
        node.issue(explicit_name, 2, explicit_descriptor)
        node.sendtoaddress(explicit_pq, Decimal('1'))
        node.generate(1)
        explicit_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == explicit_script.hex()
        }
        assert_equal(len(explicit_anchors), 1)
        native_change = node.getnewaddress()
        native_change_script = bytes.fromhex(
            node.validateaddress(native_change)['scriptPubKey'])
        explicit_txid = node.transferfromaddress(
            explicit_name, explicit_descriptor, 1, explicit_descriptor,
            '', 0, native_change)[0]
        explicit_tx = from_hex(CTransaction(),
                               node.getrawtransaction(explicit_txid))
        assert explicit_anchors <= self.input_outpoints(explicit_tx)
        assert any(output.scriptPubKey == native_change_script
                   for output in explicit_tx.vout)
        assert not any(output.scriptPubKey == explicit_script
                       for output in explicit_tx.vout)
        node.generate(1)
        assert_equal(node.listmyassets(explicit_name, True)
                     [explicit_name]['balance'], 2)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, explicit_name,
                                explicit_descriptor, 1, explicit_descriptor)
        node.sendtoaddress(explicit_pq, Decimal('1'))
        node.generate(1)
        node.transferfromaddress(explicit_name, explicit_descriptor, 1,
                                 explicit_descriptor)
        node.generate(1)

        # Two protected source programs require two native anchor inputs.
        # The single default RVN change output refreshes neither program.
        multi_name = 'PQMULTIANCHOR'
        multi_a = node.getnewpqassetaddress()
        multi_b = node.getnewpqassetaddress()
        _, multi_a_pq = multi_a.split('|')
        _, multi_b_pq = multi_b.split('|')
        multi_a_script = bytes.fromhex(
            node.validateaddress(multi_a_pq)['scriptPubKey'])
        multi_b_script = bytes.fromhex(
            node.validateaddress(multi_b_pq)['scriptPubKey'])
        node.issue(multi_name, 2, multi_a)
        node.sendtoaddress(multi_a_pq, Decimal('1'))
        node.generate(1)
        node.transferfromaddress(multi_name, multi_a, 1, multi_b)
        node.generate(1)
        multi_a_anchors = [
            {'txid': coin['txid'], 'vout': coin['vout']}
            for coin in node.listunspent()
            if coin['scriptPubKey'] == multi_a_script.hex()
        ]
        assert multi_a_anchors
        assert_equal(node.lockunspent(False, multi_a_anchors), True)
        node.sendtoaddress(multi_b_pq, Decimal('1'))
        node.generate(1)
        assert_equal(node.lockunspent(True, multi_a_anchors), True)
        multi_a_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == multi_a_script.hex()
        }
        multi_b_anchors = {
            (coin['txid'], coin['vout']) for coin in node.listunspent()
            if coin['scriptPubKey'] == multi_b_script.hex()
        }
        assert multi_a_anchors
        assert_equal(len(multi_b_anchors), 1)
        multi_txid = node.transferfromaddresses(
            multi_name, [multi_a, multi_b], 2, multi_a)[0]
        multi_tx = from_hex(CTransaction(), node.getrawtransaction(multi_txid))
        multi_inputs = self.input_outpoints(multi_tx)
        assert multi_inputs & multi_a_anchors
        assert multi_inputs & multi_b_anchors
        assert not any(output.scriptPubKey in (multi_a_script,
                                               multi_b_script)
                       for output in multi_tx.vout)
        node.generate(1)
        assert_equal(node.listmyassets(multi_name, True)
                     [multi_name]['balance'], 2)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, multi_name,
                                multi_a, 1, multi_b)
        node.sendtoaddress(multi_a_pq, Decimal('1'))
        node.generate(1)
        node.transferfromaddress(multi_name, multi_a, 1, multi_b)
        node.generate(1)

        # Raw funding adds fee inputs but does not supply the protected
        # asset's matching anchor. An explicit anchor and output back to the
        # same PQ address allow the next protected spend.
        raw_name = 'PQRAWFUND'
        raw_descriptor = node.getnewpqassetaddress()
        _, raw_pq = raw_descriptor.split('|')
        raw_script = bytes.fromhex(node.validateaddress(raw_pq)['scriptPubKey'])
        node.issue(raw_name, 1, raw_descriptor)
        raw_anchor_txid = node.sendtoaddress(raw_pq, Decimal('1'))
        fee_address = node.getnewaddress()
        fee_script = bytes.fromhex(
            node.validateaddress(fee_address)['scriptPubKey'])
        fee_txid = node.sendtoaddress(fee_address, Decimal('2'))
        node.generate(1)
        raw_anchor = {'txid': raw_anchor_txid, 'vout': self.output_index(
            node.getrawtransaction(raw_anchor_txid),
            lambda script: script == raw_script)}
        fee_input = {'txid': fee_txid, 'vout': self.output_index(
            node.getrawtransaction(fee_txid),
            lambda script: script == fee_script)}
        asset_outpoint = node.listmyassets(raw_name, True)
        asset_outpoint = asset_outpoint[raw_name]['outpoints'][0]
        asset_input = {key: asset_outpoint[key] for key in ('txid', 'vout')}
        raw_outputs = {raw_descriptor: {'transfer': {raw_name: 1}}}
        raw_unanchored = node.createrawtransaction(
            [asset_input, fee_input], raw_outputs)
        raw_unanchored = node.fundrawtransaction(raw_unanchored)['hex']
        unanchored_tx = from_hex(CTransaction(), raw_unanchored)
        assert (raw_anchor['txid'], raw_anchor['vout']) not in \
            self.input_outpoints(unanchored_tx)
        raw_unanchored = node.signrawtransaction(raw_unanchored)
        assert_equal(raw_unanchored['complete'], True)
        assert_raises_rpc_error(-26, 'bad-pq-asset-anchor',
                                node.sendrawtransaction, raw_unanchored['hex'])
        assert_equal(node.listmyassets(raw_name, True)[raw_name]['balance'], 1)
        raw_replenished = node.createrawtransaction(
            [asset_input, fee_input, raw_anchor],
            {raw_descriptor: {'transfer': {raw_name: 1}},
             raw_pq: Decimal('0.9')})
        raw_replenished = node.fundrawtransaction(raw_replenished)['hex']
        raw_replenished = node.signrawtransaction(raw_replenished)
        assert_equal(raw_replenished['complete'], True)
        raw_txid = node.sendrawtransaction(raw_replenished['hex'])
        raw_tx = from_hex(CTransaction(), node.getrawtransaction(raw_txid))
        assert (raw_anchor['txid'], raw_anchor['vout']) in \
            self.input_outpoints(raw_tx)
        assert any(output.scriptPubKey == raw_script and
                   output.nValue == 90_000_000 for output in raw_tx.vout)
        node.generate(1)
        node.transferfromaddress(raw_name, raw_descriptor, 1,
                                 raw_descriptor)
        node.generate(1)

        # With no native output, the matching anchor is consumed entirely
        # as a fee. The asset remains controlled and a fresh PQ payment
        # restores its next spend.
        exact_name = 'PQEXACTFEE'
        exact_descriptor = node.getnewpqassetaddress()
        _, exact_pq = exact_descriptor.split('|')
        exact_script = bytes.fromhex(
            node.validateaddress(exact_pq)['scriptPubKey'])
        node.issue(exact_name, 1, exact_descriptor)
        exact_anchor_txid = node.sendtoaddress(exact_pq, Decimal('0.01'))
        node.generate(1)
        exact_anchor = {'txid': exact_anchor_txid, 'vout': self.output_index(
            node.getrawtransaction(exact_anchor_txid),
            lambda script: script == exact_script)}
        exact_outpoint = node.listmyassets(exact_name, True)
        exact_outpoint = exact_outpoint[exact_name]['outpoints'][0]
        exact_input = {key: exact_outpoint[key] for key in ('txid', 'vout')}
        exact_raw = node.createrawtransaction(
            [exact_input, exact_anchor],
            {exact_descriptor: {'transfer': {exact_name: 1}}})
        exact_signed = node.signrawtransaction(exact_raw)
        assert_equal(exact_signed['complete'], True)
        exact_txid = node.sendrawtransaction(exact_signed['hex'])
        exact_tx = from_hex(CTransaction(), node.getrawtransaction(exact_txid))
        assert_equal(len(exact_tx.vout), 1)
        assert_equal(exact_tx.vout[0].nValue, 0)
        node.generate(1)
        assert_equal(node.listmyassets(exact_name, True)
                     [exact_name]['balance'], 1)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, exact_name,
                                exact_descriptor, 1, exact_descriptor)
        node.sendtoaddress(exact_pq, Decimal('0.01'))
        node.generate(1)
        node.transferfromaddress(exact_name, exact_descriptor, 1,
                                 exact_descriptor)
        node.generate(1)

        # Funded raw change below the dust threshold is added to the fee.
        # The protected asset remains owned, but its sole anchor is gone.
        dust_name = 'PQDUSTCHANGE'
        dust_descriptor = node.getnewpqassetaddress()
        _, dust_pq = dust_descriptor.split('|')
        dust_script = bytes.fromhex(
            node.validateaddress(dust_pq)['scriptPubKey'])
        node.issue(dust_name, 1, dust_descriptor)
        dust_anchor_txid = node.sendtoaddress(dust_pq, Decimal('1'))
        node.generate(1)
        dust_anchor = {'txid': dust_anchor_txid, 'vout': self.output_index(
            node.getrawtransaction(dust_anchor_txid),
            lambda script: script == dust_script)}
        dust_outpoint = node.listmyassets(dust_name, True)
        dust_outpoint = dust_outpoint[dust_name]['outpoints'][0]
        dust_input = {key: dust_outpoint[key] for key in ('txid', 'vout')}
        dust_recipient = node.getnewaddress()
        dust_outputs = {
            dust_descriptor: {'transfer': {dust_name: 1}},
            dust_recipient: Decimal('0.5'),
        }
        dust_options = {'changeAddress': dust_pq}
        probe_raw = node.createrawtransaction(
            [dust_input, dust_anchor], dust_outputs)
        probe = node.fundrawtransaction(probe_raw, dust_options)
        assert probe['changepos'] >= 0
        probe_tx = from_hex(CTransaction(), probe['hex'])
        probe_change = probe_tx.vout[probe['changepos']]
        assert_equal(probe_change.scriptPubKey, dust_script)
        assert probe_change.nValue > 1_000
        assert_equal(self.input_outpoints(probe_tx), {
            (dust_input['txid'], dust_input['vout']),
            (dust_anchor['txid'], dust_anchor['vout']),
        })

        # Keep 1,000 satoshis of the probe's native change. This is below
        # the wallet's PQ witness-v2 discard threshold, so no change output
        # can refresh the anchor.
        dust_outputs[dust_recipient] += (
            Decimal(probe_change.nValue - 1_000) / Decimal(100_000_000))
        dust_raw = node.createrawtransaction(
            [dust_input, dust_anchor], dust_outputs)
        dust_funded = node.fundrawtransaction(dust_raw, dust_options)
        assert_equal(dust_funded['changepos'], -1)
        assert dust_funded['fee'] > probe['fee']
        dust_signed = node.signrawtransaction(dust_funded['hex'])
        assert_equal(dust_signed['complete'], True)
        dust_tx = from_hex(CTransaction(), dust_signed['hex'])
        assert_equal(len(dust_tx.vout), 2)
        assert_equal(self.input_outpoints(dust_tx), {
            (dust_input['txid'], dust_input['vout']),
            (dust_anchor['txid'], dust_anchor['vout']),
        })
        assert not any(output.scriptPubKey == dust_script
                       for output in dust_tx.vout)
        dust_txid = node.sendrawtransaction(dust_signed['hex'])
        assert dust_txid in node.getblock(node.generate(1)[0])['tx']
        assert_equal(node.listmyassets(dust_name, True)
                     [dust_name]['balance'], 1)
        assert_raises_rpc_error(-25, 'funded matching PQ anchor',
                                node.transferfromaddress, dust_name,
                                dust_descriptor, 1, dust_descriptor)

        dust_topup_txid = node.sendtoaddress(dust_pq, Decimal('0.01'))
        node.generate(1)
        dust_topup = (dust_topup_txid, self.output_index(
            node.getrawtransaction(dust_topup_txid),
            lambda script: script == dust_script))
        dust_restored_txid = node.transferfromaddress(
            dust_name, dust_descriptor, 1, dust_descriptor)[0]
        dust_restored = from_hex(
            CTransaction(), node.getrawtransaction(dust_restored_txid))
        assert dust_topup in self.input_outpoints(dust_restored)
        node.generate(1)

    @staticmethod
    def input_outpoints(tx):
        return {(format(txin.prevout.hash, '064x'), txin.prevout.n)
                for txin in tx.vin}


if __name__ == '__main__':
    PQAssetAnchorTest().main()
