#!/usr/bin/env python3
# Regression tests for the P2AH review fixes:
#   T1 cycle-safe spendassetauth resolution (no node crash)
#   T2 third-party inbound transfers reach the watching wallet and are spendable
#   T3 typed null-data tags/freeze on P2AH addresses (restricted integration)
#   T4 address-index type 3 (direct + asset-carrying P2AH), incl. reorg symmetry
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class AssetAuthFixesTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-assetindex', '-addressindex', '-fallbackfee=0.0001'],
                           ['-assetindex', '-addressindex', '-fallbackfee=0.0001'],
                           ['-assetindex', '-addressindex', '-fallbackfee=0.0001']]

    def activate(self):
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        info = n0.getblockchaininfo()
        assert_equal("active", info['bip9_softforks']['assets']['status'])
        assert_equal("active", info['bip9_softforks']['assetauth']['status'])
        for i in (1, 2):
            n0.sendtoaddress(self.nodes[i].getnewaddress(), 6000)
        n0.generate(10)
        self.sync_all()

    def t_cycle_safety(self):
        n0 = self.nodes[0]
        n0.issue("CYCA", 100)
        n0.issue("CYCX", 100)
        n0.issue("CYCY", 100)
        n0.generate(1)
        self.sync_all()

        # self-cycle: P2AH address whose own preimage names CYCA!, holding CYCA!
        x = n0.addassetauthaddress(1, ["CYCA!"])['address']
        n0.transfer("CYCA!", 1, x)
        n0.generate(1)
        self.sync_all()
        try:
            n0.spendassetauth(x, {n0.getnewaddress(): 1})
            # If it unexpectedly succeeds that is fine too - it must NOT crash
        except Exception:
            pass
        n0.ping()  # raises if the node died

        # cross-cycle: X(pre CYCX!) holds CYCY!, Y(pre CYCY!) holds CYCX!
        xx = n0.addassetauthaddress(1, ["CYCX!"])['address']
        yy = n0.addassetauthaddress(1, ["CYCY!"])['address']
        n0.transfer("CYCY!", 1, xx)
        n0.transfer("CYCX!", 1, yy)
        n0.generate(1)
        self.sync_all()
        try:
            n0.spendassetauth(xx, {n0.getnewaddress(): 1})
        except Exception:
            pass
        n0.ping()
        print("T1 cycle safety: OK")

    def t_inbound_tracking_and_spend(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        # n0 issues and keeps the owner token INBOUND!, so n0 can authorize spends
        n0.issue("INBOUND", 100)
        n0.generate(1)
        self.sync_all()
        # move some fungible supply to n1 so n1 can act as an unrelated third party
        n0.transfer("INBOUND", 60, n1.getnewaddress())
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, ["INBOUND!"])['address']
        _t = n1.transfer("INBOUND", 40, p2ah)
        txid_asset = _t[0] if isinstance(_t, list) else _t
        txid_rvn = n1.sendtoaddress(p2ah, 3)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah)
        have_asset = any('asset' in u and u['asset']['name'] == "INBOUND" and float(u['asset']['amount']) == 40.0
                         for u in utxos)
        have_rvn = any(abs(float(u['amount']) - 3.0) < 1e-6 for u in utxos)
        assert have_asset, "inbound asset UTXO not visible to watching wallet: %s" % utxos
        assert have_rvn, "inbound RVN UTXO not visible to watching wallet: %s" % utxos

        # The inbound funds must be spendable through spendassetauth
        result = n0.spendassetauth(p2ah, {n1.getnewaddress(): {'transfer': {'INBOUND': 5}}})
        assert 'txid' in result
        n0.generate(1)
        self.sync_all()
        tx = n0.gettransaction(result['txid'])
        assert tx['confirmations'] >= 1
        print("T2 inbound tracking + spend: OK")

    def t_tag_p2ah_typed(self):
        n0 = self.nodes[0]
        n0.issuequalifierasset("#QTFX", 5)
        n0.issue("QTFW", 100)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, ["QTFW!"])['address']
        n0.transfer("QTFW!", 1, p2ah)
        n0.generate(1)
        self.sync_all()

        # P2AH addresses use typed null-data; tagging must succeed after assetauth is active
        n0.addtagtoaddress("#QTFX", p2ah)
        n0.generate(1)
        self.sync_all()

        # sanity: tagging a normal address still works
        n0.addtagtoaddress("#QTFX", n0.getnewaddress())
        print("T3 P2AH typed tag: OK")

    def t_address_index_type3(self):
        n0, n2 = self.nodes[0], self.nodes[2]

        # Direct P2AH RVN output
        p2ah = n0.addassetauthaddress(1, ["AIXW!"])['address']
        fund_txid = n0.sendtoaddress(p2ah, 1.23)
        n0.generate(1)
        self.sync_all()
        fund_block = n0.gettransaction(fund_txid)['blockhash']

        utxos = n2.getaddressutxos({'addresses': [p2ah]})
        rvn_utxos = [u for u in utxos if abs(float(u['satoshis']) / 1e8 - 1.23) < 1e-6]
        assert rvn_utxos, "direct P2AH RVN output missing from getaddressutxos: %s" % utxos
        assert_equal(rvn_utxos[0]['address'], p2ah)

        deltas = n2.getaddressdeltas({'addresses': [p2ah]})
        assert len(deltas) >= 1, "direct P2AH RVN deltas missing"

        # Asset-carrying P2AH output keeps the same identity
        n0.issue("AIDXASSET", 100)
        n0.generate(1)
        _t = n0.transfer("AIDXASSET", 7, p2ah)
        n0.generate(1)
        self.sync_all()
        asset_utxos = n2.getaddressutxos({'addresses': [p2ah], 'assetName': 'AIDXASSET'})
        assert asset_utxos and asset_utxos[0]['address'] == p2ah, \
            "P2AH asset output misattributed in address index: %s" % asset_utxos
        assert_equal(float(asset_utxos[0]['satoshis']) / 1e8, 7.0)

        # Reorg symmetry: undo exactly the block containing the funding tx, then restore
        n2.invalidateblock(fund_block)
        utxos_after_undo = n2.getaddressutxos({'addresses': [p2ah]})
        still_there = [u for u in utxos_after_undo if abs(float(u['satoshis']) / 1e8 - 1.23) < 1e-6]
        assert not still_there, "address index entry survived block invalidation"

        n2.reconsiderblock(fund_block)
        self.sync_all()
        utxos_restored = n2.getaddressutxos({'addresses': [p2ah]})
        restored = [u for u in utxos_restored if abs(float(u['satoshis']) / 1e8 - 1.23) < 1e-6]
        assert restored, "address index not restored after reconsiderblock"

        # disconnect/connect symmetry: net delta count must return to pre-reorg value
        deltas_restored = n2.getaddressdeltas({'addresses': [p2ah]})
        assert len(deltas_restored) >= len(deltas), \
            "delta history lost across reorg (%d -> %d)" % (len(deltas), len(deltas_restored))
        print("T4 address index type 3: OK")

    def run_test(self):
        self.activate()
        self.t_cycle_safety()
        self.t_tag_p2ah_typed()
        self.t_inbound_tracking_and_spend()
        self.t_address_index_type3()


if __name__ == '__main__':
    AssetAuthFixesTest().main()
