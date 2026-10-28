#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify a valid qualifier-tag block against its historical asset state."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal


class RestrictedVerifyDBTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')

        address = node.getnewaddress()
        node.issue('VERIFYDBTAG')
        node.generate(1)
        node.issuequalifierasset('#VERIFYDBTAG')
        node.generate(1)
        node.addtagtoaddress('#VERIFYDBTAG', address)
        tag_block = node.generate(1)[0]
        assert_equal(node.getbestblockhash(), tag_block)
        assert_equal(node.checkaddresstag(address, '#VERIFYDBTAG'), True)

        # A valid tip must reconnect after an in-memory disconnect.
        assert_equal(node.verifychain(4, 1), True)

        # The same historical check must also succeed during startup.
        self.restart_node(0, ['-assetindex', '-checklevel=4', '-checkblocks=1'])
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), tag_block)

        node.removetagfromaddress('#VERIFYDBTAG', address)
        node.generate(1)
        assert_equal(node.checkaddresstag(address, '#VERIFYDBTAG'), False)
        assert_equal(node.verifychain(4, 1), True)

        node.addtagtoaddress('#VERIFYDBTAG', address)
        node.generate(1)
        node.issuerestrictedasset('$VERIFYDBTAG', 100, '#VERIFYDBTAG', address)
        node.generate(1)

        node.freezeaddress('$VERIFYDBTAG', address)
        node.generate(1)
        assert_equal(node.checkaddressrestriction(address, '$VERIFYDBTAG'), True)
        assert_equal(node.verifychain(4, 1), True)

        node.unfreezeaddress('$VERIFYDBTAG', address)
        node.generate(1)
        assert_equal(node.checkaddressrestriction(address, '$VERIFYDBTAG'), False)
        assert_equal(node.verifychain(4, 1), True)

        node.freezerestrictedasset('$VERIFYDBTAG')
        node.generate(1)
        assert_equal(node.checkglobalrestriction('$VERIFYDBTAG'), True)
        assert_equal(node.verifychain(4, 1), True)

        node.unfreezerestrictedasset('$VERIFYDBTAG')
        node.generate(1)
        assert_equal(node.checkglobalrestriction('$VERIFYDBTAG'), False)
        assert_equal(node.verifychain(4, 1), True)

        node.reissuerestrictedasset('$VERIFYDBTAG', 10, address, True, 'true')
        node.generate(1)
        assert_equal(node.getassetdata('$VERIFYDBTAG')['verifier_string'], 'true')
        assert_equal(node.verifychain(4, 1), True)

        # A transfer valid under the old verifier remains valid after a
        # later reissue restores the qualifier requirement.
        untagged_address = node.getnewaddress()
        node.transfer('$VERIFYDBTAG', 1, untagged_address)
        node.generate(1)
        node.reissuerestrictedasset('$VERIFYDBTAG', 1, address, True, '#VERIFYDBTAG')
        node.generate(1)
        assert_equal(node.getassetdata('$VERIFYDBTAG')['verifier_string'], 'VERIFYDBTAG')
        assert_equal(node.verifychain(4, 3), True)
        for depth in range(4, 13):
            assert node.verifychain(4, depth), 'historical verifier failed at depth {}'.format(depth)


if __name__ == '__main__':
    RestrictedVerifyDBTest().main()
