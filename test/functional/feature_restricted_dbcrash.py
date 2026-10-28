#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Recover restricted asset and qualifier state after a coins-flush crash."""

import http.client

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, wait_until


class RestrictedAssetReplayCrashTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def run_test(self):
        node, reference = self.nodes
        node.generate(432)
        self.sync_all()
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')

        # Build a durable parent containing the restricted asset and qualifier.
        tagged_address = node.getnewaddress()
        node.issue('RESTRICTEDCRASH')
        node.generate(1)
        node.issuequalifierasset('#RESTRICTEDCRASH')
        node.generate(1)
        node.addtagtoaddress('#RESTRICTEDCRASH', tagged_address)
        node.generate(1)
        node.issuerestrictedasset('$RESTRICTEDCRASH', 100, '#RESTRICTEDCRASH', tagged_address)
        node.generate(1)
        self.sync_all()
        assert_equal(reference.getassetdata('$RESTRICTEDCRASH')['verifier_string'], 'RESTRICTEDCRASH')
        assert_equal(reference.checkaddresstag(tagged_address, '#RESTRICTEDCRASH'), True)
        node.gettxoutsetinfo()

        # The next block changes both qualifier membership and restricted
        # asset metadata. Keep the second node as an uninterrupted reference.
        crash_height = node.getblockcount() + 1
        newly_tagged_address = node.getnewaddress()
        self.restart_node(0, ['-assetindex', '-dbcache=1000',
                              '-dbcrashaftercoinsflushheight={}'.format(crash_height)])
        node = self.nodes[0]
        connect_nodes_bi(self.nodes, 0, 1)
        node.addtagtoaddress('#RESTRICTEDCRASH', newly_tagged_address)
        node.reissuerestrictedasset('$RESTRICTEDCRASH', 10, tagged_address,
                                    True, 'true')
        changed_block = node.generate(1)[0]
        self.sync_all()

        expected_asset_data = reference.getassetdata('$RESTRICTEDCRASH')
        expected_qualifier_data = reference.getassetdata('#RESTRICTEDCRASH')
        expected_tag = reference.checkaddresstag(tagged_address, '#RESTRICTEDCRASH')
        expected_new_tag = reference.checkaddresstag(newly_tagged_address, '#RESTRICTEDCRASH')
        assert_equal(expected_asset_data['amount'], 110)
        assert_equal(expected_asset_data['verifier_string'], 'true')
        assert_equal(expected_tag, True)
        assert_equal(expected_new_tag, True)

        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The post-coins-flush fault hook did not crash')
        node.wait_until_stopped()

        self.start_node(0, ['-assetindex', '-dbcache=1000'])
        node = self.nodes[0]
        wait_until(lambda: node.getbestblockhash() == changed_block,
                   err_msg='restricted asset rebuild did not reach the expected tip',
                   timeout=60)
        assert_equal(node.getbestblockhash(), changed_block)
        assert_equal(node.getassetdata('$RESTRICTEDCRASH'), expected_asset_data)
        assert_equal(node.getassetdata('#RESTRICTEDCRASH'), expected_qualifier_data)
        assert_equal(node.checkaddresstag(tagged_address, '#RESTRICTEDCRASH'), expected_tag)
        assert_equal(node.checkaddresstag(newly_tagged_address, '#RESTRICTEDCRASH'), expected_new_tag)


if __name__ == '__main__':
    RestrictedAssetReplayCrashTest().main()
