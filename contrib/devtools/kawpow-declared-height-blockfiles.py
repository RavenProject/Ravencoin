#!/usr/bin/env python3
"""Scan canonical mainnet KAWPOW headers directly from Raven block files.

The walk starts at the pinned height-gate checkpoint and follows previous-block
links back to the first KAWPOW block. Every KAWPOW header on that walk is bound
to its hash by recomputing the KAWPOW final hash from the stored header fields,
nonce, and claimed mix, so neither block-file order nor a node index is trusted.
The checkpoint hash is the only anchor. The tool only reads files.

The final hash does not prove the ProgPoW mix; a node's full proof check does
that. The checkpoint already fixes which headers are canonical, so the declared
heights reported here are those that any syncing node must verify.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import mmap
from multiprocessing import Pool
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

import numpy as np


MAGIC = b"RAVN"
KAWPOW_ACTIVATION_TIME = 1588788000
HEIGHT_GATE_CHECKPOINT = 4487775
HEIGHT_GATE_CHECKPOINT_HASH = (
    "000000000002d64509e06e76ddbbe418c725291687ec62b41ecfc40386a091fd"
)
EPOCH_LENGTH = 7500
DEFAULT_BUDGET_MIB = 256
L1_CACHE_BYTES = 16 * 1024
HASH512_BYTES = 64
SCHEMA_VERSION = 1

RAVENCOIN_KAWPOW = np.array(
    [0x72, 0x41, 0x56, 0x45, 0x4E, 0x43, 0x4F, 0x49,
     0x4E, 0x4B, 0x41, 0x57, 0x50, 0x4F, 0x57], dtype=np.uint32)
ROUND_CONSTANTS = [
    0x00000001, 0x00008082, 0x0000808A, 0x80008000, 0x0000808B, 0x80000001,
    0x80008081, 0x00008009, 0x0000008A, 0x00000088, 0x80008009, 0x8000000A,
    0x8000808B, 0x0000008B, 0x00008089, 0x00008003, 0x00008002, 0x00000080,
    0x0000800A, 0x8000000A, 0x80008081, 0x00008080,
]
# Keccak rotation offsets r[x][y], reduced to 32-bit lanes.
ROTATIONS = [
    [0, 36, 3, 41, 18],
    [1, 44, 10, 45, 2],
    [62, 6, 43, 15, 61],
    [28, 55, 25, 21, 56],
    [27, 20, 39, 8, 14],
]


def rotl32(lane, shift):
    shift %= 32
    if shift == 0:
        return lane
    return (lane << np.uint32(shift)) | (lane >> np.uint32(32 - shift))


def keccak_f800(state):
    """Apply Keccak-f[800] to a (25, N) uint32 state in place."""
    lanes = [state[i].copy() for i in range(25)]
    for constant in ROUND_CONSTANTS:
        parity = [lanes[x] ^ lanes[x + 5] ^ lanes[x + 10] ^ lanes[x + 15] ^ lanes[x + 20]
                  for x in range(5)]
        for x in range(5):
            mixed = parity[(x - 1) % 5] ^ rotl32(parity[(x + 1) % 5], 1)
            for y in range(5):
                lanes[x + 5 * y] = lanes[x + 5 * y] ^ mixed
        moved = [None] * 25
        for x in range(5):
            for y in range(5):
                moved[y + 5 * ((2 * x + 3 * y) % 5)] = rotl32(lanes[x + 5 * y], ROTATIONS[x][y])
        for y in range(5):
            row = moved[5 * y:5 * y + 5]
            for x in range(5):
                lanes[x + 5 * y] = row[x] ^ (~row[(x + 1) % 5] & row[(x + 2) % 5])
        lanes[0] = lanes[0] ^ np.uint32(constant)
    for i in range(25):
        state[i] = lanes[i]


def kawpow_final_hashes(headers):
    """Return the claimed-mix KAWPOW final hashes of 120-byte headers.

    The result matches CBlockHeader::GetHash() for KAWPOW headers: an (N, 32)
    uint8 array whose bytes, read in order, are the displayed block hash.
    """
    count = len(headers) // 120
    raw = np.frombuffer(headers, dtype=np.uint8).reshape(count, 120)
    header_hashes = b"".join(
        hashlib.sha256(hashlib.sha256(headers[i * 120:i * 120 + 80]).digest()).digest()[::-1]
        for i in range(count))
    header_words = np.frombuffer(header_hashes, dtype="<u4").reshape(count, 8)
    nonces = np.ascontiguousarray(raw[:, 80:88]).view("<u8").reshape(count)
    # Header serialization stores uint256 bytes reversed from ethash's hash256.
    mix_words = np.ascontiguousarray(raw[:, 88:120][:, ::-1]).view("<u4").reshape(count, 8)

    state = np.zeros((25, count), dtype=np.uint32)
    state[0:8] = header_words.T
    state[8] = (nonces & np.uint64(0xFFFFFFFF)).astype(np.uint32)
    state[9] = (nonces >> np.uint64(32)).astype(np.uint32)
    state[10:25] = RAVENCOIN_KAWPOW[:, None]
    keccak_f800(state)

    carried = state[0:8].copy()
    state[:] = 0
    state[0:8] = carried
    state[8:16] = mix_words.T
    state[16:25] = RAVENCOIN_KAWPOW[0:9, None]
    keccak_f800(state)
    return np.ascontiguousarray(state[0:8].T).astype("<u4").view(np.uint8).reshape(count, 32)


def scan_block_file(path):
    """Return KAWPOW header data and record counts from one block file."""
    kawpow_headers = bytearray()
    legacy_count = 0
    skipped_bytes = 0
    with open(path, "rb") as handle:
        size = os.fstat(handle.fileno()).st_size
        if size == 0:
            return path.name, b"", 0, 0, 0
        data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            position = 0
            while position + 8 <= size:
                if data[position:position + 4] != MAGIC:
                    following = data.find(MAGIC, position + 1)
                    if following < 0:
                        skipped_bytes += size - position
                        break
                    skipped_bytes += following - position
                    position = following
                    continue
                length = struct.unpack_from("<I", data, position + 4)[0]
                start = position + 8
                if length < 80 or start + length > size:
                    raise RuntimeError(f"{path.name}: invalid block record at offset {position}")
                timestamp = struct.unpack_from("<I", data, start + 68)[0]
                if timestamp >= KAWPOW_ACTIVATION_TIME:
                    if length < 120:
                        raise RuntimeError(f"{path.name}: short KAWPOW block at offset {position}")
                    kawpow_headers += data[start:start + 120]
                else:
                    legacy_count += 1
                position = start + length
        finally:
            data.close()
    count = len(kawpow_headers) // 120
    if count == 0:
        return path.name, b"", 0, legacy_count, skipped_bytes
    headers = bytes(kawpow_headers)
    hashes = kawpow_final_hashes(headers)
    return path.name, hashes.tobytes() + headers, count, legacy_count, skipped_bytes


def context_mib_for_height(height):
    """Mirror IsKAWPOWContextWithinLocalBudget(): bytes needed, rounded up to MiB."""
    epoch = height // EPOCH_LENGTH
    allocation = HASH512_BYTES + (1 << 24) + epoch * (1 << 17) + L1_CACHE_BYTES
    return -(-allocation // (1 << 20))


def load_headers(blocks_dir, jobs):
    files = sorted(Path(blocks_dir).glob("blk*.dat"))
    if not files:
        raise RuntimeError("No blk*.dat files found")
    by_hash = {}
    headers = []
    totals = {"files": len(files), "legacy_records": 0, "kawpow_records": 0,
              "skipped_bytes": 0, "duplicate_records": 0}
    with Pool(jobs) as pool:
        for name, blob, count, legacy, skipped in pool.imap(scan_block_file, files):
            totals["legacy_records"] += legacy
            totals["kawpow_records"] += count
            totals["skipped_bytes"] += skipped
            hashes, raw = blob[:32 * count], blob[32 * count:]
            for index in range(count):
                key = hashes[32 * index:32 * index + 32]
                if key in by_hash:
                    totals["duplicate_records"] += 1
                    continue
                by_hash[key] = len(headers)
                headers.append(raw[120 * index:120 * index + 120])
    return by_hash, headers, totals


def walk_canonical_chain(by_hash, headers):
    target = bytes.fromhex(HEIGHT_GATE_CHECKPOINT_HASH)
    if target not in by_hash:
        raise RuntimeError("Pinned checkpoint header is absent or hashes differently")
    actual_height = HEIGHT_GATE_CHECKPOINT
    walk = []
    while target in by_hash:
        header = headers[by_hash[target]]
        previous = header[4:36][::-1]
        timestamp = struct.unpack_from("<I", header, 68)[0]
        declared = struct.unpack_from("<I", header, 76)[0]
        walk.append((actual_height, target.hex(), declared, timestamp))
        target = previous
        actual_height -= 1
    return walk, target.hex(), actual_height


def summarize(walk, boundary_hash, boundary_height, budget_mib):
    max_declared = max(item[2] for item in walk)
    deltas = [item[2] - item[0] for item in walk]
    mismatches = [
        {"height": height, "hash": block_hash, "declared_height": declared,
         "declared_minus_actual": declared - height,
         "declared_epoch_minus_actual_epoch":
             declared // EPOCH_LENGTH - height // EPOCH_LENGTH}
        for height, block_hash, declared, _ in walk if declared != height
    ]
    first = walk[-1]
    max_context_mib = context_mib_for_height(max_declared)
    return {
        "kawpow_header_count": len(walk),
        "first_kawpow_height": first[0],
        "first_kawpow_hash": first[1],
        "first_kawpow_time": datetime.fromtimestamp(first[3], timezone.utc).isoformat(),
        "boundary_parent_height": boundary_height,
        "boundary_parent_hash": boundary_hash,
        "max_declared_height": max_declared,
        "max_declared_epoch": max_declared // EPOCH_LENGTH,
        "max_actual_epoch": HEIGHT_GATE_CHECKPOINT // EPOCH_LENGTH,
        "min_declared_minus_actual": min(deltas),
        "max_declared_minus_actual": max(deltas),
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
        "max_context_mib_required": max_context_mib,
        "default_budget_mib": budget_mib,
        "within_default_budget": max_context_mib <= budget_mib,
        "within_signed_int_bound": max_declared <= 0x7FFFFFFF,
    }


def summarize_off_walk(by_hash, headers, walk):
    """Describe KAWPOW records that are not ancestors of the checkpoint."""
    on_walk = {bytes.fromhex(item[1]) for item in walk}
    declared = []
    times = []
    for key, index in by_hash.items():
        if key in on_walk:
            continue
        declared.append(struct.unpack_from("<I", headers[index], 76)[0])
        times.append(struct.unpack_from("<I", headers[index], 68)[0])
    if not declared:
        return {"count": 0}
    return {
        "count": len(declared),
        "min_declared_height": min(declared),
        "max_declared_height": max(declared),
        "earliest_time": datetime.fromtimestamp(min(times), timezone.utc).isoformat(),
        "max_context_mib_required": context_mib_for_height(max(declared)),
    }


def cross_check_debug_log(path, walk):
    """Compare walked hashes with UpdateTip lines from a validating node's log."""
    by_height = {height: block_hash for height, block_hash, _, _ in walk}
    pattern = re.compile(r"UpdateTip: new best=([0-9a-f]{64}) height=(\d+)")
    matched = 0
    differing = []
    with open(path, "r", errors="replace") as handle:
        for line in handle:
            found = pattern.search(line)
            if not found:
                continue
            height = int(found.group(2))
            if height not in by_height:
                continue
            if by_height[height] == found.group(1):
                matched += 1
            else:
                differing.append(height)
    return {"matched_heights": matched, "differing_heights": differing[:20],
            "differing_count": len(differing)}


def self_test():
    # Header 4487775, the pinned height-gate checkpoint.
    checkpoint = bytes.fromhex(CHECKPOINT_HEADER_HEX)
    computed = kawpow_final_hashes(checkpoint).tobytes().hex()
    assert computed == HEIGHT_GATE_CHECKPOINT_HASH, computed
    assert struct.unpack_from("<I", checkpoint, 76)[0] == HEIGHT_GATE_CHECKPOINT
    altered = bytearray(checkpoint)
    altered[76] ^= 1
    assert kawpow_final_hashes(bytes(altered)).tobytes().hex() != HEIGHT_GATE_CHECKPOINT_HASH
    assert context_mib_for_height(1919 * EPOCH_LENGTH) <= 256
    assert context_mib_for_height(1920 * EPOCH_LENGTH) > 256
    print("self-test passed")


CHECKPOINT_HEADER_HEX = (
    "000000307a562b7789be7f55ef7e50dfd15469f925bc237f5425ebd64302020000000000"
    "f41fdf2269cc2f6871d6cd68e86f0e8f8903967abee6384dc588fa2262ccf8973dfd756a"
    "f2ad051b5f7a44003313e49268e2b7b93f187ed01113c3606445e72c479a8c62a168bc40"
    "c22d48103e8cbb766d125d62"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--blocksdir", help="mainnet blocks directory to read")
    parser.add_argument("--output", help="JSON report path")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--debug-log", help="validating node debug.log for UpdateTip cross-checks")
    parser.add_argument("--budget-mib", type=int, default=DEFAULT_BUDGET_MIB)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0
    if not args.blocksdir or not args.output:
        parser.error("--blocksdir and --output are required unless --self-test is used")

    started = datetime.now(timezone.utc).isoformat()
    by_hash, headers, totals = load_headers(args.blocksdir, args.jobs)
    walk, boundary_hash, boundary_height = walk_canonical_chain(by_hash, headers)
    report = {
        "schema_version": SCHEMA_VERSION,
        "started": started,
        "finished": None,
        "blocksdir": str(Path(args.blocksdir).resolve()),
        "checkpoint_height": HEIGHT_GATE_CHECKPOINT,
        "checkpoint_hash": HEIGHT_GATE_CHECKPOINT_HASH,
        "files": totals,
        "result": summarize(walk, boundary_hash, boundary_height, args.budget_mib),
        "off_walk_kawpow_records": summarize_off_walk(by_hash, headers, walk),
    }
    if args.debug_log:
        report["debug_log_cross_check"] = cross_check_debug_log(args.debug_log, walk)
    report["finished"] = datetime.now(timezone.utc).isoformat()

    output = Path(args.output)
    with tempfile.NamedTemporaryFile("w", dir=output.parent, delete=False) as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")
        temporary = handle.name
    os.replace(temporary, output)
    result = report["result"]
    print(json.dumps({key: result[key] for key in result if key != "mismatches"}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
