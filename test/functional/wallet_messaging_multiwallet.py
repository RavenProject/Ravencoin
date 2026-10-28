#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Message subscriptions and inboxes belong to the selected wallet."""

import http.client
import os
import shutil

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, wait_until


IPFS_HASH = 'QmZPGfJojdTzaqCWJu2m3krark38X1rqEHBo4SjeqHKB26'


class MessagingMultiWalletTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-wallet=w1', '-wallet=w2', '-assetindex']]

    def wallets(self):
        node = self.nodes[0]
        return node.get_wallet_rpc('w1'), node.get_wallet_rpc('w2')

    def assert_channels(self, first, second):
        w1, w2 = self.wallets()
        assert_equal(set(w1.viewallmessagechannels()), set(first))
        assert_equal(set(w2.viewallmessagechannels()), set(second))

    def run_test(self):
        node = self.nodes[0]
        w1, w2 = self.wallets()
        w1.generate(432)
        assert_equal(node.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'], 'active')
        w1.sendtoaddress(w2.getnewaddress(), 1200)
        w1.generate(1)

        w1.issue('WALLETMSGONE')
        w2.issue('WALLETMSGTWO')
        w1.generate(1)
        w1.issue('WALLETMSGONE~NEWS')
        w2.issue('WALLETMSGTWO~NEWS')
        w1.generate(1)

        self.assert_channels({'WALLETMSGONE!', 'WALLETMSGONE~NEWS'},
                             {'WALLETMSGTWO!', 'WALLETMSGTWO~NEWS'})

        # A manual subscription starts after the current tip. Restarting from
        # an older scan checkpoint must not import a prior channel message.
        w2.sendmessage('WALLETMSGTWO~NEWS', IPFS_HASH)
        w1.generate(1)
        assert_equal(w1.viewallmessages(), [])
        assert_equal(len(w2.viewallmessages()), 1)
        w2.clearmessages()
        w1.subscribetochannel('WALLETMSGTWO~NEWS')
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal(w1.viewallmessages(), [])
        w2.sendmessage('WALLETMSGTWO~NEWS', IPFS_HASH)
        w1.generate(1)
        assert_equal(len(w1.viewallmessages()), 1)
        assert_equal(len(w2.viewallmessages()), 1)
        w1.clearmessages()
        assert_equal(w1.viewallmessages(), [])
        assert_equal(len(w2.viewallmessages()), 1)
        w1.unsubscribefromchannel('WALLETMSGTWO~NEWS')
        self.assert_channels({'WALLETMSGONE!', 'WALLETMSGONE~NEWS'},
                             {'WALLETMSGTWO!', 'WALLETMSGTWO~NEWS'})

        # A deliberate unsubscribe must survive a historical catch-up scan.
        w2.unsubscribefromchannel('WALLETMSGTWO~NEWS')

        self.restart_node(0, ['-wallet=w2', '-wallet=w1', '-assetindex'])
        self.assert_channels({'WALLETMSGONE!', 'WALLETMSGONE~NEWS'},
                             {'WALLETMSGTWO!'})
        w1, w2 = self.wallets()
        assert_equal(w1.viewallmessages(), [])
        assert_equal(len(w2.viewallmessages()), 1)
        w2.subscribetochannel('WALLETMSGTWO~NEWS')

        # An unloaded wallet receives an asset before its first channel scan.
        late_address = w2.getnewaddress()
        self.restart_node(0, ['-wallet=w1', '-assetindex'])
        w1 = node.get_wallet_rpc('w1')
        w1.transfer('WALLETMSGONE', 1, late_address)
        w1.generate(1)
        w1.sendmessage('WALLETMSGONE!', IPFS_HASH)
        w1.generate(1)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        self.assert_channels({'WALLETMSGONE!', 'WALLETMSGONE~NEWS'},
                             {'WALLETMSGTWO!', 'WALLETMSGTWO~NEWS', 'WALLETMSGONE!'})
        w1, w2 = self.wallets()
        assert_equal({message['Asset Name'] for message in w2.viewallmessages()},
                     {'WALLETMSGTWO~NEWS', 'WALLETMSGONE!'})

        # The historical inbox must be durable before its scan checkpoint.
        node.process.kill()
        node.process.wait(timeout=10)
        self.start_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal({message['Asset Name'] for message in w2.viewallmessages()},
                     {'WALLETMSGTWO~NEWS', 'WALLETMSGONE!'})

        # A reorganization must hide incomplete wallet views. Reconnecting
        # the same block still requires a historical rebuild on restart.
        message_block = node.getbestblockhash()
        node.invalidateblock(message_block)
        assert_raises_rpc_error(-20, 'incomplete', w2.viewallmessages)
        node.reconsiderblock(message_block)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal({message['Asset Name'] for message in w2.viewallmessages()},
                     {'WALLETMSGTWO~NEWS', 'WALLETMSGONE!'})
        assert_equal({message['Status'] for message in w2.viewallmessages()},
                     {'UNREAD'})

        # Commit the old-branch inbox, then crash after the shorter chainstate
        # is written but before the message database can flush its orphans.
        parent_block = node.getblockheader(message_block)['previousblockhash']
        parent_height = node.getblockcount() - 1
        node.gettxoutsetinfo()
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex',
                              '-dbcache=1000',
                              '-dbcrashaftercoinsflushheight={}'.format(parent_height)])
        node.invalidateblock(message_block)
        try:
            node.gettxoutsetinfo()
        except (http.client.HTTPException, OSError):
            pass
        else:
            raise AssertionError('The post-coins-flush fault hook did not crash')
        node.wait_until_stopped()
        self.start_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex', '-dbcache=1000'])
        w1, w2 = self.wallets()
        wait_until(lambda: node.getbestblockhash() == parent_block,
                   err_msg='message recovery did not reach the shorter chain',
                   timeout=60)
        assert_equal([message['Status'] for message in w2.viewallmessages()
                      if message['Asset Name'] == 'WALLETMSGONE!'], ['ORPHAN'])
        node.reconsiderblock(message_block)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal([message['Status'] for message in w2.viewallmessages()
                      if message['Asset Name'] == 'WALLETMSGONE!'], ['UNREAD'])

        # A manual opt-out remains in force for blocks between unsubscribe
        # and resubscribe, even when an older scan saw auto-ownership.
        w2.unsubscribefromchannel('WALLETMSGONE!')
        w1.sendmessage('WALLETMSGONE!', IPFS_HASH)
        w1.generate(1)
        assert_equal(len([message for message in w2.viewallmessages()
                          if message['Asset Name'] == 'WALLETMSGONE!']), 1)
        w2.subscribetochannel('WALLETMSGONE!')
        opt_out_block = node.getbestblockhash()
        node.invalidateblock(opt_out_block)
        node.reconsiderblock(opt_out_block)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal(len([message for message in w2.viewallmessages()
                          if message['Asset Name'] == 'WALLETMSGONE!']), 1)

        # Disconnect and reconnect while w2 is unloaded. Its saved checkpoint
        # must be invalidated so the reconnected message becomes unread again.
        self.restart_node(0, ['-wallet=w1', '-assetindex'])
        node.invalidateblock(message_block)
        node.reconsiderblock(message_block)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal([message['Status'] for message in w2.viewallmessages()
                      if message['Asset Name'] == 'WALLETMSGONE!'], ['UNREAD'])

        # If a reorg removes the manual anchor block, the replacement branch
        # cannot be replayed as though the subscription predated that branch.
        w1.subscribetochannel('WALLETMSGTWO!')
        node.invalidateblock(node.getbestblockhash())
        assert_raises_rpc_error(-20, 'incomplete', w1.viewallmessages)
        w2.sendmessage('WALLETMSGTWO!', IPFS_HASH)
        w2.generate(1)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert 'WALLETMSGTWO!' not in {
            message['Asset Name'] for message in w1.viewallmessages()}
        w2.sendmessage('WALLETMSGTWO!', IPFS_HASH)
        w2.generate(1)
        assert 'WALLETMSGTWO!' in {
            message['Asset Name'] for message in w1.viewallmessages()}

        # A later owned transfer can restore an automatically subscribed
        # channel after a manual unsubscribe. Its state must seed a catch-up
        # scan when the wallet was unloaded for the next message block.
        w2.unsubscribefromchannel('WALLETMSGONE!')
        w2.transfer('WALLETMSGONE', 1, w2.getnewaddress())
        w1.generate(1)
        assert 'WALLETMSGONE!' in w2.viewallmessagechannels()
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        self.restart_node(0, ['-wallet=w1', '-assetindex'])
        w1 = node.get_wallet_rpc('w1')
        w1.sendmessage('WALLETMSGONE!', IPFS_HASH)
        w1.generate(1)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert_equal(len([message for message in w2.viewallmessages()
                          if message['Asset Name'] == 'WALLETMSGONE!']), 2)

        # An unsubscribe anchored to an orphaned block cannot suppress an
        # automatic subscription on its replacement branch after replay.
        w2.unsubscribefromchannel('WALLETMSGONE!')
        node.invalidateblock(node.getbestblockhash())
        w2.transfer('WALLETMSGONE', 1, w2.getnewaddress())
        w1.generate(1)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert 'WALLETMSGONE!' in w2.viewallmessagechannels()
        w1.sendmessage('WALLETMSGONE!', IPFS_HASH)
        w1.generate(1)
        assert_equal([message['Status'] for message in w2.viewallmessages()
                      if message['Asset Name'] == 'WALLETMSGONE!'][-1:], ['UNREAD'])

        # If an automatic resubscription is orphaned but the preceding
        # manual unsubscribe survives, rebuilding must restore suppression.
        w2.unsubscribefromchannel('WALLETMSGONE!')
        w2.transfer('WALLETMSGONE', 1, w2.getnewaddress())
        resubscription_block = w1.generate(1)[0]
        assert 'WALLETMSGONE!' in w2.viewallmessagechannels()
        node.invalidateblock(resubscription_block)
        self.restart_node(0, ['-wallet=w1', '-wallet=w2', '-assetindex'])
        w1, w2 = self.wallets()
        assert 'WALLETMSGONE!' not in w2.viewallmessagechannels()

        # A copied wallet has identical keys and marker but a distinct file name.
        self.stop_node(0)
        wallet_dir = os.path.join(node.datadir, 'regtest')
        shutil.copyfile(os.path.join(wallet_dir, 'w1'), os.path.join(wallet_dir, 'w3'))
        # Berkeley DB rejects loading two byte-for-byte wallet copies in one
        # process, so load the copy while the original is offline.
        self.start_node(0, ['-wallet=w2', '-wallet=w3', '-wallet=w4', '-assetindex'])
        w3 = node.get_wallet_rpc('w3')
        assert 'WALLETMSGTWO!' not in w3.viewallmessagechannels()
        w4 = node.get_wallet_rpc('w4')
        assert_equal(w4.viewallmessagechannels(), [])
        assert_equal(w4.viewallmessages(), [])

        # Pre-upgrade node-global records have no trustworthy wallet owner.
        # The operator can inspect them separately without importing them.
        legacy = node.exportlegacymessagedata(True)
        assert_equal(legacy['scope'], 'legacy-global-unscoped')
        assert_equal(legacy['messages'], [])
        assert_equal(legacy['channels'], [])
        assert_raises_rpc_error(-8, 'acknowledge', node.exportlegacymessagedata, False)


if __name__ == '__main__':
    MessagingMultiWalletTest().main()
