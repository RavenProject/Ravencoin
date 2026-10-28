#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A revoked subtag cannot make restricted validation depend on cache age."""

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, sync_blocks


class RestrictedRootSplitTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex'], ['-assetindex']]

    def run_test(self):
        live, restarted = self.nodes
        live.generate(432)
        assert_equal(live.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        live.issuequalifierasset('#SPLITROOT')
        live.generate(1)
        live.issuequalifierasset('SPLITROOT/#SUB')
        live.generate(1)
        live.issue('SPLITASSET')
        live.generate(1)

        source = live.getnewaddress()
        recipient = live.getnewaddress()
        live.addtagtoaddress('SPLITROOT/SUB', source)
        live.generate(1)
        live.addtagtoaddress('SPLITROOT/SUB', recipient)
        live.generate(1)
        live.issuerestrictedasset('$SPLITASSET', 100, '#SPLITROOT', source)
        live.generate(1)
        sync_blocks(self.nodes)
        live.gettxoutsetinfo()
        restarted.gettxoutsetinfo()
        live.removetagfromaddress('SPLITROOT/SUB', recipient)
        live.generate(1)
        sync_blocks(self.nodes)

        shared_tip = live.getbestblockhash()
        assert_equal(restarted.getbestblockhash(), shared_tip)
        self.restart_node(1, ['-assetindex'])
        restarted = self.nodes[1]
        assert_equal(restarted.getbestblockhash(), shared_tip)
        self.log.info('Root qualification, live: %s; restarted: %s',
                      live.checkaddresstag(recipient, '#SPLITROOT'),
                      restarted.checkaddresstag(recipient, '#SPLITROOT'))

        try:
            txid = live.transfer('$SPLITASSET', 1, recipient)
        except JSONRPCException as exc:
            assert 'verifier' in str(exc).lower(), str(exc)
        else:
            # Before the fix the stale cache admits an unauthorized transfer.
            # A peer with the same chain but fresh state must reject its block.
            live.generate(1)
            split_block = live.getbestblockhash()
            result = restarted.submitblock(live.getblock(split_block, 0))
            self.log.info('Restricted transfer %s in block %s: restarted peer returned %s',
                          txid, split_block, result)
            assert_equal(result, None)

        assert_equal(live.checkaddresstag(recipient, '#SPLITROOT'), False)
        assert_equal(restarted.checkaddresstag(recipient, '#SPLITROOT'), False)


if __name__ == '__main__':
    RestrictedRootSplitTest().main()
