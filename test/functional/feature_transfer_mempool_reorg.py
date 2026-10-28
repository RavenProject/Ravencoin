#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A bit-8 rollback must evict transfers invalid under the old parser."""

from test_framework.test_framework import RavenTestFramework
from test_framework.mininode import CTransaction, from_hex, to_hex
from test_framework.util import assert_equal, disconnect_nodes


class TransferMempoolReorgTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-assetindex', '-persistmempool=0'],
                           ['-assetindex', '-persistmempool=0']]

    def fixed_locktime_raw(self, node, txid):
        tx = from_hex(CTransaction(), node.gettransaction(txid)['hex'])
        tx.nLockTime = 0
        for txin in tx.vin:
            txin.scriptSig = b''
        signed = node.signrawtransaction(to_hex(tx))
        assert_equal(signed['complete'], True)
        return signed['hex']

    def run_test(self):
        producer, node = self.nodes
        producer.generate(861)
        asset_name = 'ROLLBACKASSET1234567890123456'
        short_asset = 'SHORTROLLBACKASSET'
        producer.issue(asset_name, 2)
        producer.issue(short_asset, 2)
        producer.generate(1)
        self.sync_all()
        assert_equal(node.getblockcount(), 862)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')

        active_block = producer.generate(1)[0]
        self.sync_all()
        disconnect_nodes(producer, 1)
        disconnect_nodes(node, 0)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'active')
        transfer_txid = producer.transfer(asset_name, 1, producer.getnewaddress(), 'ab' * 32, 123456789)[0]
        transfer_raw = self.fixed_locktime_raw(producer, transfer_txid)
        transfer_txid = node.sendrawtransaction(transfer_raw)
        assert_equal(node.decoderawtransaction(transfer_raw)['locktime'], 0)
        assert transfer_txid in node.getrawmempool()

        node.invalidateblock(active_block)
        assert_equal(node.getblockcount(), 862)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['transfer_script']['status'], 'locked_in')
        survived_rollback = transfer_txid in node.getrawmempool()
        self.log.info('Transfer in live mempool after rollback: %s', survived_rollback)

        self.restart_node(1, ['-assetindex', '-persistmempool=0'])
        node = self.nodes[1]
        fresh_result = node.testmempoolaccept([transfer_raw])[0]
        self.log.info('Fresh admission after rollback: %s', fresh_result)
        assert_equal(fresh_result['allowed'], False)
        assert_equal(fresh_result['reject-reason'], '16: bad-txns-transfer-asset-bad-deserialize')
        assert not survived_rollback, 'now-invalid transfer survived bit-8 rollback'

        short_txid = producer.transfer(short_asset, 1, producer.getnewaddress())[0]
        short_raw = self.fixed_locktime_raw(producer, short_txid)
        short_txid = node.sendrawtransaction(short_raw)
        assert short_txid in node.getrawmempool()


if __name__ == '__main__':
    TransferMempoolReorgTest().main()
