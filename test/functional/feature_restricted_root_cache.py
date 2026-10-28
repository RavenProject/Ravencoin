#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A removed subqualifier must not retain its root qualification."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedRootCacheTest(RavenTestFramework):
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
        node.issuequalifierasset('VERIFYROOT/#ALT')
        node.generate(1)
        node.issuequalifierasset('#ZZZZZZZZZZZ')
        node.generate(1)

        recipient = node.getnewaddress()
        node.addtagtoaddress('VERIFYROOT/SUB', recipient)
        node.generate(1)
        node.addtagtoaddress('VERIFYROOT/ALT', recipient)
        node.generate(1)
        node.addtagtoaddress('#ZZZZZZZZZZZ', recipient)
        node.generate(1)
        assert_equal(node.checkaddresstag(recipient, '#VERIFYROOT'), True)
        node.gettxoutsetinfo()
        assert_equal(node.checkaddresstag(recipient, '#VERIFYROOT'), True)

        node.removetagfromaddress('VERIFYROOT/SUB', recipient)
        node.generate(1)
        assert_equal(node.checkaddresstag(recipient, '#VERIFYROOT'), True)
        node.removetagfromaddress('VERIFYROOT/ALT', recipient)
        node.generate(1)
        tip = node.getbestblockhash()
        in_memory_result = node.checkaddresstag(recipient, '#VERIFYROOT')

        self.restart_node(0, ['-assetindex'])
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), tip)
        after_restart_result = node.checkaddresstag(recipient, '#VERIFYROOT')
        self.log.info('Root qualification before restart: %s; after restart: %s',
                      in_memory_result, after_restart_result)
        assert_equal(in_memory_result, False)
        assert_equal(after_restart_result, False)


if __name__ == '__main__':
    RestrictedRootCacheTest().main()
