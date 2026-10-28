#!/usr/bin/env python3
"""Check that inert workflow bait cannot satisfy the invariant gate."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "contrib/devtools/check-rip25-v48-invariants.sh"
WORKFLOWS = ("build-raven.yml", "rip25-v48-final-gate.yml")


class InvariantGateNegativeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="rip25-gate-")
        cls.fixture = Path(cls.tempdir.name)
        for entry in ROOT.iterdir():
            if entry.name in (".git", ".github"):
                continue
            (cls.fixture / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
        github = cls.fixture / ".github"
        github.mkdir()
        for entry in (ROOT / ".github").iterdir():
            if entry.name == "workflows":
                shutil.copytree(entry, github / entry.name)
            else:
                (github / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def run_gate(self, fixture=None):
        return subprocess.run(
            ["bash", str(GATE), "--structural-only"], cwd=fixture or self.fixture,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )

    def check_init_mutation(self, old, new, failure):
        with tempfile.TemporaryDirectory(prefix="rip25-chainstate-gate-") as directory:
            fixture = Path(directory)
            for entry in ROOT.iterdir():
                if entry.name == ".git":
                    continue
                if entry.name != "src":
                    (fixture / entry.name).symlink_to(
                        entry, target_is_directory=entry.is_dir()
                    )
                    continue
                source_dir = fixture / "src"
                source_dir.mkdir()
                for source_entry in entry.iterdir():
                    if source_entry.name != "init.cpp":
                        (source_dir / source_entry.name).symlink_to(
                            source_entry, target_is_directory=source_entry.is_dir()
                        )
                        continue
                    original = source_entry.read_text(encoding="utf-8")
                    self.assertEqual(original.count(old), 1)
                    (source_dir / "init.cpp").write_text(
                        original.replace(old, new, 1), encoding="utf-8"
                    )
            result = self.run_gate(fixture)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(failure, result.stderr)

    def check_mutation(self, workflow, old, new, occurrence=1):
        path = self.fixture / ".github/workflows" / workflow
        original = path.read_text(encoding="utf-8")
        self.assertIn(old, original, "The workflow fixture no longer matches the test")
        self.assertGreaterEqual(original.count(old), occurrence)
        at = -1
        for _ in range(occurrence):
            at = original.index(old, at + 1)
        mutated = original[:at] + new + original[at + len(old):]
        path.write_text(mutated, encoding="utf-8")
        try:
            result = self.run_gate()
            self.assertNotEqual(
                result.returncode, 0,
                workflow + " accepted inert bait: " + old + "\n" + result.stdout,
            )
        finally:
            path.write_text(original, encoding="utf-8")

    def test_unmodified_workflows_pass(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_chainstate_recovery_requires_invalid_tip_in_ahead_detection(self):
        self.check_init_mutation(
            "fCoinsAheadOfIndex = tip == mapBlockIndex.end() || fInvalidIndexedTip;",
            "fCoinsAheadOfIndex = tip == mapBlockIndex.end();",
            "chainstate-ahead detection omits invalid indexed tips",
        )

    def test_chainstate_recovery_rejects_commented_detection(self):
        detection = "fCoinsAheadOfIndex = tip == mapBlockIndex.end() || fInvalidIndexedTip;"
        self.check_init_mutation(
            detection, "// " + detection,
            "chainstate-ahead detection omits invalid indexed tips",
        )

    def test_chainstate_recovery_rejects_invalid_indexed_tip(self):
        self.check_init_mutation(
            "if (fInvalidIndexedTip) {",
            "if (false && fInvalidIndexedTip) {",
            "invalid indexed chainstate tip does not fail closed",
        )

    def test_chainstate_recovery_rejects_pruned_unknown_tip(self):
        self.check_init_mutation(
            "if (fHavePruned) {\n"
            "                                return InitError(_(\"The coins database references an unknown block",
            "if (false && fHavePruned) {\n"
            "                                return InitError(_(\"The coins database references an unknown block",
            "pruned unknown chainstate tip does not fail closed",
        )

    def test_build_raven_dependency_is_executable_yaml(self):
        self.check_mutation(
            WORKFLOWS[0], "    needs: security-tests",
            "    #    needs: security-tests",
        )

    def test_build_raven_make_check_is_executed(self):
        self.check_mutation(
            WORKFLOWS[0], "          if ! make check; then",
            "          if ! echo 'make check'; then",
        )

    def test_build_raven_sha_check_is_executed_before_and_after(self):
        old = '          test "$(git rev-parse HEAD)" = "$GITHUB_SHA"'
        self.check_mutation(WORKFLOWS[0], old, "          # " + old.strip())
        self.check_mutation(WORKFLOWS[0], old, "          # " + old.strip(), occurrence=2)

    def test_final_gate_make_check_is_executed(self):
        self.check_mutation(
            WORKFLOWS[1], "        run: make check",
            "        run: echo 'make check'",
        )

    def test_final_gate_sha_check_is_executed_before_and_after(self):
        old = '          test "$(git rev-parse HEAD)" = "$GITHUB_SHA"'
        self.check_mutation(WORKFLOWS[1], old, "          # " + old.strip())
        self.check_mutation(WORKFLOWS[1], old, "          # " + old.strip(), occurrence=2)

    def test_behavioral_suite_cannot_be_replaced_with_echo(self):
        old = "        run: ./contrib/devtools/check-rip25-v48-invariants.sh --run-tests"
        new = "        run: echo '" + old.strip() + "'"
        for workflow in WORKFLOWS:
            with self.subTest(workflow=workflow):
                self.check_mutation(workflow, old, new)

    def test_source_mutation_matrix_cannot_be_replaced_with_echo(self):
        old = "        run: python3 contrib/devtools/test-rip25-source-mutation-sensitivity.py --full"
        new = "        run: echo '" + old.strip() + "'"
        for workflow in WORKFLOWS:
            with self.subTest(workflow=workflow):
                self.check_mutation(workflow, old, new)

    def test_functional_suite_cannot_be_hidden_in_here_document(self):
        start = "          python3 test/functional/test_runner.py --require-tests --jobs=2 \\"
        end = "feature_reissue_mempool_block_split.py feature_restricted_assets.py feature_raw_restricted_assets.py"
        for workflow in WORKFLOWS:
            with self.subTest(workflow=workflow):
                source = (self.fixture / ".github/workflows" / workflow).read_text(
                    encoding="utf-8"
                )
                first = source.index(start)
                last = source.index(end, first) + len(end)
                old = source[first:last]
                new = "          : <<'RIP25_INERT'\n" + old + "\n          RIP25_INERT"
                self.check_mutation(workflow, old, new)

    def test_security_steps_cannot_be_conditional_or_fail_open(self):
        steps = (
            (WORKFLOWS[0], "      - name: Run behavioral security invariants"),
            (WORKFLOWS[0], "      - name: Run required functional security tests"),
            (WORKFLOWS[1], "      - id: behavioral_invariants"),
            (WORKFLOWS[1], "      - id: required_functional"),
        )
        for workflow, step in steps:
            for bypass in ("        if: false", "        continue-on-error: true"):
                with self.subTest(workflow=workflow, step=step, bypass=bypass):
                    self.check_mutation(workflow, step, step + "\n" + bypass)

    def test_security_jobs_cannot_be_conditional_or_fail_open(self):
        for workflow in WORKFLOWS:
            for bypass in ("    if: false", "    continue-on-error: true"):
                with self.subTest(workflow=workflow, bypass=bypass):
                    self.check_mutation(workflow, "  security-tests:",
                                        "  security-tests:\n" + bypass)

    def test_required_functional_jobs_cannot_exit_early(self):
        blocks = (
            (WORKFLOWS[0],
             "      - name: Run required functional security tests\n        run: |"),
            (WORKFLOWS[1],
             "      - id: required_functional\n"
             "        name: Run required functional security tests\n"
             "        shell: bash\n        run: |"),
        )
        for workflow, block in blocks:
            with self.subTest(workflow=workflow):
                self.check_mutation(workflow, block, block + "\n          exit 0")

    def test_release_build_job_cannot_fail_open(self):
        self.check_mutation(WORKFLOWS[0], "  build:",
                            "  build:\n    continue-on-error: true")

    def test_release_package_attestation_commands_are_executable(self):
        workflow = WORKFLOWS[0]
        commands = (
            '        git archive --format=tar --prefix="${distname}/" HEAD > "$source_tar"',
            '        test "$(git get-tar-commit-id < "$source_tar")" = "$GITHUB_SHA"',
            '          test -s "$artifact"',
            '        grep -Fx "commit=$GITHUB_SHA" PROVENANCE.txt',
            '        sha256sum -- "${artifacts[@]}" "$source_archive" PROVENANCE.txt > SHA256SUMS',
            '        sha256sum --check SHA256SUMS',
        )
        for command in commands:
            with self.subTest(command=command):
                self.check_mutation(workflow, command,
                                    "        echo '" + command.strip() + "'")

    def test_release_package_attestation_cannot_be_hidden_or_skipped(self):
        workflow = WORKFLOWS[0]
        step = "    - name: Verify Packaged Source and Artifacts"
        for bypass in ("      if: false", "      continue-on-error: true"):
            with self.subTest(bypass=bypass):
                self.check_mutation(workflow, step, step + "\n" + bypass)
        command = "        sha256sum --check SHA256SUMS"
        self.check_mutation(workflow, command,
                            "        : <<'RIP25_INERT'\n" + command +
                            "\n        RIP25_INERT")

    def test_release_package_attestation_cannot_exit_early(self):
        workflow = WORKFLOWS[0]
        step = ("    - name: Verify Packaged Source and Artifacts\n"
                "      shell: bash\n      run: |")
        self.check_mutation(workflow, step, step + "\n        exit 0")

    def test_release_upload_cannot_be_hidden_or_skipped(self):
        workflow = WORKFLOWS[0]
        step = "    - name: Upload Artifacts to Job"
        for bypass in ("      if: false", "      continue-on-error: true"):
            with self.subTest(bypass=bypass):
                self.check_mutation(workflow, step, step + "\n" + bypass)

    def test_independent_artifact_verification_is_required(self):
        workflow = WORKFLOWS[0]
        dependency = "    needs: build"
        self.check_mutation(workflow, dependency,
                            "    # needs: build")
        job = "  verify-artifacts:"
        for bypass in ("    if: false", "    continue-on-error: true"):
            with self.subTest(bypass=bypass):
                self.check_mutation(workflow, job, job + "\n" + bypass)

    def test_independent_artifact_verifier_must_execute(self):
        workflow = WORKFLOWS[0]
        step = "      - name: Verify package provenance and binary targets"
        for bypass in ("        if: false", "        continue-on-error: true"):
            with self.subTest(bypass=bypass):
                self.check_mutation(workflow, step, step + "\n" + bypass)
        command = "          python3 contrib/devtools/verify_build_raven_artifacts.py \\"
        self.check_mutation(workflow, command,
                            "          echo '" + command.strip() + "'")

    def test_independent_artifact_verifier_cannot_exit_early(self):
        workflow = WORKFLOWS[0]
        run_block = "        run: |\n          python3 contrib/devtools/verify_build_raven_artifacts.py \\"
        self.check_mutation(workflow, run_block,
                            "        run: |\n          exit 0\n"
                            "          python3 contrib/devtools/verify_build_raven_artifacts.py \\")

    def test_wallet_modal_prompt_removal_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="rip25-wallet-gate-") as directory:
            fixture = Path(directory)
            for entry in ROOT.iterdir():
                if entry.name == ".git":
                    continue
                if entry.name != "src":
                    (fixture / entry.name).symlink_to(
                        entry, target_is_directory=entry.is_dir())
                    continue
                source_dir = fixture / "src"
                source_dir.mkdir()
                for source_entry in entry.iterdir():
                    if source_entry.name != "wallet":
                        (source_dir / source_entry.name).symlink_to(
                            source_entry, target_is_directory=source_entry.is_dir())
                        continue
                    wallet_dir = source_dir / "wallet"
                    wallet_dir.mkdir()
                    for wallet_entry in source_entry.iterdir():
                        if wallet_entry.name != "wallet.cpp":
                            (wallet_dir / wallet_entry.name).symlink_to(
                                wallet_entry, target_is_directory=wallet_entry.is_dir())
                            continue
                        original = wallet_entry.read_text(encoding="utf-8")
                        modal_call = "uiInterface.ShowMnemonic(CClientUIInterface::MODAL);"
                        self.assertEqual(original.count(modal_call), 1)
                        (wallet_dir / "wallet.cpp").write_text(
                            original.replace(modal_call, "/* prompt omitted */", 1),
                            encoding="utf-8",
                        )
            result = subprocess.run(
                ["bash", str(GATE), "--structural-only"], cwd=fixture,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn("cannot locate the BIP44 atomic-creation boundaries",
                          result.stderr)

    def test_inert_contextual_discount_bait_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="rip25-weight-gate-") as directory:
            fixture = Path(directory)
            for entry in ROOT.iterdir():
                if entry.name == ".git":
                    continue
                if entry.name != "src":
                    (fixture / entry.name).symlink_to(
                        entry, target_is_directory=entry.is_dir()
                    )
                    continue
                source_dir = fixture / "src"
                source_dir.mkdir()
                for source_entry in entry.iterdir():
                    if source_entry.name != "validation.cpp":
                        (source_dir / source_entry.name).symlink_to(
                            source_entry, target_is_directory=source_entry.is_dir()
                        )
                        continue
                    original = source_entry.read_text(encoding="utf-8")
                    operation = "                contextualBlockWeight -= GetContextualPQWitnessDiscount(tx, view);"
                    self.assertEqual(original.count(operation), 1)
                    (source_dir / "validation.cpp").write_text(
                        original.replace(operation, "                // " + operation.strip(), 1),
                        encoding="utf-8",
                    )
            result = subprocess.run(
                ["bash", str(GATE), "--structural-only"], cwd=fixture,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn("ConnectBlock does not apply the contextual PQ discount",
                          result.stderr)

    def test_disabled_contextual_discount_bait_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="rip25-weight-disabled-") as directory:
            fixture = Path(directory)
            for entry in ROOT.iterdir():
                if entry.name == ".git":
                    continue
                if entry.name != "src":
                    (fixture / entry.name).symlink_to(
                        entry, target_is_directory=entry.is_dir()
                    )
                    continue
                source_dir = fixture / "src"
                source_dir.mkdir()
                for source_entry in entry.iterdir():
                    if source_entry.name != "validation.cpp":
                        (source_dir / source_entry.name).symlink_to(
                            source_entry, target_is_directory=source_entry.is_dir()
                        )
                        continue
                    original = source_entry.read_text(encoding="utf-8")
                    operation = "                contextualBlockWeight -= GetContextualPQWitnessDiscount(tx, view);"
                    self.assertEqual(original.count(operation), 1)
                    (source_dir / "validation.cpp").write_text(
                        original.replace(operation, "#if 0\n" + operation + "\n#endif", 1),
                        encoding="utf-8",
                    )
            result = subprocess.run(
                ["bash", str(GATE), "--structural-only"], cwd=fixture,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn("ConnectBlock does not apply the contextual PQ discount",
                          result.stderr)


if __name__ == "__main__":
    unittest.main()
