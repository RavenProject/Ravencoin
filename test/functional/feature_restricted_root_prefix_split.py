#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Persisted subqualifier lookup must ignore unrelated serialized keys."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, disconnect_nodes, sync_blocks


class RestrictedRootPrefixSplitTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def run_test(self):
        dirty, flushed = self.nodes
        dirty.generate(432)
        dirty.issuequalifierasset('#PREFIXROOT')
        dirty.generate(1)
        dirty.issuequalifierasset('PREFIXROOT/#SUB')
        dirty.generate(1)
        dirty.issuequalifierasset('#ZZZZZZZZZZZ')
        dirty.generate(1)
        dirty.issue('PREFIXASSET')
        dirty.generate(1)

        source = dirty.getnewaddress()
        recipient = dirty.getnewaddress()
        dirty.addtagtoaddress('PREFIXROOT/SUB', source)
        dirty.generate(1)
        dirty.addtagtoaddress('PREFIXROOT/SUB', recipient)
        dirty.generate(1)
        dirty.addtagtoaddress('#ZZZZZZZZZZZ', recipient)
        dirty.generate(1)
        dirty.issuerestrictedasset('$PREFIXASSET', 100, '#PREFIXROOT', source)
        dirty.generate(1)
        sync_blocks(self.nodes)

        shared_tip = dirty.getbestblockhash()
        assert_equal(flushed.getbestblockhash(), shared_tip)
        flushed.gettxoutsetinfo()
        self.log.info('Root qualification, dirty: %s; flushed: %s',
                      dirty.checkaddresstag(recipient, '#PREFIXROOT'),
                      flushed.checkaddresstag(recipient, '#PREFIXROOT'))
        disconnect_nodes(dirty, 1)
        disconnect_nodes(flushed, 0)

        txid = dirty.transfer('$PREFIXASSET', 1, recipient)
        dirty.generate(1)
        block_hash = dirty.getbestblockhash()
        result = flushed.submitblock(dirty.getblock(block_hash, 0))
        self.log.info('Restricted transfer %s in block %s: flushed peer returned %s',
                      txid, block_hash, result)
        assert_equal(result, None)
        assert_equal(flushed.getbestblockhash(), block_hash)
        assert_equal(dirty.checkaddresstag(recipient, '#PREFIXROOT'), True)
        assert_equal(flushed.checkaddresstag(recipient, '#PREFIXROOT'), True)


if __name__ == '__main__':
    RestrictedRootPrefixSplitTest().main()
