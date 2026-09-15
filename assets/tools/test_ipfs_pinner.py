import importlib.util
import sys
import types
import unittest
from pathlib import Path


class FakeIPFSClient:
    def __init__(self, calls):
        self.calls = calls
        self.pin = types.SimpleNamespace(add=self.pin_add, ls=self.pin_ls)
        self.repo = types.SimpleNamespace(stat=self.repo_stat)

    def __enter__(self):
        self.calls.append(("enter",))
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.calls.append(("exit",))

    def stat(self, value):
        self.calls.append(("stat", value))
        return {"CumulativeSize": 42}

    def add(self, value):
        self.calls.append(("add", value))
        return {"Hash": "QmAdded"}

    def get(self, value):
        self.calls.append(("get", value))

    def pin_add(self, value):
        self.calls.append(("pin.add", value))
        return {"Pins": [value]}

    def pin_ls(self):
        self.calls.append(("pin.ls",))
        return {"Keys": {"QmPinned": {"Type": "recursive"}}}

    def repo_stat(self):
        self.calls.append(("repo.stat",))
        return {"RepoSize": 99}


class IpfsPinnerClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.calls = []
        cls.client = FakeIPFSClient(cls.calls)

        zmq = types.ModuleType("zmq")
        bitcoinrpc = types.ModuleType("bitcoinrpc")
        authproxy = types.ModuleType("bitcoinrpc.authproxy")
        authproxy.AuthServiceProxy = lambda connection: object()
        authproxy.JSONRPCException = RuntimeError
        bitcoinrpc.authproxy = authproxy

        ipfshttpclient = types.ModuleType("ipfshttpclient")
        ipfshttpclient.connect = cls.connect

        cls.modules = {
            "zmq": zmq,
            "bitcoinrpc": bitcoinrpc,
            "bitcoinrpc.authproxy": authproxy,
            "ipfshttpclient": ipfshttpclient,
        }
        cls.previous_modules = {name: sys.modules.get(name) for name in cls.modules}
        sys.modules.update(cls.modules)

        path = Path(__file__).with_name("ipfs_pinner.py")
        spec = importlib.util.spec_from_file_location("ipfs_pinner_under_test", path)
        cls.pinner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.pinner)
        cls.pinner.args = types.SimpleNamespace(debug=False)

    @classmethod
    def tearDownClass(cls):
        for name, previous in cls.previous_modules.items():
            if previous is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = previous

    @classmethod
    def connect(cls, address):
        cls.calls.append(("connect", address))
        return cls.client

    def setUp(self):
        self.calls.clear()

    def test_ipfshttpclient_operations(self):
        self.assertEqual(self.pinner.check_ipfs_file_size("QmSize"), 42)
        self.assertEqual(self.pinner.ipfs_add("metadata.json"), "QmAdded")
        self.assertEqual(self.pinner.ipfs_get("QmGet"), ())
        self.assertEqual(self.pinner.ipfs_pin_add("QmPin"), {"Pins": ["QmPin"]})
        self.assertEqual(self.pinner.ipfs_repo_stat(), {"RepoSize": 99})
        self.assertEqual(self.pinner.ipfs_pin_ls(), {"Keys": {"QmPinned": {"Type": "recursive"}}})

        addresses = [call[1] for call in self.calls if call[0] == "connect"]
        self.assertEqual(addresses, ["/ip4/127.0.0.1/tcp/5001/http"] * 6)
        self.assertIn(("stat", "QmSize"), self.calls)
        self.assertIn(("add", "metadata.json"), self.calls)
        self.assertIn(("get", "QmGet"), self.calls)
        self.assertIn(("pin.add", "QmPin"), self.calls)
        self.assertIn(("repo.stat",), self.calls)
        self.assertIn(("pin.ls",), self.calls)


if __name__ == "__main__":
    unittest.main()
