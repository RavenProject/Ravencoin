#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Credit post-bit13 assets to PQ keys and preserve historical ownership."""

from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


ASSET_OPCODE = 0xc0
DROP_OPCODE = 0x75


class PQAssetWalletOwnershipTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ['-assetindex', '-vbparams=pq_assets:0:999999999999'],
            ['-assetindex', '-vbparams=pq_assets:0:999999999999'],
        ]

    @staticmethod
    def tag_asset_outputs(raw, program, expected_count):
        tx = from_hex(CTransaction(), raw)
        count = 0
        for output in tx.vout:
            script = output.scriptPubKey
            if len(script) > 31 and script[25] == ASSET_OPCODE:
                assert_equal(script[-1], DROP_OPCODE)
                output.scriptPubKey = script[:-1] + program
                count += 1
        assert_equal(count, expected_count)
        return to_hex(tx)

    def run_test(self):
        sender, recipient = self.nodes
        asset_name = 'PQWALLETOWNERSHIP'
        historical_name = 'PQHISTORICALTAG'
        sender_asset_address = sender.getnewaddress()
        historical_recipient = recipient.getnewaddress()
        pq_address = sender.getnewpqaddress()
        pq_script = bytes.fromhex(sender.validateaddress(pq_address)['scriptPubKey'])
        assert_equal(pq_script[:2], b'\x52\x20')
        assert_equal(recipient.validateaddress(pq_address)['ismine'], False)
        program = pq_script[2:]
        assert_equal(len(program), 32)

        sender.generate(860)
        self.sync_all()
        sender.issue(asset_name, 1, sender_asset_address)
        sender.issue(historical_name, 1, sender_asset_address)
        predefault_txid = sender.issue('PQPREDEFAULT', 1)[0]
        sender.generate(1)
        self.sync_all()
        assert_equal(sender.getblockcount(), 861)
        assert_equal(sender.listpqassetaddresses()['addresses'], [])
        assert_equal(sender.listmyassets('PQPREDEFAULT', True)
                     ['PQPREDEFAULT']['balance'], 1)
        predefault_destinations = [entry['destination'] for entry in
                                   sender.gettransaction(predefault_txid)['asset_details']
                                   if entry['asset_name'] == 'PQPREDEFAULT']
        assert_equal(len(predefault_destinations), 1)
        assert '|' not in predefault_destinations[0]

        # A tag-shaped output created before the effective activation height
        # remains a legacy, classically owned asset even after activation.
        historical_outpoint = sender.listmyassets(historical_name, True)[historical_name]['outpoints'][0]
        historical_funding = next(coin for coin in sender.listunspent()
                                  if coin['amount'] > Decimal('2'))
        historical_inputs = [
            {'txid': historical_outpoint['txid'], 'vout': historical_outpoint['vout']},
            {'txid': historical_funding['txid'], 'vout': historical_funding['vout']},
        ]
        historical_outputs = {
            historical_recipient: {'transfer': {historical_name: 1}},
            sender.getnewaddress(): historical_funding['amount'] - Decimal('0.01'),
        }
        historical_raw = sender.createrawtransaction(historical_inputs, historical_outputs)
        historical_tagged = self.tag_asset_outputs(historical_raw, program, 1)
        historical_signed = sender.signrawtransaction(historical_tagged)
        assert_equal(historical_signed['complete'], True)
        historical_txid = sender.sendrawtransaction(historical_signed['hex'])
        sender.generate(2)
        self.sync_all()
        assert_equal(sender.getblockcount(), 863)
        assert_equal(sender.getblockchaininfo()['bip9_softforks']['pq_assets']['status'], 'active')
        assert_equal(sender.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        assert_equal(recipient.listmyassets(historical_name, True)[historical_name]['balance'], 1)
        assert historical_name + '!' in recipient.viewallmessagechannels()
        assert_equal([entry['asset_name'] for entry in recipient.gettransaction(
            historical_txid)['asset_details'] if entry['category'] == 'receive'],
            [historical_name])
        assert_equal([entry['destination'] for entry in recipient.gettransaction(
            historical_txid)['asset_details'] if entry['category'] == 'receive'],
            [historical_recipient])
        recipient.removeprunedfunds(historical_txid)
        recipient.importprunedfunds(historical_signed['hex'],
                                    sender.gettxoutproof([historical_txid]))
        assert_equal(recipient.listmyassets(historical_name, True)
                     [historical_name]['balance'], 1)

        asset_outpoint = sender.listmyassets(asset_name, True)[asset_name]['outpoints'][0]
        owner_name = asset_name + '!'
        owner_outpoint = sender.listmyassets(owner_name, True)[owner_name]['outpoints'][0]
        recipient_asset_descriptor = recipient.getnewpqassetaddress()
        recipient_owner_descriptor = recipient.getnewpqassetaddress()
        _, recipient_asset_pq = recipient_asset_descriptor.split('|')
        _, recipient_owner_pq = recipient_owner_descriptor.split('|')
        native_coin = next(coin for coin in sender.listunspent()
                           if coin['amount'] > Decimal('3'))
        inputs = [
            {'txid': asset_outpoint['txid'], 'vout': asset_outpoint['vout']},
            {'txid': owner_outpoint['txid'], 'vout': owner_outpoint['vout']},
            {'txid': native_coin['txid'], 'vout': native_coin['vout']},
        ]
        outputs = {
            recipient_asset_descriptor: {'transfer': {asset_name: 1}},
            recipient_owner_descriptor: {'transfer': {owner_name: 1}},
            recipient_asset_pq: Decimal('1'),
            recipient_owner_pq: Decimal('1'),
            sender.getnewaddress(): native_coin['amount'] - Decimal('2.01'),
        }
        migration = sender.createrawtransaction(inputs, outputs)
        signed = sender.signrawtransaction(migration)
        assert_equal(signed['complete'], True)
        migration_txid = sender.sendrawtransaction(signed['hex'])
        sender.generate(1)
        self.sync_all()

        # The recipient owns each output through its PQ key and has no
        # classical private key for either synthetic asset identifier.
        for name, descriptor in ((asset_name, recipient_asset_descriptor),
                                 (owner_name, recipient_owner_descriptor)):
            asset_id, pq_destination = descriptor.split('|')
            assert_equal(recipient.validateaddress(asset_id)['ismine'], False)
            assert_equal(recipient.validateaddress(pq_destination)['ismine'], True)
            assert_equal(recipient.listmyassets(name, True)[name]['balance'], 1)
        assert owner_name in recipient.viewallmessagechannels()

        # A PQ-only asset input signs with an empty scriptSig, but consensus
        # still requires the separate matching native witness-v2 anchor.
        decoded = sender.decoderawtransaction(signed['hex'])
        asset_vouts = {out['scriptPubKey']['asset']['name']: out['n']
                       for out in decoded['vout']
                       if 'asset' in out['scriptPubKey']}
        assert_equal(set(asset_vouts), {asset_name, owner_name})
        for name, descriptor in ((asset_name, recipient_asset_descriptor),
                                 (owner_name, recipient_owner_descriptor)):
            asset_input = {'txid': migration_txid, 'vout': asset_vouts[name]}
            spend = recipient.createrawtransaction(
                [{'txid': asset_input['txid'], 'vout': asset_input['vout']}],
                {descriptor: {'transfer': {name: 1}}})
            spend_signed = recipient.signrawtransaction(spend)
            assert_equal(spend_signed['complete'], True)
            spend_tx = from_hex(CTransaction(), spend_signed['hex'])
            assert_equal(spend_tx.vin[0].scriptSig, b'')
            assert_raises_rpc_error(-26, 'bad-pq-asset-anchor',
                                    recipient.sendrawtransaction, spend_signed['hex'])

        # The post-activation defaults must create wallet-owned protected
        # destinations, including for an issuance with a protected owner token.
        parent_name = 'PQDEFAULTPARENT'
        parent_destination = sender.getnewpqassetaddress()
        sender.issue(parent_name, 1, parent_destination)
        sender.generate(1)
        self.sync_all()
        sender.sendtoaddress(parent_destination.split('|')[1], Decimal('1'))
        sender.generate(1)
        self.sync_all()

        before = set(sender.listpqassetaddresses()['addresses'])
        default_calls = (
            ('issue', 'PQDEFAULTROOT', lambda: sender.issue('PQDEFAULTROOT', 1)),
            ('issueunique', parent_name + '#AUTO',
             lambda: sender.issueunique(parent_name, ['AUTO'], None)),
            ('issuequalifierasset', '#PQDEFAULTQUAL',
             lambda: sender.issuequalifierasset('#PQDEFAULTQUAL', 1)),
        )
        failures = []
        issued = []
        for label, name, call in default_calls:
            try:
                issued.append((name, call()[0]))
            except JSONRPCException as error:
                failures.append((label, error.error['message']))
        assert_equal(failures, [])

        sender.generate(1)
        self.sync_all()
        new_destinations = set(sender.listpqassetaddresses()['addresses']) - before
        assert_equal(len(new_destinations), len(default_calls))
        received_destinations = set()
        for name, txid in issued:
            received = [entry['destination'] for entry in
                        sender.gettransaction(txid)['asset_details']
                        if entry['category'] == 'receive' and
                        entry['asset_name'] == name]
            assert_equal(len(received), 1)
            assert received[0] in new_destinations
            received_destinations.add(received[0])
            asset_id, pq = received[0].split('|')
            assert_equal(sender.validateaddress(asset_id)['ismine'], False)
            assert_equal(sender.validateaddress(pq)['ismine'], True)
            assert_equal(sender.listmyassets(name, True)[name]['balance'], 1)
        assert_equal(received_destinations, new_destinations)


if __name__ == '__main__':
    PQAssetWalletOwnershipTest().main()
