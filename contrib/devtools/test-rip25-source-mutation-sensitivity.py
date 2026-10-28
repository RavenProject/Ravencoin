#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Prove that focused RIP-25 tests reject bait-preserving source defects.

This intentionally edits production sources, builds one mutant test binary,
restores the exact source bytes, rebuilds, and runs the same tests green.
Run only in a disposable or otherwise idle checkout. No test failure counts
as a red result unless compilation succeeded and the intended assertion fired.
"""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
TEST_BINARY = ROOT / "src/test/test_raven"

# The four default cases touch separate translation units. This keeps the
# local mutation build practical while testing network, wallet, mining, and
# asset-consensus behavior in one pass.
CASES = (
    {
        "name": "network_protected_class",
        "source": "src/net.cpp",
        "original": "    const size_t nClassMaxSize = fProtected ? nMaxProtectedBulkSize : nMaxNormalBulkSize;",
        "mutant": (
            "    // fProtected ? nMaxProtectedBulkSize : nMaxNormalBulkSize\n"
            "    const size_t nClassMaxSize = nMaxNormalBulkSize;"
        ),
        "filter": "net_tests/incomplete_message_buffer_concurrent_global_limit",
        "assertion": r"net_tests\.cpp\([0-9]+\):.*protectedResult",
    },
    {
        "name": "wallet_rewrite_fail_closed",
        "source": "src/wallet/wallet.cpp",
        "original": "if (!dbw->Rewrite())\n        return false;",
        "mutant": "if (!dbw->Rewrite())\n        return true;",
        "filter": "pq_wallet_tests/rewrite_failure_quarantines_until_restart_recovery",
        "assertion": r"pq_wallet_tests\.cpp\([0-9]+\):.*!wallet->EncryptWallet\(passphrase\)",
    },
    {
        "name": "gbt_contextual_entry_weight",
        "source": "src/rpc/mining.cpp",
        "original": "pblocktemplate->vTxWeights.at(index_in_template)",
        "mutant": "(false ? pblocktemplate->vTxWeights.at(index_in_template) : GetTransactionWeight(tx))",
        "filter": "rip25_miner_tests/gbt_entries_report_contextual_pq_weight",
        "assertion": r"miner_tests\.cpp\([0-9]+\):.*weight == expectedWrapped",
    },
    {
        "name": "asset_anchor_consensus",
        "source": "src/consensus/tx_verify.cpp",
        "original": "if (anchorPrograms.count(program) == 0)",
        "mutant": "if (false && anchorPrograms.count(program) == 0)",
        "filter": "asset_tx_tests/pq_asset_creation_height_and_anchor_rule_test",
        "assertion": r"asset_tx_tests\.cpp\([0-9]+\):.*(?:noAnchor|bad-pq-asset-anchor)",
    },
)

# Header mutations require a wider incremental rebuild. CI may opt into them
# with --full when sufficient disk and build time are available.
FULL_CASES = (
    {
        "name": "witness_parser_destination",
        "source": "src/script/witness.h",
        "original": "        swap(parsed);",
        "mutant": "        if (false) swap(parsed);",
        "filter": "transaction_tests/compact_witness_move_leaves_valid_source",
        "assertion": r"transaction_tests\.cpp\([0-9]+\):.*moved\.stack\.size\(\)",
        "rebuild_source": "src/test/transaction_tests.cpp",
    },
    {
        "name": "witness_parser_chunk_limit",
        "source": "src/script/witness.h",
        "original": "std::min<uint64_t>(remaining, READ_CHUNK_SIZE)",
        "mutant": "std::min<uint64_t>(remaining, MAX_SIZE) /* READ_CHUNK_SIZE */",
        "filter": "transaction_tests/compact_witness_truncated_element_is_atomic_and_chunked",
        "assertion": r"transaction_tests\.cpp\([0-9]+\):.*max_read_request",
        "rebuild_source": "src/test/transaction_tests.cpp",
    },
    {
        "name": "block_parser_count_bound",
        "source": "src/primitives/block.h",
        "original": "if (transactionCount > MAX_BLOCK_TRANSACTION_COUNT)",
        "mutant": "if (false && transactionCount > MAX_BLOCK_TRANSACTION_COUNT)",
        "filter": "blockencodings_tests/block_family_counts_reject_before_element_read",
        "assertion": r"blockencodings_tests\.cpp\([0-9]+\):.*(?:blockStream|read_past_end|Position)",
        "rebuild_source": "src/test/blockencodings_tests.cpp",
    },
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_command(name, command, evidence_dir, timeout, expected_return=0):
    logfile = evidence_dir / (name + ".log")
    started = time.monotonic()
    with logfile.open("w", encoding="utf-8") as output:
        output.write("Command: " + " ".join(command) + "\n")
        output.flush()
        try:
            result = subprocess.run(
                command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                timeout=timeout, check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise RuntimeError("{} timed out after {} seconds: {}".format(
                name, timeout, logfile)) from error
    if expected_return is not None and result.returncode != expected_return:
        raise RuntimeError("{} returned {}, expected {}: {}".format(
            name, result.returncode, expected_return, logfile))
    return {
        "command": command,
        "returncode": result.returncode,
        "duration_seconds": round(time.monotonic() - started, 3),
        "log": str(logfile),
    }


def build_binary(label, evidence_dir, jobs, timeout):
    result = run_command(
        label, ["make", "-C", "src", "-j{}".format(jobs),
                "test/test_raven"], evidence_dir, timeout,
    )
    if not TEST_BINARY.is_file() or not os.access(TEST_BINARY, os.X_OK):
        raise RuntimeError("build returned success without an executable test_raven")
    result["binary_sha256"] = sha256_file(TEST_BINARY)
    return result


def run_focused(case, phase, evidence_dir, timeout):
    name = "{}-{}".format(phase, case["name"])
    result = run_command(
        name,
        [str(TEST_BINARY), "--run_test=" + case["filter"],
         "--log_level=test_suite", "--report_level=short"],
        evidence_dir, timeout, expected_return=None,
    )
    output = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    if "Running 1 test case" not in output:
        raise RuntimeError("{} did not run exactly one focused test: {}".format(
            name, result["log"]))
    if phase == "red":
        if result["returncode"] == 0:
            raise RuntimeError("{} did not reject the mutant: {}".format(
                name, result["log"]))
        if not re.search(case["assertion"], output):
            raise RuntimeError("{} failed for a reason other than the intended assertion: {}".format(
                name, result["log"]))
    elif (result["returncode"] != 0 or
          "1 test case out of 1 passed" not in output):
        raise RuntimeError("{} did not pass after source restoration: {}".format(
            name, result["log"]))
    result["intended_assertion"] = case["assertion"] if phase == "red" else None
    return result


def install_signal_handlers():
    def interrupt(signum, _frame):
        raise InterruptedError("interrupted by signal {}".format(signum))

    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGHUP, interrupt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true",
                        help="also mutate witness and block parsing headers")
    parser.add_argument("--evidence-dir", type=Path,
                        help="directory outside the repository for logs and JSON")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--build-timeout", type=int, default=1800)
    parser.add_argument("--test-timeout", type=int, default=300)
    args = parser.parse_args()
    if args.jobs < 1 or args.build_timeout < 1 or args.test_timeout < 1:
        parser.error("jobs and timeouts must be positive")

    cases = CASES + (FULL_CASES if args.full else ())
    evidence_dir = args.evidence_dir
    if evidence_dir is None:
        evidence_dir = Path(tempfile.mkdtemp(prefix="rip25-f020-mutation-"))
    evidence_dir = evidence_dir.resolve()
    if evidence_dir == ROOT or ROOT in evidence_dir.parents:
        parser.error("evidence directory must be outside the repository")
    evidence_dir.mkdir(parents=True, exist_ok=True)

    lock_path = Path(tempfile.gettempdir()) / (
        "rip25-f020-mutation-" + sha256(str(ROOT).encode())[:16] + ".lock")
    summary = {
        "source_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "full": args.full,
        "cases": {},
        "source_sha256": {},
        "result": "incomplete",
    }
    install_signal_handlers()

    with lock_path.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        sources = {}
        mutants = {}
        paths = sorted({case["source"] for case in cases})
        forced_rebuild_sources = sorted({
            case["rebuild_source"] for case in cases if "rebuild_source" in case
        })
        summary["forced_rebuild_sources"] = forced_rebuild_sources
        subprocess.run(["git", "diff", "--quiet", "HEAD", "--", *paths],
                       cwd=ROOT, check=True)
        for path in paths:
            source = ROOT / path
            sources[path] = source.read_bytes()
            summary["source_sha256"][path] = sha256(sources[path])
            mutated = sources[path].decode("utf-8")
            for case in cases:
                if case["source"] != path:
                    continue
                if mutated.count(case["original"]) != 1:
                    raise RuntimeError("expected exactly one mutation site in " + path)
                mutated = mutated.replace(case["original"], case["mutant"], 1)
            mutants[path] = mutated.encode("utf-8")

        matrix_error = None
        try:
            for path in paths:
                (ROOT / path).write_bytes(mutants[path])
            for path in forced_rebuild_sources:
                (ROOT / path).touch()
            summary["mutant_structural_gate"] = run_command(
                "mutant-structural-gate",
                ["bash", "contrib/devtools/check-rip25-v48-invariants.sh",
                 "--structural-only"], evidence_dir, args.test_timeout,
            )
            summary["mutant_build"] = build_binary(
                "mutant-build", evidence_dir, args.jobs, args.build_timeout)
            for case in cases:
                summary["cases"][case["name"]] = {
                    "filter": case["filter"],
                    "red": run_focused(case, "red", evidence_dir, args.test_timeout),
                }
        except BaseException as error:
            matrix_error = error
        finally:
            try:
                for path in paths:
                    current = (ROOT / path).read_bytes()
                    if current == mutants[path]:
                        (ROOT / path).write_bytes(sources[path])
                    elif current != sources[path]:
                        raise RuntimeError(
                            "concurrent source edit detected; refusing to overwrite " + path)
                summary["restored_source_sha256"] = {
                    path: sha256((ROOT / path).read_bytes()) for path in paths
                }
                for path in paths:
                    if (ROOT / path).read_bytes() != sources[path]:
                        raise RuntimeError("source restoration failed: " + path)
                for path in forced_rebuild_sources:
                    (ROOT / path).touch()
                summary["restored_structural_gate"] = run_command(
                    "restored-structural-gate",
                    ["bash", "contrib/devtools/check-rip25-v48-invariants.sh",
                     "--structural-only"], evidence_dir, args.test_timeout,
                )
                summary["restored_build"] = build_binary(
                    "restored-build", evidence_dir, args.jobs,
                    args.build_timeout)
                for case in cases:
                    summary["cases"].setdefault(case["name"], {
                        "filter": case["filter"],
                    })["green"] = run_focused(
                        case, "green", evidence_dir, args.test_timeout)
            except BaseException as restore_error:
                summary["restore_error"] = str(restore_error)
                raise
            finally:
                if matrix_error is not None:
                    summary["matrix_error"] = str(matrix_error)
                (evidence_dir / "mutation-summary.json").write_text(
                    json.dumps(summary, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )

        if matrix_error is not None:
            raise matrix_error
        summary["result"] = "red-green"
        (evidence_dir / "mutation-summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    for case in cases:
        result = summary["cases"][case["name"]]
        print("F020 {}: mutant exit {}, restored exit {}".format(
            case["name"], result["red"]["returncode"],
            result["green"]["returncode"]))
    print("F020 source mutations: {} red-green; evidence: {}".format(
        len(cases), evidence_dir))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            InterruptedError) as error:
        print("F020 source mutation matrix failed: {}".format(error),
              file=sys.stderr)
        sys.exit(1)
