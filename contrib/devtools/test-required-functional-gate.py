#!/usr/bin/env python3
"""Negative controls for mandatory functional-test selection."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test" / "functional"))
from test_framework.util import (MAX_NODES, PORT_MIN, PORT_RANGE, PortSeed,
                                 p2p_port, rpc_port)

RUNNER = ROOT / "test" / "functional" / "test_runner.py"
SPEC = importlib.util.spec_from_file_location("raven_test_runner", RUNNER)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RequiredFunctionalGateTests(unittest.TestCase):
    def test_parallel_test_port_blocks_are_disjoint(self):
        previous_seed = PortSeed.n
        try:
            used = set()
            for seed in range(30):
                PortSeed.n = seed
                for node in range(MAX_NODES):
                    for port in (p2p_port(node), rpc_port(node)):
                        self.assertGreaterEqual(port, PORT_MIN)
                        self.assertLess(port, PORT_MIN + 2 * PORT_RANGE)
                        self.assertNotIn(port, used)
                        used.add(port)
            self.assertEqual(len(used), 30 * MAX_NODES * 2)
        finally:
            PortSeed.n = previous_seed

    def test_skipped_required_test_fails(self):
        skipped = MODULE.TestResult("required.py", "Skipped", 0, required=True)
        self.assertFalse(skipped.was_successful)

    def test_optional_skip_keeps_existing_behavior(self):
        skipped = MODULE.TestResult("optional.py", "Skipped", 0)
        self.assertTrue(skipped.was_successful)

    def test_exit_77_from_real_child_fails(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            script = Path(tmpdir) / "skip.py"
            script.write_text("#!/usr/bin/env python3\nimport sys\nsys.exit(77)\n")
            script.chmod(0o700)
            handler = MODULE.TestHandler(
                num_tests_parallel=1,
                tests_dir=tmpdir + os.sep,
                tmpdir=tmpdir,
                use_term_control=False,
                test_list=["skip.py"],
                flags=[],
                required_tests=True,
            )
            result, _, _, _ = handler.get_next()
            self.assertEqual(result.status, "Skipped")
            self.assertFalse(result.was_successful)

    def test_missing_named_test_fails_before_execution(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            result = subprocess.run(
                [sys.executable, str(RUNNER), "--require-tests",
                 "--tmpdirprefix=" + tmpdir,
                 "__missing_required_security_test__.py"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Required test", result.stdout)

    def test_selection_or_loop_bypass_fails(self):
        for option in ("--list", "--loop=2", "--filter=nonexistent"):
            with self.subTest(option=option):
                result = subprocess.run(
                    [sys.executable, str(RUNNER), "--require-tests", option,
                     "wallet_encryption_rewrite.py"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                )
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
