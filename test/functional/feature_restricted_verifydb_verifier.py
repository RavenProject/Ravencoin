#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify historical transfers across verifier reissues and reorgs."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedVerifyDBVerifierTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        node.issue('VERIFIERCASE')
        node.generate(1)
        node.issuequalifierasset('#VERIA')
        node.generate(1)
        node.issuequalifierasset('#VERIB')
        node.generate(1)

        source = node.getnewaddress()
        recipient_a = node.getnewaddress()
        recipient_b = node.getnewaddress()
        node.addtagtoaddress('#VERIA', source)
        node.generate(1)
        node.addtagtoaddress('#VERIA', recipient_a)
        node.generate(1)
        node.addtagtoaddress('#VERIB', recipient_b)
        node.generate(1)
        node.issuerestrictedasset('$VERIFIERCASE', 100, 'true', source)
        node.generate(1)

        node.reissuerestrictedasset('$VERIFIERCASE', 1, source, True, '#VERIA')
        node.generate(1)
        node.transfer('$VERIFIERCASE', 1, recipient_a)
        node.generate(1)
        node.reissuerestrictedasset('$VERIFIERCASE', 1, recipient_b, True, '#VERIB')
        verifier_b_tip = node.generate(1)[0]
        node.reissuerestrictedasset('$VERIFIERCASE', 1, source, True, 'true')
        verifier_true_tip = node.generate(1)[0]
        pending_reissue = node.getblock(verifier_true_tip)['tx'][-1]
        node.invalidateblock(verifier_true_tip)
        assert_equal(node.getbestblockhash(), verifier_b_tip)
        assert_equal(node.getassetdata('$VERIFIERCASE')['verifier_string'], 'VERIB')
        assert pending_reissue in node.getrawmempool()
        # A mempool reissue cannot invalidate a different historical reissue.
        assert_equal(node.verifychain(4, 3), True)


if __name__ == '__main__':
    RestrictedVerifyDBVerifierTest().main()
