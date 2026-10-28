#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check that cache generation connects every node before block sync."""

import logging
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from . import test_framework as framework_module
from .test_framework import RavenTestFramework
from .test_node import TestNode
from .util import MAX_NODES, PortSeed


class CacheFramework(RavenTestFramework):
    def set_test_params(self):
        self.num_nodes = 0


class FirstSyncComplete(Exception):
    pass


class CachePeerTest(unittest.TestCase):
    def test_node_without_startup_peer_reaches_first_cache_tip(self):
        with tempfile.TemporaryDirectory(prefix="cache_peer_test_") as tmpdir:
            cache_dir = os.path.join(tmpdir, "cache")
            os.mkdir(cache_dir)
            framework = CacheFramework()
            framework.options = SimpleNamespace(cachedir=cache_dir, tmpdir=tmpdir, coveragedir=None)
            framework.log = logging.getLogger("CachePeerTest")
            previous_port_seed = PortSeed.n
            PortSeed.n = os.getpid()

            start_node = TestNode.start
            sync_blocks = framework_module.sync_blocks

            def start_without_node7_peer(node, extra_args=None, stderr=None):
                if node.index == MAX_NODES - 1:
                    node.args = [arg for arg in node.args if not arg.startswith("-connect=")]
                return start_node(node, extra_args, stderr)

            def stop_after_first_sync(nodes):
                sync_blocks(nodes, timeout=15)
                raise FirstSyncComplete()

            try:
                ravend = Path(__file__).resolve().parents[3] / "src" / "ravend"
                with patch.dict(os.environ, {"RAVEND": str(ravend)}), \
                        patch.object(TestNode, "start", start_without_node7_peer), \
                        patch.object(framework_module, "sync_blocks", stop_after_first_sync):
                    with self.assertRaises(FirstSyncComplete):
                        framework._initialize_chain()
                self.assertEqual([node.getblockcount() for node in framework.nodes], [25] * MAX_NODES)
            finally:
                if framework.nodes:
                    framework.stop_nodes()
                PortSeed.n = previous_port_seed


if __name__ == "__main__":
    unittest.main()
