#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Rapid successive asset transfers — grocery / high-volume trading model.

Questions answered:
1) Can one wallet spend asset change from an unconfirmed transfer (0-conf chain)?
2) How long can that mempool chain grow before the wallet/policy stops it?
3) With one confirmation between spends, can transfers keep going (checkout line)?
4) Does pre-splitting into many UTXOs allow parallel same-block spends?
"""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal
from test_framework.authproxy import JSONRPCException


class AssetRapidTransfersTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ['-assetindex', '-fallbackfee=0.0001'],
            ['-assetindex', '-fallbackfee=0.0001'],
        ]

    def activate(self):
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])
        n0.sendtoaddress(self.nodes[1].getnewaddress(), 500)
        n0.generate(1)
        self.sync_all()

    def _issue(self, name, qty=100000):
        n0 = self.nodes[0]
        n0.issue(name, qty)
        n0.generate(1)
        self.sync_all()

    def test_mempool_change_chain(self, max_attempts=40):
        """Spend change of unconfirmed transfers as fast as the wallet allows."""
        n0, n1 = self.nodes[0], self.nodes[1]
        asset = "RAPIDMEMPOOL"
        self._issue(asset)
        dest = n1.getnewaddress()
        txids = []
        failed_at = None
        fail_msg = None
        for i in range(max_attempts):
            try:
                txid = n0.transfer(asset, 1, dest)
                if isinstance(txid, list):
                    txid = txid[0]
                txids.append(txid)
            except JSONRPCException as e:
                failed_at = i
                fail_msg = str(e)
                break
        mempool = set(n0.getrawmempool())
        in_mempool = sum(1 for t in txids if t in mempool)
        self.log.info(
            "Mempool change-chain: %d transfers accepted before stop (fail_at=%s, msg=%s, in_mempool=%d)"
            % (len(txids), failed_at, fail_msg, in_mempool))
        n0.generate(1)
        self.sync_all()
        bal = float(n1.listmyassets(asset).get(asset, 0))
        assert_equal(bal, float(len(txids)))
        return len(txids), failed_at, fail_msg

    def test_one_block_between(self, rounds=20):
        """Grocery-store model: one confirmation between each purchase."""
        n0, n1 = self.nodes[0], self.nodes[1]
        asset = "RAPID1BLOCK"
        self._issue(asset)
        dest = n1.getnewaddress()
        for i in range(rounds):
            txid = n0.transfer(asset, 1, dest)
            if isinstance(txid, list):
                txid = txid[0]
            n0.generate(1)
            self.sync_all()
            conf = n0.gettransaction(txid)['confirmations']
            assert conf >= 1, "transfer %d not confirmed" % i
        bal = float(n1.listmyassets(asset).get(asset, 0))
        assert_equal(bal, float(rounds))
        self.log.info("One-block-between: %d successive transfers all confirmed" % rounds)
        return rounds

    def test_presplit_parallel_same_block(self, pieces=10):
        """Pre-split balance into N UTXOs, then spend all in one block without chaining."""
        n0, n1 = self.nodes[0], self.nodes[1]
        asset = "RAPIDSPLIT"
        self._issue(asset, qty=pieces * 10)
        # Split: send 1 unit to N local addresses (confirmed) so each is a separate UTXO
        addrs = [n0.getnewaddress() for _ in range(pieces)]
        for a in addrs:
            n0.transfer(asset, 1, a)
            n0.generate(1)
            self.sync_all()
        dest = n1.getnewaddress()
        txids = []
        failed = 0
        for a in addrs:
            # Each 1-unit UTXO can be spent independently; transfer from wallet picks available coins
            try:
                txid = n0.transfer(asset, 1, dest)
                if isinstance(txid, list):
                    txid = txid[0]
                txids.append(txid)
            except JSONRPCException as e:
                failed += 1
                self.log.info("Parallel spend failed: %s" % e)
                break
        n0.generate(1)
        self.sync_all()
        bal = float(n1.listmyassets(asset).get(asset, 0))
        self.log.info(
            "Pre-split parallel same-block: issued %d pieces, %d mempool spends, recv=%s, failed=%d"
            % (pieces, len(txids), bal, failed))
        assert bal >= 1
        return len(txids), failed

    def test_burst_then_confirm(self, burst=5, cycles=4):
        """Trading burst: N unconfirmed transfers, then mine 1, repeat."""
        n0, n1 = self.nodes[0], self.nodes[1]
        asset = "RAPIDBURST"
        self._issue(asset)
        dest = n1.getnewaddress()
        total = 0
        for c in range(cycles):
            for i in range(burst):
                txid = n0.transfer(asset, 1, dest)
                if isinstance(txid, list):
                    txid = txid[0]
                total += 1
            n0.generate(1)
            self.sync_all()
        bal = float(n1.listmyassets(asset).get(asset, 0))
        assert_equal(bal, float(total))
        self.log.info("Burst-then-confirm: %d cycles x %d = %d transfers settled" % (cycles, burst, total))
        return total

    def run_test(self):
        self.activate()
        results = {}
        results['mempool_chain'] = self.test_mempool_change_chain()
        results['one_block'] = self.test_one_block_between()
        results['presplit'] = self.test_presplit_parallel_same_block()
        results['burst'] = self.test_burst_then_confirm()
        self.log.info("RESULTS %s" % results)


if __name__ == '__main__':
    AssetRapidTransfersTest().main()
