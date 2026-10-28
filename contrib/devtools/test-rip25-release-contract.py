#!/usr/bin/env python3
"""Check the RIP-25 mining rollout and 4.8.1 release documentation contract."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
NOTES = ROOT / "doc/release-notes/release-notes-4.8.1.md"
PROCESS = ROOT / "doc/release-process.md"
README = ROOT / "README.md"


def section(text, heading):
    match = re.search(r"(?im)^#{2,3} " + heading + r"[^\n]*\n", text)
    if match is None:
        raise AssertionError("Missing section: " + heading)
    end = re.search(r"(?m)^#{2,3} ", text[match.end():])
    return text[match.end():match.end() + end.start() if end else None]


def require_paragraph(test, text, description, *patterns):
    paragraphs = re.split(r"\n\s*\n", text)
    for paragraph in paragraphs:
        if all(re.search(pattern, paragraph, re.IGNORECASE | re.DOTALL)
               for pattern in patterns):
            return
    test.fail("Missing coherent guidance for " + description)


class Rip25ReleaseContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.notes = NOTES.read_text(encoding="utf-8")
        cls.process = PROCESS.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")

    def test_readme_describes_asset_custody_and_release_limits(self):
        status = section(self.readme, "4\\.8\\.1 candidate status")
        self.assertRegex(status, r"Official\s+release qualification.*pending")
        self.assertNotRegex(self.readme.lower(), r"this fork|not an official ravenproject release")
        require_paragraph(self, status, "new PQ asset custody",
                          r"bit 13", r"asset", r"owner.token",
                          r"ML-DSA", r"matching.*witness-v2",
                          r"without.*ECDSA")
        require_paragraph(self, status, "voluntary historical migration",
                          r"historical", r"classical", r"migrat",
                          r"voluntar")
        require_paragraph(self, status, "incompatible block-weight rollout",
                          r"8 to", r"12 MWU", r"16 MWU",
                          r"Core 4\.8\.0", r"reject")
        self.assertRegex(status, r"FINDING-067 remains open")
        self.assertRegex(status, r"CI packages are unsigned")
        self.assertRegex(status, r"Native Windows and macOS GUI exercise")

    def test_upstream_pr_runs_both_qualification_workflows(self):
        for name in ("build-raven.yml", "rip25-v48-final-gate.yml"):
            workflow = (ROOT / ".github/workflows" / name).read_text(encoding="utf-8")
            match = re.search(r"(?ms)^  pull_request:\n    branches:\n(.*?)(?=^  [a-z_]+:|\Z)", workflow)
            self.assertIsNotNone(match, name + " needs pull_request branches")
            targets = re.findall(r"(?m)^      - (.+)$", match.group(1))
            self.assertIn("master", targets, name + " must run for upstream master PRs")
            self.assertIn("feature/rip25-v481-upstream", workflow,
                          name + " must also run for the review branch push")
            self.assertRegex(workflow, r"\bmake check\b")
            self.assertRegex(workflow, r"(?m)^    needs: security-tests$")

        artifact_workflow = (ROOT / ".github/workflows/build-raven.yml").read_text(encoding="utf-8")
        self.assertRegex(artifact_workflow, r"(?m)^  verify-artifacts:$")
        self.assertRegex(artifact_workflow, r"(?m)^    needs: build$")
    def test_staging_documents_defer_to_current_register(self):
        for name in ("SECURITY_AUDIT_RIP25_V48.md", "PORTING_STATUS.md"):
            document = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("doc/RIP-0025-v4.8-security-remediation-register.md", document)
            self.assertIn("FINDING-067", document)
            self.assertFalse("NOT READY FOR UPSTREAM PR UPDATE" in document or
                             "FINAL CONSENSUS MERGE BLOCKER" in document,
                             name + " contains obsolete blocker status")
            self.assertFalse("\u2014" in document or "\u2013" in document,
                             name + " contains typographic dashes")

    def test_rip25_source_comments_use_plain_punctuation(self):
        for name in ("src/base58.cpp", "src/script/interpreter.cpp",
                     "src/script/ismine.cpp", "src/script/sign.cpp",
                     "src/script/standard.h"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertFalse("\u2014" in source or "\u2013" in source,
                             name + " contains typographic dashes")

    def test_mining_template_source_contract(self):
        miner = (ROOT / "src/miner.cpp").read_text(encoding="utf-8")
        validation = (ROOT / "src/validation.cpp").read_text(encoding="utf-8")
        versionbits = (ROOT / "src/versionbits.cpp").read_text(encoding="utf-8")
        mining_rpc = (ROOT / "src/rpc/mining.cpp").read_text(encoding="utf-8")
        chainparams = (ROOT / "src/chainparams.cpp").read_text(encoding="utf-8")

        self.assertRegex(miner, r"pblock->nVersion\s*=\s*ComputeBlockVersion\(")
        self.assertRegex(validation, r"state\s*==\s*THRESHOLD_LOCKED_IN\s*\|\|\s*state\s*==\s*THRESHOLD_STARTED")
        self.assertRegex(versionbits, r'"pq_hybrid"\s*,\s*/\*\.gbt_force =\*/\s*true')
        self.assertRegex(mining_rpc, r"if\s*\(!vbinfo\.gbt_force\)\s*\{[^}]*pblock->nVersion\s*&=\s*~VersionBitsMask")
        self.assertRegex(miner, r'if\s*\(chainparams\.MineBlocksOnDemand\(\)\)\s*pblock->nVersion\s*=\s*gArgs\.GetArg\("-blockversion"')

        mainnet = chainparams.split("class CMainParams", 1)[1].split("class CTestNetParams", 1)[0]
        testnet = chainparams.split("class CTestNetParams", 1)[1].split("class CRegTestParams", 1)[0]
        regtest = chainparams.split("class CRegTestParams", 1)[1]
        self.assertRegex(mainnet, r"DEPLOYMENT_PQ_HYBRID\]\.bit\s*=\s*12;")
        self.assertRegex(mainnet, r"fMineBlocksOnDemand\s*=\s*false;")
        self.assertRegex(testnet, r"fMineBlocksOnDemand\s*=\s*false;")
        self.assertRegex(regtest, r"fMineBlocksOnDemand\s*=\s*true;")

    def test_notes_explain_automatic_signaling(self):
        rollout = section(self.notes, "Mainnet signaling")
        require_paragraph(self, rollout, "automatic bit-12 mining templates",
                          r"getblocktemplate|block templates?", r"bit[ -]?12",
                          r"STARTED", r"automatic|by default", r"signal")
        require_paragraph(self, rollout, "mainnet override limits",
                          r"-blockversion", r"regtest", r"mainnet", r"override|disable")

    def test_notes_require_recorded_prestart_decision(self):
        rollout = section(self.notes, "Mainnet signaling")
        require_paragraph(self, rollout, "recorded owner go/no-go before original STARTED",
                          r"go/no-go", r"record", r"owner", r"evidence",
                          r"before", r"original", r"STARTED")

    def test_notes_require_qualified_replacement_rollout(self):
        rollout = section(self.notes, "Mainnet signaling")
        require_paragraph(self, rollout, "qualified replacement before original window",
                          r"replacement release", r"qualif", r"start", r"timeout",
                          r"install|deploy", r"miner", r"validator", r"service",
                          r"before", r"original")

    def test_process_puts_qualification_and_owner_approval_before_tag(self):
        qualification = section(self.process, "Core 4\\.8\\.1 pre-tag qualification")
        steps = re.findall(r"(?ms)^\d+\. (.*?)(?=^\d+\. |\Z)", qualification)
        self.assertGreaterEqual(len(steps), 4, "Use an ordered pre-tag sequence")

        def step_for(description, *patterns):
            matches = [index for index, step in enumerate(steps)
                       if all(re.search(pattern, step, re.IGNORECASE)
                              for pattern in patterns)]
            self.assertEqual(len(matches), 1, "Missing or repeated step: " + description)
            return matches[0]

        check = step_for("green make check on final SHA", r"final.*SHA", r"make check", r"green|pass")
        ci = step_for("paired Build Raven and Final Gate on final SHA",
                      r"Build Raven", r"Final Gate", r"same.*SHA|final.*SHA", r"green|pass")
        packages = step_for("five reviewed packages", r"five", r"artifact|package",
                            r"checksum", r"provenance")
        workflow = (ROOT / ".github/workflows/build-raven.yml").read_text(encoding="utf-8")
        targets = re.findall(r"(?m)^\s+- OS: ([a-z0-9]+)\s*$", workflow)
        self.assertEqual(len(targets), 5, "Review every Build Raven matrix target")
        for target in targets:
            self.assertRegex(steps[packages], r"\b" + re.escape(target) + r"\b")
        approval = step_for("separate owner tag approval", r"owner", r"separate|explicit",
                            r"approv", r"tag")
        tag = step_for("tag exact qualified SHA", r"create.*tag|tag.*create",
                       r"exact.*SHA|same.*SHA|final.*SHA")
        self.assertLess(check, ci)
        self.assertLess(ci, packages)
        self.assertLess(packages, approval)
        self.assertLess(approval, tag)
        self.assertLess(self.process.index("Core 4.8.1 pre-tag qualification"),
                        self.process.index("For releases other than 4.8.1"),
                        "The 4.8.1 sequence must precede generic tag instructions")


if __name__ == "__main__":
    unittest.main()
