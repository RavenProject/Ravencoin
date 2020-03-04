#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the Wallet BIP44 12 words implementation and supporting RPC"""

import os
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_does_not_contain, assert_contains, assert_raises_rpc_error
from test_framework.wallet_util import bip39_english

MNEMONIC_0 = 'climb imitate repair vacant moral analyst barely night enemy fault report funny'
MNEMONIC_PASS_0 = 'test0'
MNEMONIC_1 = 'glass random such ginger media want pink comfort portion large ability spare'
MNEMONIC_PASS_2 = 'test2'


class Bip44Test(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.extra_args = [['-bip44=1', '-mnemonic=' + MNEMONIC_0, '-mnemonicpassphrase=' + MNEMONIC_PASS_0], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_1], # BIP44 wallet with user-generated 12-words, but no passphrase
                           ['-bip44=1', '-mnemonicpassphrase=' + MNEMONIC_PASS_2], # BIP44 wallet with auto-generated 12-words but user-generated passphrase
                           ['-bip44=1'],    # BIP44 wallet with auto-generated 12-words and no passphrase
                           ['-bip44=0'],    # BIP44 wallet disabled but supplied with words and passphrase
                           ['-bip44=0']]    # BIP44 wallet disabled


    def run_test(self):
        nodes = self.nodes

        # BIP39 list should contain 2048 words
        assert_equal(len(bip39_english), 2048)

        # Node 0 has 12 words and a passphrase
        self.log.info("Testing BIP-44 Word-Lists and passphrases")
        assert_equal(len(nodes[0].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[0].getmywords()['word_list'], MNEMONIC_0) # Word list matches
        assert_equal(nodes[0].getmywords()['passphrase'], MNEMONIC_PASS_0) # Passphrase matches

        # Node 1 has 12 words but no passphrase
        assert_equal(len(nodes[1].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[1].getmywords()['word_list'], MNEMONIC_1) # Word list matches
        assert_does_not_contain(str(nodes[1].getmywords()), 'passphrase') # Passphrase does not exist

        # Node 2 was not created with specific 12 words (using random-auto-generated), and a passphrase
        assert_equal(len(nodes[2].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert(nodes[2].getmywords()['word_list'] != nodes[3].getmywords()['word_list']) # auto-generated word-lists should not match
        assert_equal(nodes[2].getmywords()['passphrase'], MNEMONIC_PASS_2)

        # Node 3 was not created with specific 12 words (using random-auto-generated), and no passphrase
        assert_equal(len(nodes[3].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_does_not_contain(str(nodes[3].getmywords()), 'passphrase') # Passphrase does not exist

        # Nodes 4 & 5 are not BIP44 and should not have 12-words or passphrase
        assert_raises_rpc_error(-4, "Wallet doesn't have 12 words.", nodes[4].getmywords)
        assert_raises_rpc_error(-4, "Wallet doesn't have 12 words.", nodes[5].getmywords)

        # Cannot change from a non-BIP44 to a BIP44 wallet
        self.log.info("Testing that BIP-44 wallets are intransigent")
        self.stop_node(4)
        self.stop_node(5)
        self.start_node(4, extra_args=['-bip44=1', '-mnemonicpassphrase=test4'])
        self.start_node(5, extra_args=['-bip44=1', '-mnemonic=' + MNEMONIC_0, '-mnemonicpassphrase=' + MNEMONIC_PASS_0])
        assert_raises_rpc_error(-4, "Wallet doesn't have 12 words.", nodes[4].getmywords)
        assert_raises_rpc_error(-4, "Wallet doesn't have 12 words.", nodes[5].getmywords)

        # Try to add a passphrase to an existing bip44 wallet (should not add passphrase)
        self.stop_node(3)
        self.start_node(3, extra_args=['-mnemonicpassphrase=test3'])
        assert_does_not_contain(str(nodes[3].getmywords()), 'passphrase') # Passphrase does not exist

        # Cannot change an already created bip44 wallet to a non-bip44 wallet
        word_list_3 = nodes[3].getmywords()['word_list']
        self.stop_node(3)
        self.start_node(3, extra_args=['-bip44=0'])
        assert_equal(nodes[3].getmywords()['word_list'], word_list_3) # Word list matches

        # All 4 bip44 enabled wallets word-lists are in the bip39 word-list
        self.log.info("Testing that BIP-44 wallets words are valid")
        word_list_0 = MNEMONIC_0.split(' ')
        word_list_1 = MNEMONIC_1.split(' ')
        word_list_2 = nodes[2].getmywords()['word_list'].split(' ')
        word_list_3 = word_list_3.split(' ')
        for i in range(0, 12):
            assert_contains(word_list_0[i], bip39_english)
            assert_contains(word_list_1[i], bip39_english)
            assert_contains(word_list_2[i], bip39_english)
            assert_contains(word_list_3[i], bip39_english)

        # None of the words should be text-readable in the log files
        self.log.info("Testing that BIP-44 words aren't text readable")
        mnemonic_2 = nodes[2].getmywords()['word_list']
        mnemonic_3 = nodes[3].getmywords()['word_list']
        self.stop_nodes()
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_0, f.read())
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_1, f.read())
        with open(os.path.join(self.options.tmpdir+"/node2/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(mnemonic_2, f.read())
        with open(os.path.join(self.options.tmpdir+"/node3/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(mnemonic_3, f.read())

        # But words are readable in a non-encrypted wallet
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "wallet.dat"), 'rb') as f:
            assert_contains(MNEMONIC_0, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "wallet.dat"), 'rb') as f:
            assert_contains(MNEMONIC_1, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node2/regtest/", "wallet.dat"), 'rb') as f:
            assert_contains(mnemonic_2, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node3/regtest/", "wallet.dat"), 'rb') as f:
            assert_contains(mnemonic_3, str(f.read()))

        # After encryption the words are no longer readable
        self.log.info("Testing that BIP-44 wallet encryption")
        self.start_nodes()
        nodes[0].node_encrypt_wallet("password0")
        nodes[1].node_encrypt_wallet("password1")
        nodes[2].node_encrypt_wallet("password2")
        nodes[3].node_encrypt_wallet("password3")
        self.stop_nodes()
        # But words are not readable in an encrypted wallet
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_0, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_1, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node2/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(mnemonic_2, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node3/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(mnemonic_3, str(f.read()))

        # But the words are still available using getmywords after entering the passphrase
        self.start_nodes()
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first.", nodes[0].getmywords)
        nodes[0].walletpassphrase("password0", 48)
        assert_equal(nodes[0].getmywords()['word_list'], MNEMONIC_0) # Word list matches

        # Words can also be retrieved from the dumpwallet command
        nodes[0].dumpwallet(os.path.join(self.options.tmpdir+"/node0/regtest/", "dump_wallet_0.txt"))
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "dump_wallet_0.txt"), 'r') as f:
            assert_contains(MNEMONIC_0, str(f.read()))

if __name__ == '__main__':
    Bip44Test().main()