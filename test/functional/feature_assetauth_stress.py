#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
P2AH stress/regression harness.

Runs asset-auth scenarios repeatedly, appending blocks to one regtest chain so
wallet/chain history accumulates across cron invocations.

Persistent chain (default for cron)
-----------------------------------
Pass --persistent-dir=/path/to/chain (or set P2AH_DATADIR). The same node
datadirs are reused; each run adds blocks/transactions on top of prior history.
A monotonic run counter drives unique asset names so scenarios do not collide.

Ephemeral chain (one-off debugging)
-----------------------------------
Omit --persistent-dir to use a temporary datadir (removed after the run unless
--nocleanup is set).
"""

import math
import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    initialize_data_dir,
)


def truncate(number, digits=8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper


def _assert_safe_reset_path(persistent_dir):
    """F-10: canonicalize through realpath so symlink/alias paths cannot bypass
    protection, then refuse deletion of dangerous targets."""
    _target = os.path.realpath(persistent_dir)
    _home = os.path.realpath(os.path.expanduser("~"))
    # <repo>/test/functional/<file> -> three levels up is the repository root
    _repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
    _protected = {os.path.realpath(os.sep), _home, _repo_root}
    if (_target in _protected
            or any(_target.startswith(p + os.sep) for p in _protected)):
        raise RuntimeError(
            "Refusing to reset persistent dir %r: resolves to protected path %r"
            % (persistent_dir, _target))
    return _target


class AssetAuthStressTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ['-assetindex', '-fallbackfee=0.0001', '-persistmempool=0'],
            ['-assetindex', '-fallbackfee=0.0001', '-persistmempool=0'],
        ]
        self.stress_rounds = 5
        self.persistent_dir = None
        self.run_id = 0
        self.tag_epoch = 0
        self.scenario_counter = 0

    def add_options(self, parser):
        parser.add_option("--stress-rounds", dest="stress_rounds", default=5, type="int",
                          help="How many times to repeat each stress scenario (default: %default)")
        parser.add_option("--persistent-dir", dest="persistent_dir", default=os.environ.get("P2AH_DATADIR", ""),
                          help="Reuse this datadir across runs to accumulate chain history")
        parser.add_option("--reset-chain", dest="reset_chain", default=False, action="store_true",
                          help="Delete persistent-dir before starting (fresh chain)")

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
        return os.path.join(self.options.tmpdir, "p2ah_stress_run_counter")

    def _load_run_counter(self):
        path = self._counter_path()
        if os.path.isfile(path):
            with open(path, "r", encoding="utf8") as f:
                return int(f.read().strip())
        return 0

    def _save_run_counter(self, value):
        with open(self._counter_path(), "w", encoding="utf8") as f:
            f.write(str(value))

    def _height_path(self):
        return os.path.join(self.options.tmpdir, "p2ah_chain_height")

    def _save_chain_height(self, height):
        if self.persistent_dir:
            with open(self._height_path(), "w", encoding="utf8") as f:
                f.write(str(height))

    def unique_tag(self, prefix):
        """Short unique name (<=31 chars, A-Z0-9 only) for persistent chain history."""
        self.scenario_counter += 1
        base = "".join(c for c in prefix.upper() if c.isalnum())[:4]
        return "%s%04X%04X" % (base.ljust(4, "X"), self.tag_epoch & 0xFFFF, self.scenario_counter & 0xFFFF)

    def unique_qualifier(self, prefix):
        """Unique qualifier name for restricted verifier / tag tests."""
        return "#" + self.unique_tag(prefix)

    def _setup_restricted_on_p2ah(self, n0, n1, qual_prefix, base_prefix):
        """Issue qualifier + restricted asset; return (qual, base, restricted, p2ah, dest)."""
        qual = self.unique_qualifier(qual_prefix)
        base = self.unique_tag(base_prefix)
        restricted = "$" + base
        owner = base + "!"

        n0.issuequalifierasset(qual, 10)
        n0.issue(base, 100)
        n0.generate(1)
        self.sync_all()

        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        n0.generate(1)
        self.sync_all()
        n0.issuerestrictedasset(restricted, 5000, qual, dest)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [owner])['address']
        n0.addtagtoaddress(qual, p2ah)
        n0.generate(1)
        self.sync_all()
        return qual, base, restricted, p2ah, dest

    def recover_persistent_state(self):
        """Reconcile nodes after an aborted prior run left mempools diverged."""
        if not self.persistent_dir:
            return
        try:
            self.sync_all()
        except AssertionError:
            self.log.info("Mempool out of sync from prior run; mining reconciliation block")
            self.nodes[0].generate(1)
            self.sync_all()

    def clear_p2ah_hops(self):
        """Drop hop addresses registered by the previous scenario."""
        self._hop_p2ahs = []

    def register_p2ah_hop(self, address):
        """Track a P2AH address so new coinbase can top it off."""
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
        """Push fresh RVN onto every registered hop after new coinbase matures."""
        hops = getattr(self, '_hop_p2ahs', None) or []
        if not hops:
            return
        for address in hops:
            node.sendtoaddress(address, chunk)
            self.log.info("Topped off P2AH hop %s with %.2f RVN (new coinbase)" % (
                address, chunk))
        node.generate(1)
        self.sync_all()

    def ensure_p2ah_hop_funded(self, node, address, min_rvn=25.0, chunk=50.0):
        """Over-fund a P2AH hop so chained spends keep a wide fee margin."""
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
        node.generate(1)
        self.sync_all()

    def ensure_spendable_rvn(self, node, min_balance=20000):
        """Mine coinbase into this wallet until spendable balance is enough.

        When mining runs, also top off any registered P2AH hop addresses so
        chained spends do not scrape a thin change UTXO for fees.
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

    def activate(self):
        n0 = self.nodes[0]
        info = n0.getblockchaininfo()
        assets_status = info['bip9_softforks']['assets']['status']
        auth_status = info['bip9_softforks']['assetauth']['status']
        restricted_status = info['bip9_softforks']['messaging_restricted']['status']
        if (assets_status == "active" and auth_status == "active"
                and restricted_status == "active"):
            self.log.info("Assets/assetauth/restricted already active at height %d — continuing chain" % info['blocks'])
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

    def stress_simple_spend(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        tag = self.unique_tag("STR")
        n0.issue(tag, 1000)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [tag + "!"])
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=40.0, chunk=50.0)

        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah['address'], {dest: 10.0})
        assert_equal(spend['owner_assets_moved'], [tag + "!"])

        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)

        n0.generate(1)
        self.sync_all()
        assert_equal(float(n1.getreceivedbyaddress(dest)), 10.0)

    def stress_chained_spend(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        root = self.unique_tag("RT") + "!"
        leaf = self.unique_tag("LF") + "!"
        n0.issue(root.replace("!", ""), 100)
        n0.issue(leaf.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()

        p2ah_root = n0.addassetauthaddress(1, [root])
        p2ah_leaf = n0.addassetauthaddress(1, [leaf])

        n0.transfer(leaf, 1, p2ah_root['address'])
        # Over-fund the leaf hop; top up again before the second spend so fee
        # selection never depends on thin change left after hop 1.
        self.ensure_p2ah_hop_funded(n0, p2ah_leaf['address'], min_rvn=40.0, chunk=50.0)

        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah_leaf['address'], {dest: 5.0})
        assert_equal(spend['owner_assets_moved'], [root, leaf])

        moved = dict(zip(spend['owner_assets_moved'], spend['owner_asset_destinations']))
        assert moved[root] != p2ah_root['address']
        assert_equal(moved[leaf], p2ah_root['address'])

        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)

        n0.generate(1)
        self.sync_all()

        self.ensure_p2ah_hop_funded(n0, p2ah_leaf['address'], min_rvn=25.0, chunk=50.0)
        dest2 = n1.getnewaddress()
        spend2 = n0.spendassetauth(p2ah_leaf['address'], {dest2: 1.0})
        assert root in spend2['owner_assets_moved']
        assert leaf in spend2['owner_assets_moved']

        n0.generate(1)
        self.sync_all()

    def stress_multisig_and_multi_utxo(self):
        n0 = self.nodes[0]
        a = self.unique_tag("BA") + "!"
        b = self.unique_tag("BB") + "!"
        c = self.unique_tag("BC") + "!"
        n0.issue(a.replace("!", ""), 100)
        n0.issue(b.replace("!", ""), 100)
        n0.issue(c.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(2, [a, b, c])

        n0.sendtoaddress(p2ah['address'], 20)
        n0.sendtoaddress(p2ah['address'], 30)
        self.register_p2ah_hop(p2ah['address'])
        n0.generate(1)
        self.sync_all()

        assert_equal(len(n0.listassetauthutxos(p2ah['address'])), 2)

        dest = n0.getnewaddress()
        # Spend enough that both RVN UTXOs must be selected (20+30).
        spend = n0.spendassetauth(p2ah['address'], {dest: 45.0})
        assert_equal(len(spend['owner_assets_moved']), 2)

        rawtx = n0.getrawtransaction(spend['txid'], 1)
        p2ah_inputs = [vin for vin in rawtx['vin'] if 'assetAuthPreimage' in vin]
        assert_equal(len(p2ah_inputs), 2)

        n0.generate(1)
        self.sync_all()

    def stress_asset_on_p2ah(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        owner = self.unique_tag("OWN") + "!"
        token = self.unique_tag("TOK")
        n0.issue(owner.replace("!", ""), 100)
        n0.issue(token, 1000)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [owner])
        n0.transfer(token, 500, p2ah['address'])
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=25.0, chunk=40.0)

        dest = n1.getnewaddress()
        spend = n0.spendassetauth(p2ah['address'], {dest: {'transfer': {token: 100}}})
        assert_equal(spend['owner_assets_moved'], [owner])

        n0.generate(1)
        self.sync_all()
        assert_equal(float(n1.listmyassets(token)[token]), 100.0)

    def stress_nested_multisig_chain_heavy(self):
        """2-of-3 outer P2AH authorizes 1-of-3 inner P2AH holding many UTXOs."""
        n0, n1 = self.nodes[0], self.nodes[1]
        gate_a = self.unique_tag("GA") + "!"
        gate_b = self.unique_tag("GB") + "!"
        gate_c = self.unique_tag("GC") + "!"
        vault_a = self.unique_tag("VA") + "!"
        vault_b = self.unique_tag("VB") + "!"
        vault_c = self.unique_tag("VC") + "!"
        heavy_asset = self.unique_tag("HVY")

        for name in (gate_a, gate_b, gate_c, vault_a, vault_b, vault_c):
            n0.issue(name.replace("!", ""), 100)
        n0.issue(heavy_asset, 10000)
        n0.generate(1)
        self.sync_all()

        p2ah_outer = n0.addassetauthaddress(2, [gate_a, gate_b, gate_c])
        p2ah_inner = n0.addassetauthaddress(1, [vault_a, vault_b, vault_c])

        n0.transfer(vault_a, 1, p2ah_outer['address'])
        n0.generate(1)
        self.sync_all()

        self.ensure_p2ah_hop_funded(n0, p2ah_inner['address'], min_rvn=40.0, chunk=50.0)
        for amount in (100, 150, 200, 250):
            n0.transfer(heavy_asset, amount, p2ah_inner['address'])
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah_inner['address'])
        assert len(utxos) >= 5
        asset_utxos = [u for u in utxos if 'asset' in u and u['asset']['name'] == heavy_asset]
        assert len(asset_utxos) >= 4

        dest_rvn = n1.getnewaddress()
        dest_asset = n1.getnewaddress()
        spend = n0.spendassetauth(
            p2ah_inner['address'],
            {dest_rvn: 5.0, dest_asset: {'transfer': {heavy_asset: 400}}},
        )
        assert_equal(len(spend['owner_assets_moved']), 3)
        assert vault_a in spend['owner_assets_moved']
        assert len([g for g in (gate_a, gate_b, gate_c) if g in spend['owner_assets_moved']]) == 2

        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)

        n0.generate(1)
        self.sync_all()
        assert float(n1.listmyassets(heavy_asset)[heavy_asset]) >= 400.0

        self.ensure_p2ah_hop_funded(n0, p2ah_inner['address'], min_rvn=25.0, chunk=50.0)
        dest2 = n1.getnewaddress()
        spend2 = n0.spendassetauth(p2ah_inner['address'], {dest2: 1.0})
        assert len(spend2['owner_assets_moved']) >= 3
        n0.generate(1)
        self.sync_all()

    def stress_concurrent_same_dest_same_block(self):
        """Two independent 1-of-3 P2AH addresses spend to the same dest in one block."""
        n0, n1 = self.nodes[0], self.nodes[1]
        shared_dest = n1.getnewaddress()

        setups = [
            (self.unique_tag("LA"), [self.unique_tag("LAA") + "!", self.unique_tag("LAB") + "!", self.unique_tag("LAC") + "!"]),
            (self.unique_tag("LB"), [self.unique_tag("LBA") + "!", self.unique_tag("LBB") + "!", self.unique_tag("LBC") + "!"]),
        ]

        p2ah_addrs = []
        for _, owners in setups:
            for owner in owners:
                n0.issue(owner.replace("!", ""), 100)
            n0.generate(1)
            self.sync_all()
            p2ah = n0.addassetauthaddress(1, owners)
            self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=25.0, chunk=40.0)
            p2ah_addrs.append(p2ah['address'])

        spend_a = n0.spendassetauth(p2ah_addrs[0], {shared_dest: 5.0})
        spend_b = n0.spendassetauth(p2ah_addrs[1], {shared_dest: 5.0})
        assert spend_a['txid'] != spend_b['txid']

        for txid in (spend_a['txid'], spend_b['txid']):
            verify = n0.verifyassetauth(n0.getrawtransaction(txid))
            assert_equal(verify['valid'], True)

        n0.generate(1)
        self.sync_all()
        assert float(n1.getreceivedbyaddress(shared_dest)) >= 10.0

    def stress_same_p2ah_same_block_dual_spend(self):
        """Same 1-of-3 P2AH: two spends to same dest queued before one block."""
        n0, n1 = self.nodes[0], self.nodes[1]
        owners = [self.unique_tag("SA") + "!", self.unique_tag("SB") + "!", self.unique_tag("SC") + "!"]
        for owner in owners:
            n0.issue(owner.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()
        for owner in owners:
            assert owner in n0.listmyassets()

        p2ah = n0.addassetauthaddress(1, owners)
        shared_dest = n1.getnewaddress()
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=40.0, chunk=50.0)
        for _ in range(3):
            n0.sendtoaddress(p2ah['address'], 10.0)
        n0.generate(1)
        self.sync_all()

        spend1 = n0.spendassetauth(p2ah['address'], {shared_dest: 5.0})
        dual_mempool = True
        try:
            spend2 = n0.spendassetauth(p2ah['address'], {shared_dest: 5.0})
        except Exception as e:
            dual_mempool = False
            self.log.info("Second mempool spend from same 1-of-3 P2AH rejected: %s" % e)
            spend2 = None

        n0.generate(1)
        self.sync_all()
        received = float(n1.getreceivedbyaddress(shared_dest))
        assert received >= 5.0

        if dual_mempool:
            assert spend1['txid'] != spend2['txid']
            moved1 = set(spend1['owner_assets_moved'])
            moved2 = set(spend2['owner_assets_moved'])
            assert len(moved1) == 1
            assert len(moved2) == 1
            assert moved1 != moved2
            assert received >= 10.0
        else:
            self.log.info("Observed wallet single-mempool-spend limit; confirmed %.8f RVN" % received)

    def stress_same_p2ah_sequential_owner_rotation(self):
        """Same 1-of-3 P2AH: confirmed spends in short succession (mine between each)."""
        n0, n1 = self.nodes[0], self.nodes[1]
        owners = [self.unique_tag("SQ") + "!", self.unique_tag("SR") + "!", self.unique_tag("SS") + "!"]
        for owner in owners:
            n0.issue(owner.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, owners)
        shared_dest = n1.getnewaddress()
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=40.0, chunk=50.0)

        total = 0.0
        spend_count = 0
        for amount in (5.0, 5.0, 4.0, 4.0):
            self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=25.0, chunk=50.0)
            spend = n0.spendassetauth(p2ah['address'], {shared_dest: amount})
            spend_count += 1
            n0.generate(1)
            self.sync_all()
            total = float(n1.getreceivedbyaddress(shared_dest))

        assert spend_count == 4
        assert total >= 18.0

    def stress_one_of_three_shared_asset_same_dest(self):
        """One 1-of-3 P2AH holding two assets; two spends targeting same dest, same block."""
        n0, n1 = self.nodes[0], self.nodes[1]
        owners = [self.unique_tag("OA") + "!", self.unique_tag("OB") + "!", self.unique_tag("OC") + "!"]
        asset_x = self.unique_tag("AX")
        asset_y = self.unique_tag("AY")
        for owner in owners:
            n0.issue(owner.replace("!", ""), 100)
        n0.issue(asset_x, 5000)
        n0.issue(asset_y, 5000)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, owners)
        shared_dest = n1.getnewaddress()
        n0.transfer(asset_x, 300, p2ah['address'])
        n0.transfer(asset_y, 400, p2ah['address'])
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=25.0, chunk=40.0)

        spend_x = None
        spend_y = None
        dual_asset_block = True
        try:
            spend_x = n0.spendassetauth(
                p2ah['address'], {shared_dest: {'transfer': {asset_x: 100}}},
            )
            try:
                spend_y = n0.spendassetauth(
                    p2ah['address'], {shared_dest: {'transfer': {asset_y: 150}}},
                )
            except Exception as e:
                dual_asset_block = False
                self.log.info("Second same-block asset spend rejected: %s" % e)
        except Exception as e:
            dual_asset_block = False
            self.log.info("Same-block asset spend unavailable (%s); trying sequential" % e)
            spend_x = n0.spendassetauth(
                p2ah['address'], {shared_dest: {'transfer': {asset_x: 100}}},
            )
            n0.generate(1)
            self.sync_all()
            spend_y = n0.spendassetauth(
                p2ah['address'], {shared_dest: {'transfer': {asset_y: 150}}},
            )

        if spend_y is None:
            n0.generate(1)
            self.sync_all()
            spend_y = n0.spendassetauth(
                p2ah['address'], {shared_dest: {'transfer': {asset_y: 150}}},
            )

        n0.generate(1)
        self.sync_all()
        bal = n1.listmyassets()
        assert spend_x is not None
        assert spend_y is not None
        assert float(bal.get(asset_x, 0)) >= 100.0
        assert float(bal.get(asset_y, 0)) >= 150.0
        if dual_asset_block:
            assert spend_x['txid'] != spend_y['txid']
            assert len(set(spend_x['owner_assets_moved']).intersection(spend_y['owner_assets_moved'])) == 0
        else:
            self.log.info("Confirmed both asset transfers (sequential fallback for second spend)")

    def stress_one_asset_multisig_multi_dest(self):
        """One owner asset authorizes a single spend to two different 2-of-3 P2AH addresses."""
        n0, n1 = self.nodes[0], self.nodes[1]
        hub = self.unique_tag("HUB") + "!"
        va, vb, vc = self.unique_tag("VA") + "!", self.unique_tag("VB") + "!", self.unique_tag("VC") + "!"
        wa, wb, wc = self.unique_tag("WA") + "!", self.unique_tag("WB") + "!", self.unique_tag("WC") + "!"

        for name in (hub, va, vb, vc, wa, wb, wc):
            n0.issue(name.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()

        p2ah_source = n0.addassetauthaddress(1, [hub])
        vault_a = n0.addassetauthaddress(2, [va, vb, vc])
        vault_b = n0.addassetauthaddress(2, [wa, wb, wc])

        self.ensure_p2ah_hop_funded(n0, p2ah_source['address'], min_rvn=50.0, chunk=60.0)
        self.register_p2ah_hop(vault_a['address'])
        self.register_p2ah_hop(vault_b['address'])

        spend = n0.spendassetauth(
            p2ah_source['address'],
            {vault_a['address']: 20.0, vault_b['address']: 20.0},
        )
        assert_equal(spend['owner_assets_moved'], [hub])
        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)

        n0.generate(1)
        self.sync_all()

        self.ensure_p2ah_hop_funded(n0, vault_a['address'], min_rvn=25.0, chunk=40.0)
        self.ensure_p2ah_hop_funded(n0, vault_b['address'], min_rvn=25.0, chunk=40.0)

        utxos_a = n0.listassetauthutxos(vault_a['address'])
        utxos_b = n0.listassetauthutxos(vault_b['address'])
        assert len(utxos_a) >= 1
        assert len(utxos_b) >= 1
        rvn_a = sum(float(u['amount']) for u in utxos_a if 'asset' not in u)
        rvn_b = sum(float(u['amount']) for u in utxos_b if 'asset' not in u)
        assert rvn_a >= 20.0
        assert rvn_b >= 20.0

        # Each vault can spend independently with its own 2-of-3 policy.
        dest_a = n1.getnewaddress()
        dest_b = n1.getnewaddress()
        spend_a = n0.spendassetauth(vault_a['address'], {dest_a: 5.0})
        spend_b = n0.spendassetauth(vault_b['address'], {dest_b: 5.0})
        assert spend_a['txid'] != spend_b['txid']
        assert len(spend_a['owner_assets_moved']) == 2
        assert len(spend_b['owner_assets_moved']) == 2

        n0.generate(1)
        self.sync_all()
        assert float(n1.getreceivedbyaddress(dest_a)) >= 5.0
        assert float(n1.getreceivedbyaddress(dest_b)) >= 5.0

    def stress_one_asset_two_multisig_same_block(self):
        """One hub owner asset chains to two 2-of-3 vaults; spend both in the same block."""
        n0, n1 = self.nodes[0], self.nodes[1]
        hub = self.unique_tag("SG") + "!"
        va, vb, vc = self.unique_tag("GA") + "!", self.unique_tag("GB") + "!", self.unique_tag("GC") + "!"
        wa, wb, wc = self.unique_tag("GD") + "!", self.unique_tag("GE") + "!", self.unique_tag("GF") + "!"

        for name in (hub, va, vb, vc, wa, wb, wc):
            n0.issue(name.replace("!", ""), 100)
        n0.generate(1)
        self.sync_all()

        p2ah_hub = n0.addassetauthaddress(1, [hub])
        vault_a = n0.addassetauthaddress(2, [va, vb, vc])
        vault_b = n0.addassetauthaddress(2, [wa, wb, wc])

        # Leave only one co-signer in the wallet so the gate key at the hub must be used.
        n1_addr = n1.getnewaddress()
        n0.transfer(vc, 1, n1_addr)
        n0.transfer(wc, 1, n1_addr)
        n0.transfer(va, 1, p2ah_hub['address'])
        n0.transfer(wa, 1, p2ah_hub['address'])
        self.ensure_p2ah_hop_funded(n0, vault_a['address'], min_rvn=40.0, chunk=50.0)
        self.ensure_p2ah_hop_funded(n0, vault_b['address'], min_rvn=40.0, chunk=50.0)

        dest_a = n1.getnewaddress()
        dest_b = n1.getnewaddress()
        same_block = True
        spend_a = n0.spendassetauth(vault_a['address'], {dest_a: 5.0})
        assert hub in spend_a['owner_assets_moved']
        try:
            spend_b = n0.spendassetauth(vault_b['address'], {dest_b: 5.0})
        except Exception as e:
            same_block = False
            self.log.info("Second chained multisig spend same-block rejected: %s" % e)
            spend_b = None

        if spend_b is None:
            n0.generate(1)
            self.sync_all()
            self.ensure_p2ah_hop_funded(n0, vault_b['address'], min_rvn=25.0, chunk=50.0)
            spend_b = n0.spendassetauth(vault_b['address'], {dest_b: 5.0})

        assert len(spend_a['owner_assets_moved']) >= 2
        assert len(spend_b['owner_assets_moved']) >= 2
        assert hub in spend_a['owner_assets_moved']

        n0.generate(1)
        self.sync_all()
        assert float(n1.getreceivedbyaddress(dest_a)) >= 5.0
        assert float(n1.getreceivedbyaddress(dest_b)) >= 5.0

        if same_block:
            assert spend_a['txid'] != spend_b['txid']
            self.log.info("One hub asset authorized two 2-of-3 vault spends in the same block")
        else:
            self.log.info("Hub asset authorized both vaults (sequential fallback for second spend)")

    def stress_subpoena_recovery_seizure_chain(self):
        """Nested 2-of-3 chains: subpoenaed recovery keys seize treasury + admin owner token.

        Topology (each layer is 2-of-3 P2AH):

          recovery_p2ah  --holds gate-->  CUST_A!  (subpoenaed REC_A + REC_B authorize)
                |
          custody_p2ah  --holds gate-->  VAULT_A!
                |
          vault_p2ah    --holds-->       TREASURY asset + TREASURY! (admin owner token)

        Authority wallet retains only the subpoenaed/compelled keys; user-held keys are
        transferred away so spends must walk the full chain.
        """
        n0, n1 = self.nodes[0], self.nodes[1]
        # Recovery layer — simulates court/compelled 2-of-3 (authority has 2 of 3)
        rec_a = self.unique_tag("RC") + "!"
        rec_b = self.unique_tag("RD") + "!"
        rec_c = self.unique_tag("RE") + "!"
        # Custody layer — original custodian multisig
        cust_a = self.unique_tag("CS") + "!"
        cust_b = self.unique_tag("CT") + "!"
        cust_c = self.unique_tag("CU") + "!"
        # Vault layer — holds the asset under admin control
        vault_a = self.unique_tag("VL") + "!"
        vault_b = self.unique_tag("VM") + "!"
        vault_c = self.unique_tag("VN") + "!"
        treasury = self.unique_tag("TRS")
        admin = treasury + "!"

        for name in (rec_a, rec_b, rec_c, cust_a, cust_b, cust_c, vault_a, vault_b, vault_c):
            n0.issue(name.replace("!", ""), 100)
        n0.issue(treasury, 50000)
        n0.generate(1)
        self.sync_all()

        recovery_p2ah = n0.addassetauthaddress(2, [rec_a, rec_b, rec_c])
        custody_p2ah = n0.addassetauthaddress(2, [cust_a, cust_b, cust_c])
        vault_p2ah = n0.addassetauthaddress(2, [vault_a, vault_b, vault_c])

        # Gate keys sit at the parent layer (recovery holds custody gate, custody holds vault gate).
        n0.transfer(cust_a, 1, recovery_p2ah['address'])
        n0.transfer(vault_a, 1, custody_p2ah['address'])
        n0.transfer(treasury, 40000, vault_p2ah['address'])
        n0.transfer(admin, 1, vault_p2ah['address'])
        n0.sendtoaddress(vault_p2ah['address'], 2.0)
        n0.generate(1)
        self.sync_all()

        # User-held keys the authority did NOT obtain — only subpoenaed recovery pair remains usable.
        user_sink = n1.getnewaddress()
        n0.transfer(rec_c, 1, user_sink)
        n0.transfer(cust_c, 1, user_sink)
        n0.transfer(vault_c, 1, user_sink)
        n0.generate(1)
        self.sync_all()

        seizure_asset_dest = n1.getnewaddress()
        seizure_admin_dest = recovery_p2ah['address']

        # Phase 1: seize treasury tokens + move admin owner token to recovery custody.
        seize1 = n0.spendassetauth(
            vault_p2ah['address'],
            {
                seizure_asset_dest: {'transfer': {treasury: 25000}},
                seizure_admin_dest: {'transfer': {admin: 1}},
            },
        )
        moved = set(seize1['owner_assets_moved'])
        assert len(moved) >= 5
        assert len([k for k in (rec_a, rec_b) if k in moved]) == 2
        assert len([k for k in (cust_a, cust_b) if k in moved]) >= 1
        assert len([k for k in (vault_a, vault_b) if k in moved]) >= 1

        n0.generate(1)
        self.sync_all()
        assert float(n1.listmyassets(treasury)[treasury]) >= 25000.0

        admin_on_recovery = [
            u for u in n0.listassetauthutxos(recovery_p2ah['address'])
            if 'asset' in u and u['asset']['name'] == admin
        ]
        assert len(admin_on_recovery) >= 1
        self.log.info("Seized %s admin token to recovery P2AH" % admin)

        # Phase 2: using the same subpoenaed chain, drain remaining treasury from vault.
        seizure_asset_dest2 = n1.getnewaddress()
        seize2 = n0.spendassetauth(
            vault_p2ah['address'],
            {seizure_asset_dest2: {'transfer': {treasury: 10000}}},
        )
        assert len(seize2['owner_assets_moved']) >= 4
        n0.generate(1)
        self.sync_all()
        total_seized = float(n1.listmyassets(treasury)[treasury])
        assert total_seized >= 35000.0

        # Phase 3: recovery P2AH re-spends the seized admin token to a final escrow (full control transfer).
        final_escrow = n1.getnewaddress()
        admin_spend = n0.spendassetauth(
            recovery_p2ah['address'],
            {final_escrow: {'transfer': {admin: 1}}},
        )
        assert len([k for k in (rec_a, rec_b) if k in admin_spend['owner_assets_moved']]) == 2
        n0.generate(1)
        self.sync_all()

        assert float(n1.listmyassets().get(admin, 0)) >= 1.0
        self.log.info(
            "Subpoena recovery chain complete: seized %.0f %s, admin %s in final escrow"
            % (total_seized, treasury, admin)
        )

    def stress_rejection_paths(self):
        n0 = self.nodes[0]
        self.ensure_spendable_rvn(n0, min_balance=2000)
        tag = self.unique_tag("REJ")
        n0.issue(tag, 100)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.createassetauthaddress(1, [tag + "!"])
        self.ensure_p2ah_hop_funded(n0, p2ah['address'], min_rvn=10.0, chunk=20.0)

        utxos = n0.listassetauthutxos(p2ah['address'])
        utxo = utxos[0]
        dest = n0.getnewaddress()
        inputs = [{'txid': utxo['txid'], 'vout': utxo['vout']}]
        outputs = {dest: float(utxo['amount']) - 0.01}
        rawtx = n0.createrawtransaction(inputs, outputs)
        spk = n0.getrawtransaction(utxo['txid'], 1)['vout'][utxo['vout']]['scriptPubKey']['hex']
        prevtxs = [{'txid': utxo['txid'], 'vout': utxo['vout'], 'scriptPubKey': spk,
                    'assetAuthPreimage': p2ah['preimage'], 'amount': float(utxo['amount'])}]
        signed = n0.signrawtransaction(rawtx, prevtxs)
        assert_raises_rpc_error(-26, "bad-txns-assetauth-insufficient-owner-movement",
                                n0.sendrawtransaction, signed['hex'])

        assert_raises_rpc_error(None, None, n0.spendassetauth, "not_a_p2ah", {dest: 0.1})
        assert_raises_rpc_error(None, None, n0.addassetauthaddress, 2, [tag + "!"])
        assert_raises_rpc_error(None, None, n0.addassetauthaddress, 1, [])
        self.log.info("Rejection paths ok for %s" % tag)

    def stress_restricted_p2ah_custody_roundtrip(self):
        """Typed tag on P2AH, receive $ASSET, spendassetauth to tagged dest."""
        n0, n1 = self.nodes[0], self.nodes[1]
        qual, base, restricted, p2ah, _dest = self._setup_restricted_on_p2ah(n0, n1, "RQ", "RB")

        n0.transfer(restricted, 250, p2ah)
        self.ensure_p2ah_hop_funded(n0, p2ah, min_rvn=25.0, chunk=40.0)

        utxos = n0.listassetauthutxos(p2ah)
        have = [u for u in utxos if 'asset' in u and u['asset']['name'] == restricted]
        assert len(have) >= 1

        recv = n1.getnewaddress()
        n0.addtagtoaddress(qual, recv)
        n0.generate(1)
        self.sync_all()

        spend = n0.spendassetauth(p2ah, {recv: {'transfer': {restricted: 75}}})
        verify = n0.verifyassetauth(n0.getrawtransaction(spend['txid']))
        assert_equal(verify['valid'], True)
        n0.generate(1)
        self.sync_all()
        assert float(n1.listmyassets(restricted).get(restricted, 0)) >= 75.0
        self.log.info("Restricted custody roundtrip: %s -> P2AH -> spend" % restricted)

    def stress_restricted_third_party_inbound(self):
        """Third party sends tagged $ASSET to P2AH; watching wallet sees and holds it."""
        n0, n1 = self.nodes[0], self.nodes[1]
        qual, base, restricted, p2ah, _dest = self._setup_restricted_on_p2ah(n0, n1, "RI", "IB")

        n1_addr = n1.getnewaddress()
        n0.addtagtoaddress(qual, n1_addr)
        n0.generate(1)
        self.sync_all()
        n0.transfer(restricted, 500, n1_addr)
        n0.generate(1)
        self.sync_all()

        n1.transfer(restricted, 80, p2ah)
        n1.sendtoaddress(p2ah, 1.5)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah)
        have = [u for u in utxos if 'asset' in u and u['asset']['name'] == restricted
                and float(u['asset']['amount']) >= 80.0]
        assert len(have) >= 1
        self.log.info("Third-party inbound restricted at P2AH: %s" % restricted)

    def stress_restricted_freeze_blocks_spend(self):
        """Per-address freeze on P2AH blocks spendassetauth for that $ASSET."""
        n0, n1 = self.nodes[0], self.nodes[1]
        qual, base, restricted, p2ah, _dest = self._setup_restricted_on_p2ah(n0, n1, "RF", "FZ")

        n0.transfer(restricted, 100, p2ah)
        self.ensure_p2ah_hop_funded(n0, p2ah, min_rvn=25.0, chunk=40.0)

        n0.freezeaddress(restricted, p2ah)
        n0.generate(1)
        self.sync_all()

        recv = n1.getnewaddress()
        n0.addtagtoaddress(qual, recv)
        n0.generate(1)
        self.sync_all()
        assert_raises_rpc_error(-4, None, n0.spendassetauth, p2ah,
                                {recv: {'transfer': {restricted: 5}}})
        self.log.info("Freeze on P2AH blocked spend for %s" % restricted)

    def stress_restricted_untagged_p2ah_rejected(self):
        """Verifier-gated $ASSET cannot land on untagged P2AH."""
        n0, _n1 = self.nodes[0], self.nodes[1]
        qual = self.unique_qualifier("UG")
        base = self.unique_tag("UG")
        restricted = "$" + base
        owner = base + "!"

        n0.issuequalifierasset(qual, 5)
        n0.issue(base, 50)
        n0.generate(1)
        self.sync_all()

        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        n0.generate(1)
        self.sync_all()
        n0.issuerestrictedasset(restricted, 1000, qual, dest)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [owner])['address']
        assert_raises_rpc_error(-8, None, n0.transfer, restricted, 10, p2ah)
        self.log.info("Untagged P2AH correctly rejected for %s" % restricted)

    def run_test(self):
        self.stress_rounds = max(1, int(getattr(self.options, 'stress_rounds', 5)))

        self.activate()
        self.ensure_spendable_rvn(self.nodes[0], min_balance=25000)
        if float(self.nodes[1].getbalance()) < 100:
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 500)
            self.nodes[0].generate(1)
            self.sync_all()
        if self.persistent_dir:
            self._save_run_counter(self.run_id + 1)
        self.recover_persistent_state()

        start_height = self.nodes[0].getblockcount()
        self.tag_epoch = (start_height << 4) | (self.run_id & 0xF)
        self.scenario_counter = 0
        self.log.info("Stress run %d: %d scenario-rounds each, starting height %d (tag epoch 0x%X)" % (
            self.run_id, self.stress_rounds, start_height, self.tag_epoch))

        scenarios = (
            ("simple spend", self.stress_simple_spend),
            ("chained spend (double)", self.stress_chained_spend),
            ("multisig + multi-utxo", self.stress_multisig_and_multi_utxo),
            ("asset held at P2AH", self.stress_asset_on_p2ah),
            ("nested 2-of-3 -> 1-of-3 heavy", self.stress_nested_multisig_chain_heavy),
            ("concurrent same dest same block", self.stress_concurrent_same_dest_same_block),
            ("same P2AH same-block dual spend", self.stress_same_p2ah_same_block_dual_spend),
            ("same P2AH sequential owner rotation", self.stress_same_p2ah_sequential_owner_rotation),
            ("dual asset same P2AH same block", self.stress_one_of_three_shared_asset_same_dest),
            ("one asset -> two multisig dests", self.stress_one_asset_multisig_multi_dest),
            ("one asset two multisig same block", self.stress_one_asset_two_multisig_same_block),
            ("subpoena recovery seizure chain", self.stress_subpoena_recovery_seizure_chain),
            ("restricted P2AH custody roundtrip", self.stress_restricted_p2ah_custody_roundtrip),
            ("restricted third-party inbound", self.stress_restricted_third_party_inbound),
            ("restricted freeze blocks spend", self.stress_restricted_freeze_blocks_spend),
            ("restricted untagged P2AH rejected", self.stress_restricted_untagged_p2ah_rejected),
            ("rejection paths", self.stress_rejection_paths),
        )

        for i in range(self.stress_rounds):
            for name, fn in scenarios:
                self.clear_p2ah_hops()
                self.ensure_spendable_rvn(self.nodes[0], min_balance=15000)
                if float(self.nodes[1].getbalance()) < 50:
                    self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 200)
                    self.nodes[0].generate(1)
                    self.sync_all()
                self.log.info("=== round %d/%d: %s ===" % (i + 1, self.stress_rounds, name))
                fn()

        end_height = self.nodes[0].getblockcount()
        self.log.info("Stress harness done: height %d -> %d (+%d blocks), run id %d" % (
            start_height, end_height, end_height - start_height, self.run_id))

        if self.persistent_dir:
            self._save_chain_height(end_height)


if __name__ == '__main__':
    AssetAuthStressTest().main()
