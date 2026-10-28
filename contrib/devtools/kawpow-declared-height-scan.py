#!/usr/bin/env python3
"""Scan canonical mainnet headers for pre-height-gate KAWPOW height mismatches.

This tool reads a local node through JSON-RPC. It never writes to the node.
Use an unpruned node started with -assumevalid=0 for release evidence.
RPC does not expose the assumevalid setting, so record that setting separately.
"""

import argparse
import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import urllib.error
import urllib.request


KAWPOW_ACTIVATION_TIME = 1588788000
HEIGHT_GATE_CHECKPOINT = 4487775
HEIGHT_GATE_CHECKPOINT_HASH = (
    "000000000002d64509e06e76ddbbe418c725291687ec62b41ecfc40386a091fd"
)
EPOCH_LENGTH = 7500
SCHEMA_VERSION = 1
DERIVED_FIELDS = (
    "next_height", "last_hash", "legacy_header_count", "kawpow_header_count",
    "first_kawpow_height", "max_declared_height", "min_declared_minus_actual",
    "max_declared_minus_actual", "max_declared_epoch_minus_actual_epoch",
    "max_absolute_epoch_difference", "mismatches",
)


def parse_header(raw_hex):
    """Return (previous hash, timestamp, declared height or None)."""
    try:
        raw = bytes.fromhex(raw_hex)
    except ValueError as exc:
        raise ValueError("RPC returned a non-hex block header") from exc
    if len(raw) not in (80, 120):
        raise ValueError(f"Unexpected block header size: {len(raw)} bytes")

    previous_hash = raw[4:36][::-1].hex()
    timestamp = struct.unpack_from("<I", raw, 68)[0]
    is_kawpow = timestamp >= KAWPOW_ACTIVATION_TIME
    if len(raw) != (120 if is_kawpow else 80):
        raise ValueError("Header size does not match its KAWPOW activation time")
    declared_height = struct.unpack_from("<I", raw, 76)[0] if is_kawpow else None
    return previous_hash, timestamp, declared_height


class LocalRPC:
    def __init__(self, datadir, port):
        cookie_path = datadir / ".cookie"
        try:
            cookie = cookie_path.read_bytes().strip()
        except OSError as exc:
            raise RuntimeError("Cannot read the local mainnet RPC cookie") from exc
        if b":" not in cookie:
            raise RuntimeError("Invalid local mainnet RPC cookie")
        self.url = f"http://127.0.0.1:{port}/"
        self.authorization = "Basic " + base64.b64encode(cookie).decode("ascii")
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def batch(self, calls, timeout=120):
        requests = [
            {"jsonrpc": "1.0", "id": index, "method": method, "params": params}
            for index, (method, params) in enumerate(calls)
        ]
        body = json.dumps(requests, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            self.url,
            data=body,
            headers={
                "Authorization": self.authorization,
                "Content-Type": "application/json",
            },
        )
        try:
            with self.opener.open(request, timeout=timeout) as response:
                result = json.load(response)
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"Local RPC returned HTTP {exc.code}") from exc
        except (urllib.error.URLError, TimeoutError) as exc:
            raise RuntimeError("Cannot contact the local mainnet RPC server") from exc
        if not isinstance(result, list) or len(result) != len(calls):
            raise RuntimeError("Local RPC returned an incomplete batch")
        by_id = {}
        for item in result:
            if not isinstance(item, dict) or item.get("id") in by_id:
                raise RuntimeError("Local RPC returned duplicate or malformed IDs")
            by_id[item.get("id")] = item
        values = []
        for index in range(len(calls)):
            if index not in by_id:
                raise RuntimeError("Local RPC omitted a batch result")
            item = by_id[index]
            if item.get("error") is not None:
                error = item["error"]
                code = error.get("code") if isinstance(error, dict) else "unknown"
                raise RuntimeError(f"Local RPC call failed with code {code}")
            values.append(item.get("result"))
        return values

    def call(self, method, *params):
        timeout = 43200 if method == "verifychain" else 120
        return self.batch([(method, list(params))], timeout=timeout)[0]


def write_json_atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, path)


def new_state(node, checkpoint_hash):
    return {
        "schema_version": SCHEMA_VERSION,
        "chain": "main",
        "checkpoint_height": HEIGHT_GATE_CHECKPOINT,
        "checkpoint_hash": checkpoint_hash,
        "kawpow_activation_time": KAWPOW_ACTIVATION_TIME,
        "node_at_start": node,
        "next_height": 0,
        "last_hash": None,
        "legacy_header_count": 0,
        "kawpow_header_count": 0,
        "first_kawpow_height": None,
        "max_declared_height": None,
        "min_declared_minus_actual": None,
        "max_declared_minus_actual": None,
        "max_declared_epoch_minus_actual_epoch": None,
        "max_absolute_epoch_difference": 0,
        "mismatches": [],
    }


def valid_hash(value):
    return (isinstance(value, str) and len(value) == 64 and
            all(character in "0123456789abcdef" for character in value))


def valid_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def validate_partial_state(state, checkpoint_hash):
    """Reject a truncated or inconsistent resume cursor before skipping work."""
    if not isinstance(state, dict):
        raise RuntimeError("Partial scan is not a JSON object")
    if (state.get("schema_version") != SCHEMA_VERSION or
            state.get("chain") != "main" or
            state.get("checkpoint_height") != HEIGHT_GATE_CHECKPOINT or
            state.get("checkpoint_hash") != checkpoint_hash or
            state.get("kawpow_activation_time") != KAWPOW_ACTIVATION_TIME):
        raise RuntimeError("Partial scan has an incompatible checkpoint or schema")

    cursor = state.get("next_height")
    legacy = state.get("legacy_header_count")
    kawpow = state.get("kawpow_header_count")
    if (not all(valid_int(value) for value in (cursor, legacy, kawpow)) or
            not 0 <= cursor <= HEIGHT_GATE_CHECKPOINT + 1 or
            legacy < 0 or kawpow < 0 or legacy + kawpow != cursor):
        raise RuntimeError("Partial scan has inconsistent height or header counts")
    last_hash = state.get("last_hash")
    if (cursor == 0 and last_hash is not None) or (cursor > 0 and not valid_hash(last_hash)):
        raise RuntimeError("Partial scan has an invalid last block hash")
    if not isinstance(state.get("node_at_start"), dict):
        raise RuntimeError("Partial scan lacks its node provenance")

    mismatches = state.get("mismatches")
    if not isinstance(mismatches, list) or len(mismatches) > kawpow:
        raise RuntimeError("Partial scan has inconsistent mismatch entries")
    first = state.get("first_kawpow_height")
    max_declared = state.get("max_declared_height")
    min_delta = state.get("min_declared_minus_actual")
    max_delta = state.get("max_declared_minus_actual")
    max_epoch_delta = state.get("max_declared_epoch_minus_actual_epoch")
    max_abs_epoch = state.get("max_absolute_epoch_difference")
    if kawpow == 0:
        if (first is not None or max_declared is not None or min_delta is not None or
                max_delta is not None or max_epoch_delta is not None or
                max_abs_epoch != 0 or mismatches):
            raise RuntimeError("Partial scan has KAWPOW statistics without headers")
    elif (not valid_int(first) or not 0 <= first < cursor or
          not valid_int(max_declared) or not 0 <= max_declared <= 0xFFFFFFFF or
          not all(valid_int(value) for value in (min_delta, max_delta,
                                                 max_epoch_delta, max_abs_epoch)) or
          min_delta > max_delta or max_abs_epoch < abs(max_epoch_delta)):
        raise RuntimeError("Partial scan has inconsistent KAWPOW statistics")

    previous_height = -1
    for entry in mismatches:
        if not isinstance(entry, dict):
            raise RuntimeError("Partial scan has a malformed mismatch entry")
        actual = entry.get("actual_height")
        declared = entry.get("declared_height")
        delta = entry.get("declared_minus_actual")
        epoch_delta = entry.get("declared_epoch_minus_actual_epoch")
        if (not all(valid_int(value) for value in (actual, declared, delta, epoch_delta)) or
                not first <= actual < cursor or actual <= previous_height or
                not 0 <= declared <= 0xFFFFFFFF or delta != declared - actual or
                delta == 0 or
                epoch_delta != declared // EPOCH_LENGTH - actual // EPOCH_LENGTH or
                declared > max_declared or not min_delta <= delta <= max_delta or
                epoch_delta > max_epoch_delta or abs(epoch_delta) > max_abs_epoch or
                not valid_hash(entry.get("block_hash"))):
            raise RuntimeError("Partial scan has a malformed mismatch entry")
        previous_height = actual


def update_state(state, actual_height, block_hash, raw_hex):
    previous_hash, _, declared_height = parse_header(raw_hex)
    expected_previous = state["last_hash"] or "0" * 64
    if previous_hash != expected_previous:
        raise RuntimeError(f"Canonical header link differs at height {actual_height}")

    if declared_height is None:
        state["legacy_header_count"] += 1
    else:
        state["kawpow_header_count"] += 1
        if state["first_kawpow_height"] is None:
            state["first_kawpow_height"] = actual_height
        state["max_declared_height"] = max(
            declared_height, state["max_declared_height"] or 0
        )
        delta = declared_height - actual_height
        epoch_delta = declared_height // EPOCH_LENGTH - actual_height // EPOCH_LENGTH
        for key, value in (
            ("min_declared_minus_actual", delta),
            ("max_declared_minus_actual", delta),
            ("max_declared_epoch_minus_actual_epoch", epoch_delta),
        ):
            old = state[key]
            if old is None or (value < old if key.startswith("min_") else value > old):
                state[key] = value
        state["max_absolute_epoch_difference"] = max(
            state["max_absolute_epoch_difference"], abs(epoch_delta)
        )
        if delta:
            state["mismatches"].append(
                {
                    "actual_height": actual_height,
                    "declared_height": declared_height,
                    "declared_minus_actual": delta,
                    "declared_epoch_minus_actual_epoch": epoch_delta,
                    "block_hash": block_hash,
                }
            )
    state["last_hash"] = block_hash
    state["next_height"] = actual_height + 1


def scan_range(rpc, state, stop_height, batch_size, partial=None, label="Scanned"):
    for first in range(state["next_height"], stop_height, batch_size):
        heights = range(first, min(first + batch_size, stop_height))
        hashes = rpc.batch([("getblockhash", [height]) for height in heights])
        if any(not valid_hash(block_hash) for block_hash in hashes):
            raise RuntimeError("Local RPC returned a malformed block hash")
        raw_headers = rpc.batch(
            [("getblockheader", [block_hash, False]) for block_hash in hashes]
        )
        for height, block_hash, raw_hex in zip(heights, hashes, raw_headers):
            if not isinstance(raw_hex, str):
                raise RuntimeError("Local RPC returned a malformed raw header")
            update_state(state, height, block_hash, raw_hex)
        if partial is not None:
            write_json_atomic(partial, state)
        if state["next_height"] % 100000 < batch_size:
            print(f"{label} through height {state['next_height'] - 1}", file=sys.stderr)


def scan(rpc, output, batch_size, resume, verifychain):
    blockchain = rpc.call("getblockchaininfo")
    if not isinstance(blockchain, dict) or blockchain.get("chain") != "main":
        raise RuntimeError("This scan requires a mainnet node")
    if blockchain.get("pruned") is not False:
        raise RuntimeError("This scan requires an unpruned node")
    if blockchain.get("blocks", -1) < HEIGHT_GATE_CHECKPOINT:
        raise RuntimeError("Mainnet node is below the required checkpoint")
    checkpoint_hash = rpc.call("getblockhash", HEIGHT_GATE_CHECKPOINT)
    if checkpoint_hash != HEIGHT_GATE_CHECKPOINT_HASH:
        raise RuntimeError("Mainnet checkpoint hash does not match chainparams")
    network = rpc.call("getnetworkinfo")
    node = {
        "blocks": blockchain.get("blocks"),
        "headers": blockchain.get("headers"),
        "bestblockhash": blockchain.get("bestblockhash"),
        "verificationprogress": blockchain.get("verificationprogress"),
        "pruned": blockchain.get("pruned"),
        "initialblockdownload": blockchain.get("initialblockdownload"),
        "version": network.get("version") if isinstance(network, dict) else None,
        "subversion": network.get("subversion") if isinstance(network, dict) else None,
    }

    partial = output.with_name(output.name + ".partial.json")
    if resume:
        if output.exists():
            raise RuntimeError("Completed scan output already exists")
        try:
            saved_state = json.loads(partial.read_text())
        except OSError as exc:
            raise RuntimeError("No partial scan is available to resume") from exc
        validate_partial_state(saved_state, checkpoint_hash)
        if saved_state["next_height"] > 0:
            current_hash = rpc.call("getblockhash", saved_state["next_height"] - 1)
            if current_hash != saved_state["last_hash"]:
                raise RuntimeError("Canonical chain changed since the partial scan")
        state = new_state(node, checkpoint_hash)
        scan_range(rpc, state, saved_state["next_height"], batch_size,
                   label="Replayed")
        if any(state[key] != saved_state.get(key) for key in DERIVED_FIELDS):
            raise RuntimeError("Partial scan does not match the replayed prefix")
        state["prefix_replayed_on_resume"] = True
    else:
        if output.exists() or partial.exists():
            raise RuntimeError("Output or partial scan already exists; use --resume")
        state = new_state(node, checkpoint_hash)
        state["prefix_replayed_on_resume"] = False

    scan_range(rpc, state, HEIGHT_GATE_CHECKPOINT + 1, batch_size, partial)

    if state["kawpow_header_count"] == 0:
        raise RuntimeError("No KAWPOW headers were found in the scan range")
    if rpc.call("getblockhash", HEIGHT_GATE_CHECKPOINT) != checkpoint_hash:
        raise RuntimeError("Canonical checkpoint changed during the scan")
    state["node_at_end"] = rpc.call("getblockchaininfo")
    state["mismatch_count"] = len(state["mismatches"])
    state["all_previous_links_verified"] = True
    state["full_verifychain_level4"] = None
    if verifychain:
        state["full_verifychain_level4"] = rpc.call("verifychain", 4, 0)
        if state["full_verifychain_level4"] is not True:
            raise RuntimeError("Full verifychain level 4 did not pass")
    state["assumevalid_zero_verified_by_rpc"] = False
    state["completed_utc"] = datetime.now(timezone.utc).isoformat()
    write_json_atomic(output, state)
    partial.unlink()
    return state


def self_test():
    global HEIGHT_GATE_CHECKPOINT
    prior = "ab" * 32
    prefix = (
        struct.pack("<i", 4)
        + bytes.fromhex(prior)[::-1]
        + bytes.fromhex("cd" * 32)
        + struct.pack("<II", KAWPOW_ACTIVATION_TIME, 0x1D00FFFF)
    )
    raw = prefix + struct.pack("<IQ", 987654321, 7) + bytes.fromhex("ef" * 32)
    assert len(raw) == 120
    assert parse_header(raw.hex()) == (prior, KAWPOW_ACTIVATION_TIME, 987654321)
    legacy = prefix[:68] + struct.pack("<II", KAWPOW_ACTIVATION_TIME - 1, 0x1D00FFFF)
    legacy += struct.pack("<I", 11)
    assert len(legacy) == 80
    assert parse_header(legacy.hex()) == (prior, KAWPOW_ACTIVATION_TIME - 1, None)

    genesis = (
        struct.pack("<i", 4)
        + bytes(32)
        + bytes.fromhex("cd" * 32)
        + struct.pack("<III", KAWPOW_ACTIVATION_TIME - 1, 0x1D00FFFF, 11)
    )
    state = new_state({}, HEIGHT_GATE_CHECKPOINT_HASH)
    update_state(state, 0, prior, genesis.hex())
    update_state(state, 1, "12" * 32, raw.hex())
    assert state["next_height"] == 2
    assert state["legacy_header_count"] == 1
    assert state["kawpow_header_count"] == 1
    assert state["first_kawpow_height"] == 1
    assert state["min_declared_minus_actual"] == 987654320
    assert state["max_declared_epoch_minus_actual_epoch"] == 987654321 // EPOCH_LENGTH
    assert len(state["mismatches"]) == 1
    validate_partial_state(state, HEIGHT_GATE_CHECKPOINT_HASH)

    oversized = dict(state)
    oversized["next_height"] = HEIGHT_GATE_CHECKPOINT + 2
    oversized["legacy_header_count"] = oversized["next_height"] - 1
    try:
        validate_partial_state(oversized, HEIGHT_GATE_CHECKPOINT_HASH)
    except RuntimeError:
        pass
    else:
        raise AssertionError("Forged oversized resume cursor was accepted")

    bad_counts = dict(state)
    bad_counts["legacy_header_count"] = 0
    try:
        validate_partial_state(bad_counts, HEIGHT_GATE_CHECKPOINT_HASH)
    except RuntimeError:
        pass
    else:
        raise AssertionError("Forged resume counts were accepted")

    bad_hash = dict(state)
    bad_hash["last_hash"] = "not-a-hash"
    try:
        validate_partial_state(bad_hash, HEIGHT_GATE_CHECKPOINT_HASH)
    except RuntimeError:
        pass
    else:
        raise AssertionError("Malformed resume block hash was accepted")

    class FakeRPC:
        def call(self, method, *params):
            if method == "getblockchaininfo":
                return {"chain": "main", "pruned": False, "blocks": 2,
                        "headers": 2, "bestblockhash": HEIGHT_GATE_CHECKPOINT_HASH}
            if method == "getnetworkinfo":
                return {"version": 480100, "subversion": "/test/"}
            if method == "getblockhash":
                return (prior, HEIGHT_GATE_CHECKPOINT_HASH)[params[0]]
            raise AssertionError(method)

        def batch(self, calls):
            hashes = (prior, HEIGHT_GATE_CHECKPOINT_HASH)
            raw_headers = (genesis.hex(), raw.hex())
            return [hashes[params[0]] if method == "getblockhash" else
                    raw_headers[hashes.index(params[0])]
                    for method, params in calls]

    original_checkpoint = HEIGHT_GATE_CHECKPOINT
    try:
        HEIGHT_GATE_CHECKPOINT = 1
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "evidence.json"
            partial = output.with_name(output.name + ".partial.json")
            forged = new_state({}, HEIGHT_GATE_CHECKPOINT_HASH)
            forged.update({"next_height": 2, "last_hash": HEIGHT_GATE_CHECKPOINT_HASH,
                           "legacy_header_count": 1, "kawpow_header_count": 1,
                           "first_kawpow_height": 1, "max_declared_height": 1,
                           "min_declared_minus_actual": 0,
                           "max_declared_minus_actual": 0,
                           "max_declared_epoch_minus_actual_epoch": 0,
                           "max_absolute_epoch_difference": 0})
            validate_partial_state(forged, HEIGHT_GATE_CHECKPOINT_HASH)
            write_json_atomic(partial, forged)
            try:
                scan(FakeRPC(), output, 2, True, False)
            except RuntimeError as exc:
                assert "does not match the replayed prefix" in str(exc)
            else:
                raise AssertionError("Forged complete resume state was accepted")
            assert not output.exists()

            correct = new_state({}, HEIGHT_GATE_CHECKPOINT_HASH)
            update_state(correct, 0, prior, genesis.hex())
            update_state(correct, 1, HEIGHT_GATE_CHECKPOINT_HASH, raw.hex())
            write_json_atomic(partial, correct)
            resumed = scan(FakeRPC(), output, 2, True, False)
            assert resumed["prefix_replayed_on_resume"] is True
            assert resumed["mismatch_count"] == 1
            assert resumed["all_previous_links_verified"] is True
    finally:
        HEIGHT_GATE_CHECKPOINT = original_checkpoint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datadir", type=Path, default=Path.home() / ".raven")
    parser.add_argument("--rpc-port", type=int, default=8766)
    parser.add_argument("--output", type=Path, required=False)
    parser.add_argument("--batch-size", type=int, default=500)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--verifychain", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("KAWPOW raw-header parser self-test: OK")
        return 0
    if args.output is None:
        parser.error("--output is required unless --self-test is used")
    if not 1 <= args.rpc_port <= 65535 or not 1 <= args.batch_size <= 1000:
        parser.error("--rpc-port or --batch-size is outside its supported range")
    try:
        rpc = LocalRPC(args.datadir, args.rpc_port)
        state = scan(rpc, args.output, args.batch_size, args.resume, args.verifychain)
    except (RuntimeError, ValueError, KeyError, TypeError) as exc:
        print(f"Scan failed: {exc}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "output": str(args.output),
                "kawpow_header_count": state["kawpow_header_count"],
                "mismatch_count": state["mismatch_count"],
                "max_declared_height": state["max_declared_height"],
                "full_verifychain_level4": state["full_verifychain_level4"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
