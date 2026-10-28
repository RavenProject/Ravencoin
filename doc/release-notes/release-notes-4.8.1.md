# Raven Core 4.8.1 release notes

The release tag identifies the source commit. Published release metadata
provides the final artifact checksums. Verify the tag commit and the checksum
of the downloaded package before installation.

## Upgrade before bit 12 activates

RIP-25 adds ML-DSA-44 signatures for native RVN witness-v2 outputs. The
same activation changes the block limit from 8 to 12 million weight units
(MWU) and applies a contextual discount to PQ witness data. Core 4.8.0
can reject blocks accepted by upgraded nodes after activation. This is an
incompatible consensus upgrade. Miners, pools, full nodes, exchanges,
custodians, explorers, and other validating services must install a
qualified 4.8.1 release before bit 12 becomes ACTIVE. A miner's signal
does not upgrade any other validator.

Back up wallets, stop the old node cleanly, install the release artifact
for the platform, verify its published checksum, and allow the upgraded
node to synchronize. Before relying on it for validation, confirm the
running version and inspect `getblockchaininfo.bip9_softforks.pq_hybrid`.
Do not downgrade a validator to Core 4.8.0 after bit 12 is ACTIVE: it may
follow a different chain once a newly allowed block appears.

## Mainnet signaling and block limits

| Rule | Value |
|------|-------|
| Bit 12 STARTED threshold | Median time past reaches 2027-07-01 00:00 UTC (1814400000) at a window boundary |
| Timeout | 2029-01-01 00:00 UTC (1861920000), 18 months after start |
| Signaling threshold | At least 1714 of 2016 blocks in one STARTED window |
| Lock-in | One complete LOCKED_IN window follows a successful signaling window |
| Activation | ACTIVE begins at the following window boundary, at height `H12` |
| Block limit at `H12` | 12 MWU, with the contextual PQ witness discount |
| PQ sigop cost at `H12` | 2 units per active native or P2SH-wrapped witness-v2 input; 80,000-unit block cap unchanged |
| Later block limit | 16 MWU at `H12 + 525600` blocks, one nominal year at the 60-second target spacing |

The start timestamp is not the date when the new rules take effect. BIP9
uses chain median time past and full 2016-block windows. If the timeout
is reached before lock-in, bit 12 fails and these rules do not activate
through this deployment. The 16 MWU phase is height-based, not tied to
a calendar date. There is no automatic return to Core 4.8.0 validation
rules after activation.

The PQ sigop cost of two is a consensus change from the initial RIP-25
value of one. It is fixed across platforms and begins with bit-12 ACTIVE.
Both miner templates and block validation count native and P2SH-wrapped
witness-v2 inputs at this cost. The change follows portable-backend
verification measurements and exact-limit regression tests.

On mainnet, upgraded 4.8.1 `getblocktemplate` mining templates
automatically set bit 12 and signal during STARTED and LOCKED_IN.
`CreateNewBlock` uses `ComputeBlockVersion`, and `pq_hybrid` is a forced
getblocktemplate version bit. A pool can therefore signal as soon as its
upgraded node reaches STARTED, even if other validators and services are
still running Core 4.8.0. The `-blockversion` override is regtest-only;
it cannot disable bit 12 in mainnet 4.8.1 templates. Pools should check
the returned template version and coordinate their upgrade with the
validators and services that depend on their blocks.

Testnet differs from this mainnet schedule. Its native RIP-25 rule is
force-enabled from genesis by `nPQHybridEnabled=true`. At the reviewed
2026-10-15 testnet tip, `getblocktemplate` therefore advertises the later
16 MWU limit, even while the raw bit-12 BIP9 state reads STARTED. This
does not imply that mainnet has activated bit 12. The testnet override
cannot demonstrate the public 8-to-12-MWU BIP9 transition; controlled
regtest boundary tests cover that transition. Testnet miners and node
operators should account for the already active PQ and block-weight
rules when comparing with Core 4.8.0.

Before the original bit-12 STARTED window, the release owner must record
an evidence-based go/no-go decision. The evidence includes a green
`make check` on the final 4.8.1 SHA, paired green Build Raven and Final
Gate runs on that SHA, checksum and provenance review of all five
packages, and confirmed readiness of miners, pools, validators, and
dependent services. The clean candidate has already replayed the
available public testnet history from genesis, and both old and new
binaries have been tested on both sides of the 8 MWU boundary. That
history does not replace the final qualification or operator readiness
decision. Operators must have time to upgrade before ACTIVE.

If the decision is no-go or readiness is absent, a replacement release
must revise both the start and timeout dates and pass the full release
qualification again. The replacement release with revised parameters
must be installed across miners, pools, validators, and dependent
services before the original STARTED window begins. Merely preparing
revised parameters, or relying on `-blockversion` on mainnet, does not
prevent an already deployed 4.8.1 miner from signaling on the original
schedule.

## Migration to PQ custody

Native RVN in historical classical outputs remains spendable with the
existing signature. Owners migrate voluntarily by generating a witness-v2
PQ address with `getnewpqaddress` and sending RVN to it. Its later spend
uses ML-DSA-44 without an ECDSA signature. Existing classical outputs
are not converted by activation.

The separate bit-13 mainnet deployment is scheduled to start no earlier
than 2029-01-01 00:00 UTC by median time past, with a 1714/2016 threshold
and 2030-07-01 timeout. It cannot enforce its PQ-only asset rule until
bit 12 and the transfer-script deployment on bit 8 are ACTIVE. Once all
three deployments are ACTIVE, newly created asset and owner-token outputs
use the PQ asset format: spending requires a matching native RVN
witness-v2 input signed with ML-DSA-44 and no classical asset signature.
Historical asset and owner-token outputs retain their classical spending
condition until their owners move them to new protected outputs. Activation
does not retroactively protect them. If bit 12 fails or activates too late,
the bit-13 window must be reviewed before it starts.

There is no automatic network-wide point at which only PQ keys can spend
all existing holdings. Each owner reaches PQ-only custody after moving every
historical RVN, asset, and owner-token UTXO they hold. Unmigrated historical
outputs remain spendable with their original classical keys.

For the asset format, wallet recovery routes, and current limitations in
automatic anchor refresh and raw transaction funding, see
[RIP-25 PQ Asset Extension Design Note](../RIP-0025-PQ-Assets-Followup.md).

## Known asset limitations

An asset PQ anchor is native RVN at the protected output's matching PQ
address. Some asset spends and ordinary RVN payments consume the last
anchor without creating a replacement. Exact-fee, dust, multiple-program,
and explicit native-change cases need the owner to fund the matching PQ
address again before the next protected asset spend. The asset output
remains unspent; consensus rejects a spend without its anchor.

`fundrawtransaction` does not discover or select anchors for protected
asset inputs. Raw transaction callers must supply matching funded PQ
inputs and check the final transaction before broadcast.

## Reward distribution recovery after restart

New reward requests bind the originating wallet and reload after a node
restart. Keep that wallet loaded and unlocked, allow the node to synchronize,
and check the request after the next connected block. Query the original
asset, snapshot height, distribution asset, gross amount, and optional
exception-address string with `getdistributestatus`. The exact exception
string is part of the request identity. `-debug=rewards` provides startup
and retry diagnostics.

Status 6 means a payout transaction is uncertain; status 10 means the
snapshot changed. Neither status resumes automatically. Requests created
before wallet binding also cannot be assigned to a wallet or retried
automatically. For any of these cases, preserve the wallet and reward
database, compare `getblockhash` and `getsnapshot` with the original
obligation, and reconcile every possible prior payout in the originating
wallet with `listtransactions` and `gettransaction`. Where available,
`getrawtransaction` can confirm a transaction in the mempool or an indexed
chain. Absence from an unindexed node or one wallet alone is not proof of
nonpayment. Arrange a manually reconciled payout only after establishing
what remains owed. Do not edit or erase reward database records to force
a retry.

The numeric status alone does not prove that a reward was paid or unpaid:
successful batches can leave the original status unchanged. Reward RPCs do
not list distribution records or expose stored batch transaction IDs, and
the same request parameters cannot be submitted again as a duplicate.
Retain the originating wallet, logs, and request details for reconciliation.

## Node-only GUI

The Qt GUI now starts with `-disablewallet` without accessing the
wallet-only currency selector. Its Options dialog can also submit
the Icons only setting without accessing absent wallet toolbar controls.
Both crashes were inherited from Core 4.8.0. The Linux release build
runs wallet-disabled GUI startup, Options interaction, and RPC
shutdown checks.

Native Windows and macOS GUI exercise is assigned to community testers
after publication. The cross-platform CI builds and package checks do
not establish that either native GUI works. Community reports should
identify the 4.8.1 version, package checksum, operating system version,
wallet setup, steps, expected result, observed result, and relevant
debug log. The requested checks cover first launch, wallet creation and
recovery, asset and owner-token send and receive, and clean shutdown.
Bit-13 asset activation behavior needs a controlled test environment
because the public deployment has not activated.

## Qualification evidence and limits

The available testnet active chain was replayed from genesis by a clean
4.8.1 candidate with `-assumevalid=0`, then synchronized with public
peers. A later unpruned candidate at `d5dbc0d07` scanned the active chain
through height 2,562,352, covering 2,562,353 blocks and 2,782,751
transactions with 1,285 verified checkpoints. The exact snapshots and
limits are in the remediation register and private audit evidence.
The scanned chain contained no native witness-v2 outputs or spends, so
the replay checks historical compatibility rather than an on-chain PQ
signature. It does not test unknown side branches or future bit-13
activation.
The scoped activation and side-branch defects FINDING-104 through
FINDING-107 are fixed after H-2 through H+2 regressions, connected
alternate branches, rollback and restart checks, and a green exact-SHA
Build Raven and Final Gate pair on `0c33cc92c`. All five packages from
that run passed downloaded checksum and provenance review. The
pinned xproto and libXau source-availability fixes passed both
workflows on `35fa04a40`; all five downloaded packages passed
provenance and checksum review. The owner accepted a documented
cause-not-reconstructible disposition for two historical intermittent
wallet recovery test failures (FINDING-112); a renewed failure blocks
release, and `make check` remains mandatory on the final SHA.
The wallet-disabled Qt corrections (FINDING-177 and FINDING-178)
require passing unmodified Linux package smokes on the final SHA.
The later installer and DMG binding fixes (FINDING-193 and FINDING-194)
passed Build Raven `39939214218` and Final Gate `39939213883` on
`b1a67830c`. All five release targets and Build Raven's aggregate
downloaded-bundle verification passed. A separate check of all five
downloaded bundles confirmed their common source archive, the Windows
installer program identity, and all 23 entries of the macOS DMG app.
FINDING-020, FINDING-193, and FINDING-194 are fixed for their documented
scope. A later pre-tag candidate at `97eb1bfb5` passed Build Raven
`40069848322` and Final Gate `40069850388` on that exact source SHA.
Mandatory `make check`, the required functional security gate, all five
release targets, and pinned-Qt Linux checks passed. Independent review of
all five downloaded bundles accepted their checksums, provenance, common
source archive, Windows installer program code, and 23 macOS DMG app
entries. These runs precede this documentation change and are prior
evidence, not qualification of a later tag commit.

A later exact-source checkpoint at `83b2f1709` passed Build Raven
`40119099375` and RIP-25 v4.8 Final Gate `40119101545`. Both required
security jobs passed `make check`, seven source mutations, and all 38
required functional tests. All five release targets, the aggregate
downloaded-bundle verifier, and all seven Final Gate builds passed.
Independent review of the five bundles confirmed checksums, source
provenance, target formats, Windows installer code, and all 23 macOS DMG
app entries. The downloaded Linux package passed node-only Qt startup
and Options interaction after extraction. The common source archive
SHA-256 was
`bfe7dcd5f782de2fe9ee6cf45b7c690a12fed99b4b7aa13423700732ccc94552`.
The packages from this checkpoint are unsigned. This later README and
release-notes update also changes the source SHA, so its exact tag commit
requires fresh qualification.

The later official PR #1330 checkpoint `c63cad804` includes the KAWPOW
checkpoint and concurrent epoch-context corrections (FINDING-195 and
FINDING-196). The forged-work shortcut report and regression suggestions
from [@skareemullah8-a11y in PR #1329](https://github.com/RavenProject/Ravencoin/pull/1329)
prompted the FINDING-195 review. The correction preserves the inherited
pre-activation height rule while requiring full work outside exact indexed
checkpoint ancestry. Its local `make check` passed 535 unit cases. Build Raven
`40327113350` and Final Gate `40327113034` passed on that exact SHA,
including their security jobs, all five release targets, the aggregate
package verifier, and all seven Final Gate variants. A separate download
review of all five bundles passed source, provenance, manifest,
target-format, Windows installer, and complete macOS app checks. Their
common source archive SHA-256 is
`77665286b6a2a5c0a100ae5342b266d7139e0f3160633ca4a0404d2807b3e6b1`.
The extracted Linux package passed wallet-disabled Qt startup and
Options interaction. The unpruned public testnet node reopened its
existing chain with this binary at height 2,569,434, accepted later
blocks, and passed `verifychain 4 2016`. The existing RC1 package is
older and does not contain the KAWPOW corrections. A subsequent
FINDING-197 mitigation gives KAWPOW verification a local context-memory
budget, default 256 MiB per context, configurable with
`-maxkawpowcontextmb`. A budget or allocation failure is reported as a
local error instead of invalid proof of work, without a peer penalty or
persistent failed header. A valid historical header above that budget
needs a higher operator setting. Peer messages are verified on one
thread, so peer input can hold at most one context within the budget.
A later correction checks the already required
KAWPOW difficulty before building a context on the header path; its
red-then-green regression and local 539-case `make check` passed. A
subsequent complete-block regression found a second path that could
still build a context first. The `1fd1b4628` fix checks known-parent
difficulty before full block hashing. The constrained-budget regression
changed from a local resource error to `bad-diffbits`, and local
`make check` passed 540 cases. The invariant gate passed under Python
3.10 with all 14 required functional tests. Build Raven
`40455702783` passed all five release targets on the earlier
`30349977b` upstream head, but Final Gate `40455702781` failed the
Windows consensus-library link. FINDING-199 traces this to the
node-only budget option referenced from the standalone library.
Commit `c4d1b20ad` keeps that option in node builds and uses the
256 MiB default in the library. The local library link, 17 focused
KAWPOW tests, and 540-case `make check` passed. On official PR head
`aa5b128c5`, Build Raven `40499782364` and Final Gate `40499782271`
both passed on the same SHA, including the Windows consensus-library
link, all five release builds, and required security tests. FINDING-199
is fixed on that checkpoint. Its five packages have not yet received an
independent download review. Later commits check each KAWPOW claim
against its target before building a context and remember completed
proof results, so replaying a header cannot force repeated context
work. Startup now rejects an invalid `-maxkawpowcontextmb` value, and
ELF builds reject unresolved symbols in the shared consensus library.
A checkpoint-anchored
scan of the canonical mainnet block files found that every KAWPOW header
up to the height gate declares its actual height, so the largest
canonical context needs 91 MiB of the 256 MiB default. The unpruned
public testnet node running `aa5b128c5` passed `verifychain 4 2016`
twice around a restart and accepted three new blocks through height
2,570,490. A host restart interrupted the separate mainnet replay at
height 1,453,567; the saved chainstate was retained and the replay
restarted. The required pre-RC manpage and English translation refresh
changes the candidate SHA after `aa5b128c5`. The new check on
unresolved library symbols then found that 32-bit ARM builds left
`sha256_transform` undefined in the shared consensus library; commit
`c86a18d96` compiles the existing C implementation on that target. On
official PR head `a756574d4`, which contains the refresh and these
corrections, Build Raven `40599833317` and Final Gate `40599833429`
both passed, including all five release builds, required security
tests, and every Final Gate variant. FINDING-197 and FINDING-199 are
fixed on that checkpoint. Its five packages have not yet received an
independent download review. The unsigned `v4.8.1-rc2` pre-release was
built from the next PR head `e3356f3f1`, which changes only
documentation. Build Raven `40609746431` and Final Gate `40609746325`
passed on that exact SHA, its five packages passed an independent
download review against the source checkout, and the packaged Linux Qt
application passed the wallet-disabled smokes. The RC1 Linux ARM32
package ships a shared consensus library that fails to load because
`sha256_transform` is unresolved; RC2 corrects it, and the RC1 node,
Qt, and CLI executables do not use that library. None of these
checkpoints authorizes bit-12 activation or a final stable release.

The release process requires `make check`, paired green Build Raven and
Final Gate runs, pinned-Qt Linux checks, and independent review of all five
packages at the exact tag commit. Record those run IDs, the immutable
source SHA, and final package checksums in the release metadata. Confirm
miner, pool, validator, and dependent-service readiness before the bit-12
go/no-go decision. Native Windows and macOS GUI exercise is assigned to
post-publication community testing, with the residual uncertainty stated
above. Tag creation and publication each require a separate owner decision.
