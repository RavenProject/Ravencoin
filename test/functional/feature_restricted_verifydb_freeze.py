#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify historical restricted transfers across freeze and reorg cycles."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedVerifyDBFreezeTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        node.issue('VERIFYFREEZE')
        node.generate(1)
        node.issuequalifierasset('#VERIFYFREEZE')
        node.generate(1)
        recipient = node.getnewaddress()
        node.addtagtoaddress('#VERIFYFREEZE', recipient)
        node.generate(1)
        node.issuerestrictedasset('$VERIFYFREEZE', 100, '#VERIFYFREEZE', recipient)
        node.generate(1)

        node.freezeaddress('$VERIFYFREEZE', recipient)
        node.generate(1)
        node.unfreezeaddress('$VERIFYFREEZE', recipient)
        node.generate(1)
        node.transfer('$VERIFYFREEZE', 1, recipient)
        node.generate(1)
        node.freezeaddress('$VERIFYFREEZE', recipient)
        frozen_tip = node.generate(1)[0]
        node.unfreezeaddress('$VERIFYFREEZE', recipient)
        unfreeze_tip = node.generate(1)[0]
        node.invalidateblock(unfreeze_tip)
        assert_equal(node.getbestblockhash(), frozen_tip)
        assert_equal(node.checkaddressrestriction(recipient, '$VERIFYFREEZE'), True)
        assert_equal(node.verifychain(4, 4), True)


if __name__ == '__main__':
    RestrictedVerifyDBFreezeTest().main()
