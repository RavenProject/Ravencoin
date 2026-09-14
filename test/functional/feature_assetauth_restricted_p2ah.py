#!/usr/bin/env python3
# P2AH + restricted asset integration (typed null-data tags/freeze).
# Exercises qualifier tags, restricted transfers, freeze, and spendassetauth custody.
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class AssetAuthRestrictedP2AHTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ['-assetindex', '-addressindex', '-fallbackfee=0.0001'],
            ['-assetindex', '-addressindex', '-fallbackfee=0.0001'],
        ]

    def activate(self):
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        info = n0.getblockchaininfo()
        assert_equal("active", info['bip9_softforks']['assets']['status'])
        assert_equal("active", info['bip9_softforks']['messaging_restricted']['status'])
        assert_equal("active", info['bip9_softforks']['assetauth']['status'])
        n0.sendtoaddress(self.nodes[1].getnewaddress(), 6000)
        n0.generate(10)
        self.sync_all()

    def t_p2ah_qualifier_tag_and_receive(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        qual = "#P2AHCUST"
        base = "P2AHCUST"
        restricted = "$" + base
        verifier = qual

        n0.issuequalifierasset(qual, 10)
        n0.issue(base, 100)
        n0.generate(1)
        self.sync_all()

        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        n0.generate(1)
        self.sync_all()
        n0.issuerestrictedasset(restricted, 5000, verifier, dest)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [base + "!"])['address']
        n0.transfer(base + "!", 1, p2ah)
        n0.generate(1)
        self.sync_all()

        # Typed null-data: tag the P2AH custody address
        n0.addtagtoaddress(qual, p2ah)
        n0.generate(1)
        self.sync_all()

        # Transfer restricted asset to tagged P2AH (verifier requires #P2AHCUST)
        n0.transfer(restricted, 250, p2ah)
        n0.sendtoaddress(p2ah, 2.5)
        n0.generate(1)
        self.sync_all()

        utxos = n0.listassetauthutxos(p2ah)
        have = [u for u in utxos if 'asset' in u and u['asset']['name'] == restricted
                and float(u['asset']['amount']) == 250.0]
        assert have, "restricted asset not visible at P2AH: %s" % utxos
        print("R1 P2AH tag + receive $ASSET: OK")

    def t_untagged_p2ah_rejected(self):
        n0 = self.nodes[0]
        qual = "#P2AHGATE"
        base = "P2AHGATE"
        restricted = "$" + base

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

        p2ah = n0.addassetauthaddress(1, [base + "!"])['address']
        n0.transfer(base + "!", 1, p2ah)
        n0.generate(1)
        self.sync_all()

        assert_raises_rpc_error(-8, None, n0.transfer, restricted, 10, p2ah)
        print("R2 untagged P2AH rejected by verifier: OK")

    def t_spendassetauth_restricted_out(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        qual = "#P2AHSPND"
        base = "P2AHSPND"
        restricted = "$" + base

        n0.issuequalifierasset(qual, 5)
        n0.issue(base, 100)
        n0.generate(1)
        self.sync_all()

        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        n0.generate(1)
        self.sync_all()
        n0.issuerestrictedasset(restricted, 2000, qual, dest)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [base + "!"])['address']
        # Keep owner token in wallet so spendassetauth can authorize from key-held root
        n0.addtagtoaddress(qual, p2ah)
        n0.generate(1)
        self.sync_all()
        n0.transfer(restricted, 400, p2ah)
        n0.sendtoaddress(p2ah, 3)
        n0.generate(1)
        self.sync_all()

        recv = n1.getnewaddress()
        n0.addtagtoaddress(qual, recv)
        n0.generate(1)
        self.sync_all()
        result = n0.spendassetauth(p2ah, {recv: {'transfer': {restricted: 75}}})
        assert 'txid' in result
        n0.generate(1)
        self.sync_all()
        bal = n1.listmyassets(restricted, True)
        assert restricted in bal and float(bal[restricted]['balance']) >= 75.0
        print("R3 spendassetauth restricted out: OK")

    def t_freeze_p2ah_blocks_spend(self):
        n0 = self.nodes[0]
        qual = "#P2AHFRZ"
        base = "P2AHFRZ"
        restricted = "$" + base

        n0.issuequalifierasset(qual, 5)
        n0.issue(base, 100)
        n0.generate(1)
        self.sync_all()

        dest = n0.getnewaddress()
        n0.addtagtoaddress(qual, dest)
        n0.generate(1)
        self.sync_all()
        n0.issuerestrictedasset(restricted, 1000, qual, dest)
        n0.generate(1)
        self.sync_all()

        p2ah = n0.addassetauthaddress(1, [base + "!"])['address']
        # Keep owner token in wallet so spendassetauth can authorize from key-held root
        n0.addtagtoaddress(qual, p2ah)
        n0.generate(1)
        self.sync_all()
        n0.transfer(restricted, 100, p2ah)
        n0.sendtoaddress(p2ah, 2)
        n0.generate(1)
        self.sync_all()

        n0.freezeaddress(restricted, p2ah)
        n0.generate(1)
        self.sync_all()

        recv = n0.getnewaddress()
        assert_raises_rpc_error(-4, None, n0.spendassetauth, p2ah,
                                {recv: {'transfer': {restricted: 5}}})
        print("R4 freeze P2AH blocks spendassetauth: OK")

    def t_third_party_inbound_restricted(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        qual = "#P2AHIN"
        base = "P2AHIN"
        restricted = "$" + base

        n0.issuequalifierasset(qual, 5)
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

        p2ah = n0.addassetauthaddress(1, [base + "!"])['address']
        # Keep owner token in wallet so spendassetauth can authorize from key-held root
        n0.addtagtoaddress(qual, p2ah)
        n0.generate(1)
        self.sync_all()
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
                and float(u['asset']['amount']) == 80.0]
        assert have, "third-party inbound restricted missing: %s" % utxos
        print("R5 third-party inbound restricted: OK")

    def run_test(self):
        self.activate()
        self.t_p2ah_qualifier_tag_and_receive()
        self.t_untagged_p2ah_rejected()
        self.t_spendassetauth_restricted_out()
        self.t_freeze_p2ah_blocks_spend()
        self.t_third_party_inbound_restricted()


if __name__ == '__main__':
    AssetAuthRestrictedP2AHTest().main()
