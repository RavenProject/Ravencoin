#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Persistent chain accumulator — grows regtest history with diverse asset operations.

Each tick issues new assets (root, sub, unique, qualifier, restricted) and exercises
transfers, reissues, freeze, messaging channels, and lightweight P2AH flows.
Reuse --persistent-dir across daemon/cron invocations so block height and wallet
state accumulate.

Pair with contrib/chain_accumulator.sh (optionally runs feature_assetauth_stress.py
on the same datadir for deep P2AH scenarios).
"""

import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    initialize_data_dir,
)


def _assert_safe_reset_path(persistent_dir):
    _target = os.path.realpath(persistent_dir)
    _home = os.path.realpath(os.path.expanduser("~"))
    _repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
    _protected = {os.path.realpath(os.sep), _home, _repo_root}
    if (_target in _protected
            or any(_target.startswith(p + os.sep) for p in _protected)):
        raise RuntimeError(
            "Refusing to reset persistent dir %r: resolves to protected path %r"
            % (persistent_dir, _target))
    return _target


class ChainAccumulatorTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ['-assetindex', '-fallbackfee=0.0001', '-persistmempool=0'],
            ['-assetindex', '-fallbackfee=0.0001', '-persistmempool=0'],
        ]
        self.ticks = 1
        self.persistent_dir = None
        self.run_id = 0
        self.tag_epoch = 0
        self.scenario_counter = 0
        self.include_p2ah = False

    def add_options(self, parser):
        parser.add_option("--ticks", dest="ticks", default=1, type="int",
                          help="How many accumulator ticks per invocation (default: %default)")
        parser.add_option("--persistent-dir", dest="persistent_dir",
                          default=os.environ.get("CHAIN_ACCUMULATOR_DATADIR", ""),
                          help="Reuse this datadir across runs to accumulate chain history")
        parser.add_option("--reset-chain", dest="reset_chain", default=False, action="store_true",
                          help="Delete persistent-dir before starting (fresh chain)")
        parser.add_option("--include-p2ah", dest="include_p2ah", default=False, action="store_true",
                          help="Include P2AH-specific scenarios in the core accumulator")

    def setup_chain(self):
        self.persistent_dir = getattr(self.options, "persistent_dir", "") or None
        if self.persistent_dir:
            self.persistent_dir = os.path.abspath(self.persistent_dir)
            if getattr(self.options, "reset_chain", False) and os.path.isdir(self.persistent_dir):
                _target = _assert_safe_reset_path(self.persistent_dir)
                self.log.info("Resetting persistent chain at %s" % self.persistent_dir)
                shutil.rmtree(_target)
            os.makedirs(self.persistent_dir, exist_ok=True)
            self.options.tmpdir = self.persistent_dir
            self.options.nocleanup = True
            self.log.info("Using persistent chain datadir %s" % self.persistent_dir)
            for i in range(self.num_nodes):
                initialize_data_dir(self.options.tmpdir, i)
                self._clear_persisted_mempool(i)
            self.run_id = self._load_run_counter()
            self.log.info("Persistent run id %d (block height will accumulate)" % self.run_id)
        else:
            super().setup_chain()

    def _clear_persisted_mempool(self, node_index):
        mempool_path = os.path.join(
            self.options.tmpdir, "node%d" % node_index, "regtest", "mempool.dat")
        if os.path.isfile(mempool_path):
            os.remove(mempool_path)

    def setup_network(self):
        self.log.info("Running setup_network")
        self.setup_nodes()
        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        if not self.persistent_dir:
            self.sync_all()
            return
        try:
            self.sync_all()
        except AssertionError:
            self.log.info("Startup mempool mismatch; clearing mempools")
            for node in self.nodes:
                node.clearmempool()
            try:
                self.sync_all()
            except AssertionError:
                self.log.info("Startup mismatch persisted; mining reconciliation block")
                self.nodes[0].generate(1)
                self.sync_all()

    def _counter_path(self):
        return os.path.join(self.options.tmpdir, "chain_accumulator_run_counter")

    def _height_path(self):
        return os.path.join(self.options.tmpdir, "chain_accumulator_height")

    def _load_run_counter(self):
        path = self._counter_path()
        if os.path.isfile(path):
            with open(path, "r", encoding="utf8") as f:
                return int(f.read().strip())
        return 0

    def _save_run_counter(self, value):
        with open(self._counter_path(), "w", encoding="utf8") as f:
            f.write(str(value))

    def _save_chain_height(self, height):
        if self.persistent_dir:
            with open(self._height_path(), "w", encoding="utf8") as f:
                f.write(str(height))

    def unique_tag(self, prefix):
        self.scenario_counter += 1
        base = "".join(c for c in prefix.upper() if c.isalnum())[:4]
        return "%s%04X%04X" % (base.ljust(4, "X"), self.tag_epoch & 0xFFFF, self.scenario_counter & 0xFFFF)

    def unique_qualifier(self, prefix):
        return "#" + self.unique_tag(prefix)

    def recover_persistent_state(self):
        if not self.persistent_dir:
            return
        try:
            self.sync_all()
        except AssertionError:
            self.log.info("Mempool out of sync from prior run; mining reconciliation block")
            self.nodes[0].generate(1)
            self.sync_all()

    def activate(self):
        n0 = self.nodes[0]
        info = n0.getblockchaininfo()
        forks = info['bip9_softforks']
        if (forks['assets']['status'] == "active"
                and forks['assetauth']['status'] == "active"
                and forks['messaging_restricted']['status'] == "active"):
            self.log.info("Forks already active at height %d — continuing chain" % info['blocks'])
            return
        self.log.info("Activating assets + assetauth + restricted (height %d)" % info['blocks'])
        needed = max(0, 432 - info['blocks'])
        if needed:
            n0.generate(needed)
            self.sync_all()
        info = n0.getblockchaininfo()
        assert_equal("active", info['bip9_softforks']['assets']['status'])
        assert_equal("active", info['bip9_softforks']['assetauth']['status'])
        assert_equal("active", info['bip9_softforks']['messaging_restricted']['status'])

    def _mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()

    def clear_p2ah_hops(self):
        self._hop_p2ahs = []

    def register_p2ah_hop(self, address):
        hops = getattr(self, '_hop_p2ahs', None)
        if hops is None:
            self._hop_p2ahs = []
            hops = self._hop_p2ahs
        if address not in hops:
            hops.append(address)

    def p2ah_rvn_balance(self, node, address):
        utxos = node.listassetauthutxos(address)
        return sum(float(u['amount']) for u in utxos if 'asset' not in u)

    def _top_off_registered_hops(self, node, chunk=50.0):
        hops = getattr(self, '_hop_p2ahs', None) or []
        if not hops:
            return
        for address in hops:
            node.sendtoaddress(address, chunk)
            self.log.info("Topped off P2AH hop %s with %.2f RVN (new coinbase)" % (
                address, chunk))
        self._mine(1)

    def ensure_p2ah_hop_funded(self, node, address, min_rvn=25.0, chunk=50.0):
        self.register_p2ah_hop(address)
        self.ensure_spendable_rvn(node, min_balance=max(15000, chunk + 2000))
        bal = self.p2ah_rvn_balance(node, address)
        if bal >= min_rvn:
            return
        if float(node.getbalance()) < chunk + 1:
            self.ensure_spendable_rvn(node, min_balance=chunk + 5000)
            bal = self.p2ah_rvn_balance(node, address)
            if bal >= min_rvn:
                return
        self.log.info(
            "P2AH hop %s low (%.8f < %.2f); sending %.2f RVN"
            % (address, bal, min_rvn, chunk))
        node.sendtoaddress(address, chunk)
        self._mine(1)

    def ensure_spendable_rvn(self, node, min_balance=15000):
        """Mine coinbase into this wallet until spendable balance is enough.

        generate()/generatetoaddress already pays the mining wallet. The usual
        failure mode is immature coinbase (100-block maturity) or a long chain
        where subsidy has already collapsed after many halvings.

        When mining runs, also top off registered P2AH hop addresses.
        """
        bal = float(node.getbalance())
        if bal >= min_balance:
            return
        self.log.info(
            "Spendable RVN low (%.2f < %.2f); mining coinbase into wallet"
            % (bal, min_balance))
        addr = node.getnewaddress()
        stalled = 0
        mined = False
        while float(node.getbalance()) < min_balance:
            before = float(node.getbalance())
            # 101 blocks unlocks the first new coinbase of this batch.
            node.generatetoaddress(101, addr)
            mined = True
            self.sync_all()
            after = float(node.getbalance())
            if after <= before + 1.0:
                stalled += 1
            else:
                stalled = 0
            if stalled >= 2:
                raise RuntimeError(
                    "Cannot fund wallet by mining: balance stuck near %.8f "
                    "(need %.2f). Regtest burn has exhausted practical coinbase."
                    % (after, min_balance))
        self.log.info("Wallet funded to %.2f RVN by mining" % float(node.getbalance()))
        if mined:
            self._top_off_registered_hops(node)

    def ensure_wallets_funded(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        # Core tick burns roughly ~12k RVN in asset-issue fees.
        self.ensure_spendable_rvn(n0, min_balance=15000)
        if float(n1.getbalance()) < 100:
            n0.sendtoaddress(n1.getnewaddress(), 500)
            self._mine(1)

    def tick_fungible_transfer_reissue(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        root = self.unique_tag("FUNG")
        ipfs = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        addr = n0.getnewaddress()
        n0.issue(root, 5000, addr, "", 4, True, True, ipfs)
        self._mine(1)
        dest = n1.getnewaddress()
        n0.transfer(root, 500, dest)
        self._mine(1)
        total_before = n0.getassetdata(root)['amount']
        n0.reissue(root, 1000, addr, "", True, -1, ipfs)
        self._mine(1)
        assert_equal(total_before + 1000, n0.getassetdata(root)['amount'])
        assert_equal(500, n1.listmyassets(root, True)[root]['balance'])
        self.log.info("Fungible %s transfer + reissue ok" % root)

    def tick_sub_asset(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        root = self.unique_tag("ROOT")
        sub = root + "/SUBA"
        n0.issue(root, 1000)
        self._mine(1)
        n0.issue(sub, 2000)
        self._mine(1)
        dest = n1.getnewaddress()
        n0.transfer(sub, 300, dest)
        self._mine(1)
        assert_equal(300, n1.listmyassets(sub, True)[sub]['balance'])
        self.log.info("Sub-asset %s ok" % sub)

    def tick_unique_batch(self):
        n0 = self.nodes[0]
        root = self.unique_tag("UNIQ")
        n0.issue(root)
        self._mine(1)
        tags = ["A", "B"]
        ipfs = ["QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"] * 2
        n0.issueunique(root, tags, ipfs)
        self._mine(1)
        for tag in tags:
            name = "%s#%s" % (root, tag)
            assert_equal(1, n0.listmyassets(name)[name])
        self.log.info("Unique batch on %s ok" % root)

    def tick_sub_unique(self):
        n0 = self.nodes[0]
        root = self.unique_tag("SBUQ")
        sub = root + "/CHILD"
        uniq = sub + "#ONE"
        n0.issue(root, 100)
        self._mine(1)
        n0.issue(sub, 50)
        self._mine(1)
        n0.issue(uniq)
        self._mine(1)
        assert_equal(1, n0.listmyassets(uniq)[uniq])
        self.log.info("Sub+unique %s ok" % uniq)

    def tick_qualifier_restricted(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        qual = self.unique_qualifier("QUAL")
        base = self.unique_tag("REST")
        restricted = "$" + base
        n0.issuequalifierasset(qual, 10)
        n0.issue(base, 100)
        self._mine(1)
        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        self._mine(1)
        n0.issuerestrictedasset(restricted, 8000, qual, dest)
        self._mine(1)
        recv = n1.getnewaddress()
        n0.addtagtoaddress(qual, recv)
        self._mine(1)
        n0.transfer(restricted, 250, recv)
        self._mine(1)
        assert_equal(250, n1.listmyassets(restricted, True)[restricted]['balance'])
        self.log.info("Qualifier-gated %s ok" % restricted)

    def tick_restricted_true(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        base = self.unique_tag("TRUE")
        restricted = "$" + base
        n0.issue(base, 50)
        self._mine(1)
        addr = n0.getnewaddress()
        n0.issuerestrictedasset(restricted, 5000, "true", addr)
        self._mine(1)
        recv = n1.getnewaddress()
        n0.transfer(restricted, 100, recv)
        self._mine(1)
        assert_equal(100, n1.listmyassets(restricted, True)[restricted]['balance'])
        self.log.info("Verifier-true %s ok" % restricted)

    def tick_restricted_reissue(self):
        n0 = self.nodes[0]
        base = self.unique_tag("RISU")
        restricted = "$" + base
        addr = n0.getnewaddress()
        n0.issue(base, 10)
        self._mine(1)
        n0.issuerestrictedasset(restricted, 1000, "true", addr)
        self._mine(1)
        n0.reissuerestrictedasset(restricted, 500, addr, False)
        self._mine(1)
        assert_equal(1500, n0.getassetdata(restricted)['amount'])
        self.log.info("Restricted reissue %s ok" % restricted)

    def tick_freeze_unfreeze(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        base = self.unique_tag("FRZ")
        restricted = "$" + base
        addr = n0.getnewaddress()
        n0.issue(base, 10)
        self._mine(1)
        n0.issuerestrictedasset(restricted, 2000, "true", addr)
        self._mine(1)
        recv = n1.getnewaddress()
        change = n0.getnewaddress()
        n0.freezerestrictedasset(restricted, change)
        self._mine(1)
        assert_raises_rpc_error(-8, None, n0.transfer, restricted, 10, recv)
        n0.unfreezerestrictedasset(restricted, change)
        self._mine(1)
        n0.transfer(restricted, 10, recv)
        self._mine(1)
        assert_equal(10, n1.listmyassets(restricted, True)[restricted]['balance'])
        self.log.info("Freeze/unfreeze %s ok" % restricted)

    def tick_messaging_channel(self):
        n0 = self.nodes[0]
        root = self.unique_tag("MSG")
        channel = root + "~CHAN"
        ipfs = "QmZPGfJojdTzaqCWJu2m3krark38X1rqEHBo4SjeqHKB26"
        n0.issue(root, 100)
        self._mine(1)
        n0.issue(channel)
        self._mine(1)
        n0.sendmessage(root + "!", ipfs)
        self._mine(1)
        msgs = n0.viewallmessages()
        assert len(msgs) >= 1
        n0.clearmessages()
        self.log.info("Messaging on %s ok" % root)

    def tick_owner_handoff(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        root = self.unique_tag("OWNR")
        owner = root + "!"
        n0.issue(root, 1000)
        self._mine(1)
        recv = n1.getnewaddress()
        n0.transfer(owner, 1, recv)
        self._mine(1)
        assert_equal(1, n1.listmyassets(owner, True)[owner]['balance'])
        self.log.info("Owner handoff %s ok" % owner)

    def tick_p2ah_rvn_and_asset(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        self.clear_p2ah_hops()
        tag = self.unique_tag("P2AH")
        owner = tag + "!"
        n0.issue(tag, 500)
        self._mine(1)
        p2ah = n0.addassetauthaddress(1, [owner])['address']
        self.ensure_p2ah_hop_funded(n0, p2ah, min_rvn=40.0, chunk=50.0)
        n0.transfer(tag, 100, p2ah)
        self._mine(1)
        dest_rvn = n1.getnewaddress()
        dest_asset = n1.getnewaddress()
        spend_rvn = n0.spendassetauth(p2ah, {dest_rvn: 10})
        assert_equal(spend_rvn['owner_assets_moved'], [owner])
        self._mine(1)
        self.ensure_p2ah_hop_funded(n0, p2ah, min_rvn=25.0, chunk=50.0)
        spend_asset = n0.spendassetauth(p2ah, {dest_asset: {'transfer': {tag: 40}}})
        assert_equal(spend_asset['owner_assets_moved'], [owner])
        self._mine(1)
        assert_equal(40, n1.listmyassets(tag, True)[tag]['balance'])
        self.log.info("P2AH RVN+asset spend on %s ok" % tag)

    def tick_restricted_p2ah_custody(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        self.clear_p2ah_hops()
        qual = self.unique_qualifier("P2RQ")
        base = self.unique_tag("P2RB")
        restricted = "$" + base
        owner = base + "!"
        n0.issuequalifierasset(qual, 5)
        n0.issue(base, 50)
        self._mine(1)
        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        self._mine(1)
        n0.issuerestrictedasset(restricted, 3000, qual, dest)
        self._mine(1)
        p2ah = n0.addassetauthaddress(1, [owner])['address']
        n0.addtagtoaddress(qual, p2ah)
        self._mine(1)
        n0.transfer(restricted, 200, p2ah)
        self.ensure_p2ah_hop_funded(n0, p2ah, min_rvn=25.0, chunk=40.0)
        recv = n1.getnewaddress()
        n0.addtagtoaddress(qual, recv)
        self._mine(1)
        n0.spendassetauth(p2ah, {recv: {'transfer': {restricted: 75}}})
        self._mine(1)
        assert_equal(75, n1.listmyassets(restricted, True)[restricted]['balance'])
        self.log.info("Restricted P2AH custody %s ok" % restricted)

    def tick_failure_matrix(self):
        """Exercise many rejection paths on one shared fixture (low extra burn)."""
        n0, n1 = self.nodes[0], self.nodes[1]
        self.ensure_spendable_rvn(n0, min_balance=5000)

        root = self.unique_tag("FAIL")
        qual = self.unique_qualifier("FAIL")
        restricted = "$" + root
        uniq = root + "#DUP"
        addr = n0.getnewaddress()
        bad = "not_a_raven_address"
        n1_addr = n1.getnewaddress()

        n0.issue(root, 1000)
        n0.issuequalifierasset(qual, 5)
        self._mine(1)
        n0.addtagtoaddress(qual, addr)
        self._mine(1)
        n0.issuerestrictedasset(restricted, 1000, qual, addr)
        n0.issue(uniq)
        self._mine(1)

        # Duplicate names
        assert_raises_rpc_error(-8, None, n0.issue, root, 1)
        assert_raises_rpc_error(-8, None, n0.issue, uniq)
        assert_raises_rpc_error(None, None, n0.issuerestrictedasset, restricted, 1, "true", addr)

        # Unique without owning the root owner token
        orphan_root = self.unique_tag("ORPH")
        assert_raises_rpc_error(-32600, None, n0.issue, orphan_root + "#X")

        # Bad destinations / over-transfer
        assert_raises_rpc_error(None, None, n0.transfer, root, 1, bad)
        assert_raises_rpc_error(None, None, n0.transfer, root, 10**12, n1_addr)

        # Restricted verifier gate: untagged peer must fail
        assert_raises_rpc_error(-8, None, n0.transfer, restricted, 1, n1_addr)

        # Global freeze blocks transfer
        change = n0.getnewaddress()
        n0.freezerestrictedasset(restricted, change)
        self._mine(1)
        assert_raises_rpc_error(-8, None, n0.transfer, restricted, 1, addr)
        n0.unfreezerestrictedasset(restricted, change)
        self._mine(1)

        # Double-tag same address
        assert_raises_rpc_error(-32600, None, n0.addtagtoaddress, qual, addr)

        # Bad restricted verifier at issue time
        bad_base = self.unique_tag("BADV")
        n0.issue(bad_base, 10)
        self._mine(1)
        assert_raises_rpc_error(
            None, None, n0.issuerestrictedasset, "$" + bad_base, 1, "#NONEXISTENTTAG", addr)

        # Reissue without ownership: send owner away, then fail reissue
        owner = root + "!"
        n0.transfer(owner, 1, n1_addr)
        self._mine(1)
        assert_raises_rpc_error(None, None, n0.reissue, root, 1, addr)
        # Restore owner so later ticks stay healthy if they touch this wallet state
        n1.transfer(owner, 1, n0.getnewaddress())
        self._mine(1)

        self.log.info("Failure matrix ok on fixture %s / %s" % (root, restricted))

    def run_one_tick(self, tick_index, tick_total):
        scenarios = [
            ("fungible transfer/reissue", self.tick_fungible_transfer_reissue),
            ("sub-asset", self.tick_sub_asset),
            ("unique batch", self.tick_unique_batch),
            ("sub+unique", self.tick_sub_unique),
            ("qualifier restricted", self.tick_qualifier_restricted),
            ("restricted true", self.tick_restricted_true),
            ("restricted reissue", self.tick_restricted_reissue),
            ("freeze/unfreeze", self.tick_freeze_unfreeze),
            ("messaging", self.tick_messaging_channel),
            ("owner handoff", self.tick_owner_handoff),
            ("failure matrix", self.tick_failure_matrix),
        ]
        if self.include_p2ah:
            scenarios.extend([
                ("P2AH RVN+asset", self.tick_p2ah_rvn_and_asset),
                ("restricted P2AH custody", self.tick_restricted_p2ah_custody),
            ])
        self.log.info("=== tick %d/%d (%d scenarios) ===" % (tick_index, tick_total, len(scenarios)))
        for name, fn in scenarios:
            self.clear_p2ah_hops()
            self.log.info("--- %s ---" % name)
            self.ensure_spendable_rvn(self.nodes[0], min_balance=5000)
            fn()

    def run_test(self):
        self.ticks = max(1, int(getattr(self.options, 'ticks', 1)))
        self.include_p2ah = bool(getattr(self.options, 'include_p2ah', False))

        self.activate()
        self.ensure_wallets_funded()
        if self.persistent_dir:
            self._save_run_counter(self.run_id + 1)
        self.recover_persistent_state()

        start_height = self.nodes[0].getblockcount()
        self.tag_epoch = (start_height << 4) | (self.run_id & 0xF)
        self.scenario_counter = 0
        self.log.info("Accumulator run %d: %d tick(s), height %d (tag epoch 0x%X)" % (
            self.run_id, self.ticks, start_height, self.tag_epoch))

        for i in range(self.ticks):
            self.ensure_wallets_funded()
            self.run_one_tick(i + 1, self.ticks)

        end_height = self.nodes[0].getblockcount()
        self.log.info("Accumulator done: height %d -> %d (+%d blocks), run id %d" % (
            start_height, end_height, end_height - start_height, self.run_id))

        if self.persistent_dir:
            self._save_chain_height(end_height)


if __name__ == '__main__':
    ChainAccumulatorTest().main()
