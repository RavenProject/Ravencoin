#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Do not wipe an uncertified asset chainstate when block data is missing."""

import http.client
import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, wait_until


class AssetMissingBlockRecoveryTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-assetindex']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(432)
        node.gettxoutsetinfo()

        crash_height = node.getblockcount() + 1
        self.restart_node(0, ['-assetindex', '-dbcache=1000',
                              '-dbcrashaftercoinsflushheight={}'.format(crash_height)])
        node = self.nodes[0]
        asset_name = 'MISSINGBLOCKASSET'
        node.issue(asset_name, 7)
        asset_block = node.generate(1)[0]
        expected_asset = node.getassetdata(asset_name)
        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The post-coins-flush fault hook did not crash')
        node.wait_until_stopped()

        block_path = os.path.join(node.datadir, 'regtest', 'blocks', 'blk00000.dat')
        held_path = block_path + '.held'
        os.rename(block_path, held_path)
        try:
            # Keep the file openable by LoadBlockIndexDB but remove the block
            # payload needed for a full chainstate rebuild.
            shutil.copyfile(held_path, block_path)
            os.truncate(block_path, 128)
            self.assert_start_raises_init_error(
                0, ['-assetindex', '-dbcache=1000'],
                'Asset database state is not certified and required block data is unavailable')
        finally:
            os.replace(held_path, block_path)

        self.start_node(0, ['-assetindex', '-dbcache=1000'])
        node = self.nodes[0]
        # RPC can become available before background block import reaches
        # the recovered tip. A persistently wrong tip must still fail.
        wait_until(lambda: node.getbestblockhash() == asset_block,
                   err_msg='recovery did not reach the expected asset block',
                   timeout=60)
        assert_equal(node.getbestblockhash(), asset_block)
        assert_equal(node.getassetdata(asset_name), expected_asset)


if __name__ == '__main__':
    AssetMissingBlockRecoveryTest().main()
