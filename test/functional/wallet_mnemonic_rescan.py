#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test that a wallet created from a mnemonic rescans an existing chain."""

import os

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal

MNEMONIC = "climb imitate repair vacant moral analyst barely night enemy fault report funny"


class WalletMnemonicRescanTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-bip44=1", "-mnemonic=" + MNEMONIC]]

    def run_test(self):
        node = self.nodes[0]
        address = node.getnewaddress()
        node.generatetoaddress(101, address)
        balance_before_restore = node.getbalance()
        assert balance_before_restore > 0

        self.stop_node(0)
        wallet_path = os.path.join(node.datadir, "regtest", "wallet.dat")
        os.remove(wallet_path)
        self.start_node(0)

        assert_equal(node.getbalance(), balance_before_restore)


if __name__ == "__main__":
    WalletMnemonicRescanTest().main()
