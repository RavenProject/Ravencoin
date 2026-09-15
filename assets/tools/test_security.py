import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from safe_url import fetch_public_https
from rpc_auth import rpc_connection_url


class SecurityToolTests(unittest.TestCase):
    def test_rejects_non_https_url(self):
        with self.assertRaises(ValueError):
            fetch_public_https("http://example.com/document")

    def test_rejects_private_target(self):
        with self.assertRaises(ValueError):
            fetch_public_https("https://127.0.0.1/document")

    def test_asset_tools_have_no_shell_sink(self):
        for name in ("asset_audit.py", "issuebulk.py"):
            tree = ast.parse((ROOT / name).read_text(encoding="utf-8"), filename=name)
            sinks = [
                node
                for node in ast.walk(tree)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and node.func.attr == "system"
            ]
            self.assertEqual([], sinks, name)

    def test_rpc_credentials_are_uri_encoded(self):
        url = rpc_connection_url("user@example", "p@ss:word/with?reserved", 8766)
        self.assertEqual(
            "http://user%40example:p%40ss%3Aword%2Fwith%3Freserved@127.0.0.1:8766",
            url,
        )


if __name__ == "__main__":
    unittest.main()
