#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify historical restricted transfers across global freeze cycles."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedVerifyDBGlobalTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        node.issue('VERIFYGLOBAL')
        node.generate(1)
        node.issuequalifierasset('#VERIFYGLOBAL')
        node.generate(1)
        recipient = node.getnewaddress()
        node.addtagtoaddress('#VERIFYGLOBAL', recipient)
        node.generate(1)
        node.issuerestrictedasset('$VERIFYGLOBAL', 100, '#VERIFYGLOBAL', recipient)
        node.generate(1)

        node.freezerestrictedasset('$VERIFYGLOBAL')
        node.generate(1)
        node.unfreezerestrictedasset('$VERIFYGLOBAL')
        node.generate(1)
        node.transfer('$VERIFYGLOBAL', 1, recipient)
        node.generate(1)
        node.freezerestrictedasset('$VERIFYGLOBAL')
        frozen_tip = node.generate(1)[0]
        node.unfreezerestrictedasset('$VERIFYGLOBAL')
        unfreeze_tip = node.generate(1)[0]
        node.invalidateblock(unfreeze_tip)
        assert_equal(node.getbestblockhash(), frozen_tip)
        assert_equal(node.checkglobalrestriction('$VERIFYGLOBAL'), True)
        assert_equal(node.verifychain(4, 4), True)


if __name__ == '__main__':
    RestrictedVerifyDBGlobalTest().main()
