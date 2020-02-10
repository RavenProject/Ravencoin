#!/usr/bin/env python3
# Copyright (c) 2018-2019 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.blocktools import REGTEST_GENISIS_BLOCK_TIME

class CreateTxWalletTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        self.log.info('Create some old blocks')
        self.nodes[0].setmocktime(REGTEST_GENISIS_BLOCK_TIME)
        self.nodes[0].generate(200)
        self.nodes[0].setmocktime(0)

        self.test_anti_fee_sniping()
        self.test_tx_size_too_large()

    def test_anti_fee_sniping(self):
        self.log.info('Check that we have some (old) blocks and that anti-fee-sniping is disabled')
        assert_equal(self.nodes[0].getblockchaininfo()['blocks'], 200)
        tx = self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1))['hex'])
        attempts = 0
        while tx['locktime'] < 200:
            # for some reason sometimes we don't get the correct locktime on first attempt, retry...
            tx = self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1))['hex'])
            if attempts == 20:
                self.log.debug("Exceeded ~10 seconds waiting for tx locktime == 200.")
                break
            time.sleep(0.5)
            attempts += 1
        assert_equal(tx['locktime'], 200)

        self.log.info('Check that anti-fee-sniping is enabled when we mine a recent block')
        self.nodes[0].generate(1)
        tx = self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1))['hex'])
        assert 0 < tx['locktime'] <= 201

    def test_tx_size_too_large(self):
        # More than 10kB of outputs, so that we hit -maxtxfee with a high feerate
        outputs = {self.nodes[0].getnewaddress(): 0.000025 for _ in range(400)}
        raw_tx = self.nodes[0].createrawtransaction(inputs=[], outputs=outputs)

        self.log.info('Check maxtxfee in combination with -minrelaytxfee=0.01, -maxtxfee=0.1')
        self.restart_node(0, extra_args=['-minrelaytxfee=0.01', '-maxtxfee=0.1'])
        assert_raises_rpc_error(-6, "Transaction too large for fee policy", lambda: self.nodes[0].sendmany(fromaccount="", amounts=outputs),)
        assert_raises_rpc_error(-4, "Transaction too large for fee policy", lambda: self.nodes[0].fundrawtransaction(hexstring=raw_tx),)

        self.log.info('Check maxtxfee in combination with -mintxfee=0.01, -maxtxfee=0.1')
        self.restart_node(0, extra_args=['-mintxfee=0.01', '-maxtxfee=0.1'])
        assert_raises_rpc_error(-6, "Transaction too large for fee policy", lambda: self.nodes[0].sendmany(fromaccount="", amounts=outputs),)
        assert_raises_rpc_error(-4, "Transaction too large for fee policy", lambda: self.nodes[0].fundrawtransaction(hexstring=raw_tx),)

        self.log.info('Check maxtxfee in combination with -paytxfee=0.01, -maxtxfee=0.1')
        self.restart_node(0, extra_args=['-paytxfee=0.01', '-maxtxfee=0.1'])
        assert_raises_rpc_error(-6, "Transaction too large for fee policy", lambda: self.nodes[0].sendmany(fromaccount="", amounts=outputs),)
        assert_raises_rpc_error(-4, "Transaction too large for fee policy", lambda: self.nodes[0].fundrawtransaction(hexstring=raw_tx),)


        self.log.info('Check maxtxfee in combination with settxfee')
        self.restart_node(0, extra_args=['-maxtxfee=0.1'])
        self.nodes[0].settxfee(0.01)
        assert_raises_rpc_error(-6, "Transaction too large for fee policy", lambda: self.nodes[0].sendmany(fromaccount="", amounts=outputs),)
        assert_raises_rpc_error(-4, "Transaction too large for fee policy", lambda: self.nodes[0].fundrawtransaction(hexstring=raw_tx),)
        self.nodes[0].settxfee(0)


if __name__ == '__main__':
    CreateTxWalletTest().main()
