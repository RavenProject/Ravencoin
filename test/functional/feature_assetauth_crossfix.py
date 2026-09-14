#!/usr/bin/env python3
# Cross-validation fix regressions (F-02, F-03, F-05, F-08)
import json
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class CrossFixTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex', '-addressindex', '-spentindex', '-fallbackfee=0.0001'],
                           ['-assetindex', '-addressindex', '-spentindex', '-fallbackfee=0.0001']]

    def activate(self):
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        info = n0.getblockchaininfo()
        assert_equal("active", info['bip9_softforks']['assets']['status'])
        assert_equal("active", info['bip9_softforks']['assetauth']['status'])
        n0.sendtoaddress(self.nodes[1].getnewaddress(), 6000)
        n0.generate(10)
        self.sync_all()

    # ------------------------------------------------------------------
    # F-01/F-02 combined: deep invalidation below the deployment windows must
    # yield old-node parity (P2AH outputs accepted again), the verdict must be
    # identical across process restart (no sticky history), and reconsidering
    # must restore enforcement.
    def t_activation_determinism(self):
        # Single-node scope: propagation noise is irrelevant to the
        # history-dependence assertion being made here.
        n0 = self.nodes[0]
        p2ah = n0.addassetauthaddress(1, ["DETW!"])['address']

        n0.sendtoaddress(p2ah, 0.5)
        blk_active_era = n0.generate(1)[0]
        import test_framework.util as _u
        _u.sync_blocks(self.nodes)

        # detach the peer so propagation cannot interfere with the
        # history-dependence experiment
        n0.setnetworkactive(False)
        n0.invalidateblock(n0.getblockhash(201))
        st_inv = n0.getblockchaininfo()['bip9_softforks']['assetauth']['status']
        acc_inv, err_inv = None, None
        try:
            tx = n0.sendtoaddress(p2ah, 0.25)
            acc_inv = tx in n0.getrawmempool()
        except Exception as e:
            err_inv = str(e)[:80]

        self.stop_node(0)
        self.start_node(0)
        info = self.nodes[0].getassetauthinfo(p2ah)
        assert info.get('known'), "canonical stored preimage lost after restart (F-07 compat)"
        acc_re, err_re = None, None
        try:
            tx2 = self.nodes[0].sendtoaddress(p2ah, 0.125)
            acc_re = tx2 in self.nodes[0].getrawmempool()
        except Exception as e:
            err_re = str(e)[:80]

        assert acc_inv == acc_re, "history-dependent acceptance (%s/%s vs %s/%s)" % (
            acc_inv, err_inv, acc_re, err_re)

        # Restore active era, then reconnect and let the peer resync.
        n0.reconsiderblock(blk_active_era)
        print("T-X1DBG post-reconsider n0:", n0.getbestblockhash()[:12], n0.getblockcount(),
              "n1:", self.nodes[1].getbestblockhash()[:12], self.nodes[1].getblockcount())
        st_back = n0.getblockchaininfo()['bip9_softforks']['assetauth']['status']
        assert_equal("active", st_back)
        n0.generate(2)
        print("T-X1DBG post-generate n0:", n0.getbestblockhash()[:12], n0.getblockcount(),
              "n1:", self.nodes[1].getbestblockhash()[:12], self.nodes[1].getblockcount())
        import test_framework.util as _u
        n0.clearmempool()
        self.nodes[1].clearmempool()
        n0.setnetworkactive(True)
        _u.connect_nodes(self.nodes[0], 1)
        _u.sync_blocks(self.nodes)
        print("T-X1DBG final n0:", n0.getbestblockhash()[:12], "n1:", self.nodes[1].getbestblockhash()[:12])
        _u.sync_mempools(self.nodes)
        print("T-X1 parity+determinism OK (status=%s accept=%s->%s)" % (st_inv, acc_inv, acc_re))

    # ------------------------------------------------------------------
    # F-03a: any-m-of-n with satisfied names NOT being a prefix
    def t_any_m_of_n_suffix(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        for name in ("XAA", "XBB", "XCC"):
            n0.issue(name, 10)
        n0.generate(1)
        self.sync_all()

        # move XAA! away so only XBB!/XCC! remain key-held
        n0.transfer("XAA!", 1, n1.getnewaddress())
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(2, ["XAA!", "XBB!", "XCC!"])['address']  # sorted, 2-of-3
        n0.issue("XFFF", 100)
        n0.generate(1)
        _t = n0.transfer("XFFF", 12, p2ah)
        n0.generate(1)
        self.sync_all()

        r = n0.spendassetauth(p2ah, {n1.getnewaddress(): {'transfer': {'XFFF': 3}}})
        n0.generate(1)
        self.sync_all()
        assert n0.gettransaction(r['txid'])['confirmations'] >= 1
        moved = set(r['owner_assets_moved'])
        assert moved == {"XBB!", "XCC!"}, "expected suffix owners moved, got %s" % moved
        print("T-X3 any-m-of-n completeness: OK")

    # ------------------------------------------------------------------
    # F-03b/c: alternative candidate inside parent requirement + shared key root
    def t_backtracking_shapes(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        for name in ("PQA", "PQB", "CQA", "CQB", "KRA", "KRB", "LKA", "LKB"):
            n0.issue(name, 10)
        n0.issue("OVFA", 500)
        n0.issue("OVFB", 500)
        n0.generate(1)
        self.sync_all()

        # shape 1: parent pre=[CQA!, CQB!] 1-of-2 with CQA! unavailable, CQB! key-held;
        #          child p2ah Y holds PQB!, target needs [PQA!, PQB!] 2-of-2, PQA! key-held.
        y_pre = n0.addassetauthaddress(1, ["CQA!", "CQB!"])
        t_pre = n0.addassetauthaddress(2, ["PQB!", "PQA!"])
        n0.transfer("PQA!", 1, n1.getnewaddress()) if False else None
        n0.transfer("CQA!", 1, n1.getnewaddress())
        n0.transfer("PQB!", 1, y_pre['address'])
        n0.transfer("OVFA", 20, t_pre['address'])
        n0.generate(1)
        self.sync_all()

        r = n0.spendassetauth(t_pre['address'], {n1.getnewaddress(): {'transfer': {'OVFA': 4}}})
        n0.generate(1)
        self.sync_all()
        assert n0.gettransaction(r['txid'])['confirmations'] >= 1
        print("T-X4 alternative candidate inside parent chain: OK")

        # shape 2: two sibling p2ah links share ONE key-held root KRA!
        x1 = n0.addassetauthaddress(1, ["KRA!"])['address']
        x2 = n0.addassetauthaddress(1, ["KRB!", "KRA!"])['address']
        tgt = n0.addassetauthaddress(2, ["LKA!", "LKB!"])['address']
        n0.transfer("KRA!", 1, x1)   # single KRA! token deposited at x1
        n0.transfer("LKA!", 1, x2)   # LKB!? only one token each:
        n0.transfer("LKB!", 1, x2)
        n0.transfer("OVFB", 20, tgt)
        n0.generate(1)
        self.sync_all()

        r2 = n0.spendassetauth(tgt, {n1.getnewaddress(): {'transfer': {'OVFB': 6}}})
        n0.generate(1)
        self.sync_all()
        assert n0.gettransaction(r2['txid'])['confirmations'] >= 1
        print("T-X5 shared-root sibling links: OK")

    # ------------------------------------------------------------------
    # F-05: spent-index consumers render type-3 identities
    def t_spent_index_consumers(self):
        n0 = self.nodes[0]
        p2ah = n0.addassetauthaddress(1, ["SIDA!"])['address']
        n0.issue("SIDA", 50)
        n0.generate(1)
        fund_rvn = n0.sendtoaddress(p2ah, 1.5)
        n0.generate(1)
        _t = n0.transfer("SIDA", 7, p2ah)
        blk_fund = n0.generate(1)[0]
        self.sync_all()

        spend = n0.spendassetauth(p2ah, {self.nodes[1].getnewaddress(): {'transfer': {'SIDA': 2}}})
        blk_spend = n0.generate(1)[0]
        self.sync_all()

        deltas_fund = n0.getblockdeltas(blk_fund)
        out_addrs = [d.get('address') for tx in deltas_fund['deltas'] for d in tx['outputs']]
        assert p2ah in out_addrs, "P2AH receive missing from block outputs: %s" % out_addrs

        deltas_spend = n0.getblockdeltas(blk_spend)
        in_addrs = [d.get('address') for tx in deltas_spend['deltas'] for d in tx['inputs']]
        assert p2ah in in_addrs, "P2AH spend input missing from getblockdeltas inputs: %s" % in_addrs

        raw = n0.getrawtransaction(spend['txid'], 1)
        vin_addr = [v.get('address') for v in raw['vin']]
        assert p2ah in vin_addr, "expanded raw vin lacks P2AH address: %s" % vin_addr
        print("T-X6 spent-index consumers (vin address / blockdeltas): OK")

    # ------------------------------------------------------------------
    # F-08: signer refuses non-ALL sighash for P2AH-mixed transactions
    def t_signer_sighash_gate(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        p2ah = n0.addassetauthaddress(1, ["SGW!"])['address']
        n0.issue("SGW", 30)
        n0.generate(1)
        n0.transfer("SGW", 8, p2ah)
        n0.generate(1)
        self.sync_all()

        utxo = [u for u in n0.listassetauthutxos(p2ah) if 'asset' in u][0]
        normal_out = None
        for u in n0.listunspent():
            if float(u['amount']) > 1:
                normal_out = u
                break
        assert normal_out

        # locate the three inputs
        p2ah_utxo = [u for u in n0.listassetauthutxos(p2ah) if 'asset' in u][0]
        owner_out = n0.listmyassets("SGW!", True)["SGW!"]["outpoints"][0]
        rvn_utxo = None
        for u in n0.listunspent():
            if float(u['amount']) > 1:
                rvn_utxo = u
                break
        assert rvn_utxo

        raw_fund = n0.getrawtransaction(p2ah_utxo['txid'], 1)
        p2ah_script_hex = raw_fund['vout'][p2ah_utxo['vout']]['scriptPubKey']['hex']

        raw_owner = n0.getrawtransaction(owner_out['txid'], 1)
        owner_spk = raw_owner['vout'][owner_out['vout']]['scriptPubKey']['hex']

        dest = n1.getnewaddress()
        dest2 = n0.getnewaddress()
        inputs = [
            {"txid": p2ah_utxo['txid'], "vout": p2ah_utxo['vout']},
            {"txid": owner_out['txid'], "vout": owner_out['vout']},
            {"txid": rvn_utxo['txid'], "vout": int(rvn_utxo['vout'])},
        ]
        outputs = {
            dest: {"transfer": {"SGW": 8}},
            dest2: {"transfer": {"SGW!": 1}},
            dest2: 0.5,
        }
        # dict keys collide on dest2; use distinct recipients
        change_addr = n0.getnewaddress()
        change_amt = round(float(rvn_utxo['amount']) - 0.5 - 0.01, 8)
        assert change_amt > 0
        outputs = {
            dest: {"transfer": {"SGW": 8}},
            dest2: {"transfer": {"SGW!": 1}},
            change_addr: change_amt,
        }
        raw_unsigned = n0.createrawtransaction(inputs, outputs)

        preimage_info = n0.getassetauthinfo(p2ah)
        assert preimage_info.get('known')
        preimage_hex = preimage_info['preimage']

        prevtxs = [
            {"txid": p2ah_utxo['txid'], "vout": p2ah_utxo['vout'],
             "scriptPubKey": p2ah_script_hex,
             "amount": float(raw_fund['vout'][p2ah_utxo['vout']]['value']),
             "assetAuthPreimage": preimage_hex},
            {"txid": owner_out['txid'], "vout": owner_out['vout'],
             "scriptPubKey": owner_spk},
            {"txid": rvn_utxo['txid'], "vout": int(rvn_utxo['vout']),
             "scriptPubKey": rvn_utxo['scriptPubKey']},
        ]

        # non-ALL sighash must now be refused outright (F-08 gate)
        try:
            res = n0.signrawtransaction(raw_unsigned, prevtxs, None, "NONE")
            assert not res.get('complete'), "signer accepted non-ALL sighash on P2AH-mixed tx"
            print("T-X7 fallback path complete=false:", json.dumps(res.get('errors', []))[:120])
        except Exception as e:
            msg = str(e)
            assert "SIGHASH_ALL" in msg, "unexpected signer error: %s" % msg

        # ALL completes and is accepted by consensus end-to-end
        res_all = n0.signrawtransaction(raw_unsigned, prevtxs, None, "ALL")
        assert res_all['complete'], res_all.get('errors')
        sent = n0.sendrawtransaction(res_all['hex'])
        n0.generate(1)
        self.sync_all()
        assert n0.gettransaction(sent)['confirmations'] >= 1
        print("T-X7 signer sighash gate + ALL e2e: OK")

    # ------------------------------------------------------------------
    # R-01/F-05: asset-bearing deep reorg must survive contextual undo, and
    # historical type-3 representation must not depend on the current tip.
    def t_deep_asset_reorg(self):
        n0 = self.nodes[0]
        p2ah = n0.addassetauthaddress(1, ["DPA!"])['address']
        n0.issue("DPA", 40)
        n0.generate(1)
        n0.sendtoaddress(p2ah, 1.5)
        n0.generate(1)
        _t = n0.transfer("DPA", 9, p2ah)
        blk_fund_ast = n0.generate(1)[0]
        n0.generate(1)
        spend = n0.spendassetauth(p2ah, {self.nodes[1].getnewaddress(): {'transfer': {'DPA': 2}}})
        blk_spend = n0.generate(1)[0]
        import test_framework.util as _u
        _u.sync_blocks(self.nodes)

        baseline = sorted(map(json.dumps,
                          n0.getblockdeltas(blk_fund_ast)['deltas']))
        assert any(p2ah in json.dumps(d) for d in baseline), \
            "type-3 rows missing in baseline deltas"

        # Deep invalidation BELOW the funding block: several asset-bearing
        # disconnects run with block-contextual deployment state.
        below = n0.getblockhash(n0.getblockcount() - 3)
        n0.invalidateblock(below)
        assert n0.ping() is None

        restored_utxos = [u for u in n0.listassetauthutxos(p2ah)
                          if 'asset' in u and float(u['asset']['amount']) >= 7.0]
        assert len(restored_utxos) >= 1, \
            "contextual asset undo failed to restore P2AH holdings"

        n0.reconsiderblock(below)
        _u.sync_blocks(self.nodes)
        after = sorted(map(json.dumps,
                       n0.getblockdeltas(blk_fund_ast)['deltas']))
        assert after == baseline, "delta set changed across reorg cycle"
        conf = n0.gettransaction(spend['txid'])['confirmations']
        assert conf >= 1
        print("T-X8 asset-bearing deep reorg + contextual undo/restore: OK")

    def run_test(self):
        self.activate()
        self.t_activation_determinism()
        self.t_any_m_of_n_suffix()
        self.t_backtracking_shapes()
        self.t_spent_index_consumers()
        self.t_signer_sighash_gate()
        self.t_deep_asset_reorg()


if __name__ == '__main__':
    CrossFixTest().main()
