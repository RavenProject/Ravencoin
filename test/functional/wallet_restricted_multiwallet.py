#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Restricted address views follow wallet ownership across load orders and index rebuilds."""

import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, wait_until


TAG = '#WALLETVIEWTAG'
RESTRICTED = '$WALLETVIEWASSET'


class RestrictedMultiWalletTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-wallet=w1', '-wallet=w2', '-assetindex']]

    def assert_views(self, first_addresses, second_addresses):
        node = self.nodes[0]
        for name, expected in [('w1', first_addresses), ('w2', second_addresses)]:
            wallet = node.get_wallet_rpc(name)
            tagged = {
                entry['Address']: entry for entry in wallet.viewmytaggedaddresses()
                if entry['Tag Name'] == TAG
            }
            frozen = {
                entry['Address']: entry for entry in wallet.viewmyrestrictedaddresses()
                if entry['Asset Name'] == RESTRICTED
            }
            assert_equal(set(tagged), set(expected))
            assert_equal(set(frozen), set(expected))
            for address in expected:
                assert 'Assigned' in tagged[address]
                assert 'Restricted' in frozen[address]

    def add_events(self, addresses):
        issuer = self.nodes[0].get_wallet_rpc('w1')
        for address in addresses:
            issuer.addtagtoaddress(TAG, address)
            issuer.generate(1)
            issuer.freezeaddress(RESTRICTED, address)
            issuer.generate(1)

    def run_test(self):
        node = self.nodes[0]
        issuer = node.get_wallet_rpc('w1')
        other = node.get_wallet_rpc('w2')
        issuer.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        issuer.issuequalifierasset(TAG)
        issuer.generate(1)
        issuer.issue('WALLETVIEWASSET')
        issuer.generate(1)
        issuer.issuerestrictedasset(RESTRICTED, 100, 'true', issuer.getnewaddress())
        issuer.generate(1)

        first_addresses = [issuer.getnewaddress()]
        second_addresses = [other.getnewaddress()]
        self.add_events(first_addresses + second_addresses)
        self.assert_views(first_addresses, second_addresses)

        self.restart_node(0, ['-wallet=w2', '-wallet=w1', '-assetindex'])
        self.assert_views(first_addresses, second_addresses)
        issuer = self.nodes[0].get_wallet_rpc('w1')
        other = self.nodes[0].get_wallet_rpc('w2')
        first_addresses.append(issuer.getnewaddress())
        second_addresses.append(other.getnewaddress())
        self.add_events(first_addresses[-1:] + second_addresses[-1:])
        self.assert_views(first_addresses, second_addresses)

        self.stop_node(0)
        shutil.rmtree(os.path.join(self.nodes[0].datadir, 'regtest', 'myrestricted'))
        self.start_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        self.assert_views(first_addresses, second_addresses)

        disconnected = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(disconnected)
        other = self.nodes[0].get_wallet_rpc('w2')
        frozen = {
            entry['Address'] for entry in other.viewmyrestrictedaddresses()
            if entry['Asset Name'] == RESTRICTED
        }
        assert_equal(frozen, {second_addresses[0]})
        self.nodes[0].reconsiderblock(disconnected)
        self.assert_views(first_addresses, second_addresses)

        height = self.nodes[0].getblockcount()
        self.restart_node(0, ['-wallet=w2', '-wallet=w1', '-assetindex', '-reindex'])
        wait_until(lambda: self.nodes[0].getblockcount() == height,
                   err_msg='reindex did not restore the active chain', timeout=60)
        self.assert_views(first_addresses, second_addresses)


if __name__ == '__main__':
    RestrictedMultiWalletTest().main()
