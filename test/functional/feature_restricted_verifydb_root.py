#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify a restricted transfer before a later subqualifier tag."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedVerifyDBRootTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')

        node.issuequalifierasset('#VERIFYROOT')
        node.generate(1)
        node.issuequalifierasset('VERIFYROOT/#SUB')
        node.generate(1)
        node.issue('ROOTCASE')
        node.generate(1)

        source_address = node.getnewaddress()
        recipient = node.getnewaddress()
        node.issuerestrictedasset('$ROOTCASE', 100, '!#VERIFYROOT', source_address)
        node.generate(1)
        node.transfer('$ROOTCASE', 1, recipient)
        node.generate(1)
        assert_equal(node.checkaddresstag(recipient, '#VERIFYROOT'), False)

        node.addtagtoaddress('VERIFYROOT/SUB', recipient)
        node.generate(1)
        assert_equal(node.checkaddresstag(recipient, '#VERIFYROOT'), True)

        # The transfer predates the subtag and is valid under !#VERIFYROOT.
        assert_equal(node.verifychain(4, 2), True)


if __name__ == '__main__':
    RestrictedVerifyDBRootTest().main()
