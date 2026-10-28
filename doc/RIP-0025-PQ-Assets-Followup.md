# RIP-25 PQ Asset Extension Design Note

Status: bit 13 PQ-only asset candidate with approved mainnet and testnet
BIP9 windows in the 4.8.1 working branch. Bit 13 is not active on either
public network. Public activation and release qualification remain blocked
by the native bit-12 deployment plan, testnet history review, and final
same-SHA release evidence.

## 1. Current Scope

Until bit 13 activates, no Ravencoin asset class gains ML-DSA-44 ownership
from this candidate. The 4.8.1 working branch schedules a separate,
dependent bit-13 rule for new asset and owner-token outputs. The PQ-only
implementation passed a same-SHA CI pair on `9151946ad`, but it is not a
deployed public-network protection or a release-qualified feature.

The historical spendable asset envelope is:

```
OP_DUP OP_HASH160 <20-byte-key-id> OP_EQUALVERIFY OP_CHECKSIG
OP_RVN_ASSET <serialized-asset-data> OP_DROP
```

`CScript::IsAssetScript` recognizes `OP_RVN_ASSET` only at byte offset 25.
Historical outputs use the P2PKH prefix and a secp256k1 `CKeyID`.
The candidate PQ-only prefix in Section 20 preserves that offset while
replacing classical authorization with a program-derived asset identifier.

The limitation applies to all current asset classes:

| Class | Current authorization | RIP-25 protection |
|---|---|---|
| Normal and sub-assets | Legacy P2PKH asset output | None |
| Owner token `ASSET!` | Legacy P2PKH asset output | None |
| Reissuable asset | Legacy owner token plus legacy destination | None |
| Unique asset | Legacy parent owner token plus legacy destination | None |
| Restricted asset `$ASSET` | Legacy root owner token `ASSET!` | None |
| Qualifier and sub-qualifier | Legacy qualifier ownership and address records | None |

The notation `$ASSET!` is not the controlling owner token for a restricted asset. The restricted asset `$ASSET` is administered through the root owner token `ASSET!`.

## 2. Security Consequence

A cryptographically relevant quantum computer that recovers a secp256k1 private key can steal an asset UTXO even if the same wallet also holds native RVN at RIP-25 addresses. Theft of `ASSET!` is especially serious because it can transfer administrative control and authorize reissuance when the asset is reissuable. Unique assets can be transferred irreversibly. Restricted and qualifier administration remains exposed through its legacy authorization outputs.

Wallet and RPC messages must not imply that a native-RVN PQ address alone
protects asset owner or administrator tokens. The asset builder rejects
that bare address and, at active bit 13, requires a canonical
`assetID|PQ` descriptor. The asset identifier has no classical private key.

## 3. Why the Proposed Concatenation Is Unsafe

One conceptual form suggested for future analysis is:

```
OP_2 <32-byte-hash> OP_RVN_ASSET <serialized-asset-data> OP_DROP
```

It must not be adopted directly.

First, a native witness program must be the entire script. Adding any suffix makes `CScript::IsWitnessProgram` return false. Second, the current asset parser requires the asset opcode at the P2PKH-specific byte offset. Third, `OP_RVN_ASSET` is a no-op in ordinary script execution. Merely teaching the asset parser to recognize the proposed concatenation would not invoke witness-v2 verification and could create an anyone-can-spend asset output.

Current nodes reject the form because the asset opcode appears in the wrong location. Changing it from invalid to valid expands the set of valid transactions. The deployment and legacy-node consequences therefore require a complete fork analysis and cannot be assumed to form a conventional soft fork.

## 4. Required Protocol Design

A follow-up proposal should define a canonical versioned asset envelope that separates:

1. the ownership or spending-condition version;
2. the asset operation type;
3. the serialized asset payload.

The spending condition must unambiguously dispatch to ML-DSA-44 witness-v2 verification. It must not depend on a parser side effect or on executing `OP_RVN_ASSET` as an opcode. The proposal must specify exact serialization lengths, canonical pushes, rejection of trailing data, and how unknown versions behave.

At minimum, the consensus design must cover:

- normal issue and transfer operations;
- owner-token creation and transfer;
- reissue authorization;
- unique-asset creation and transfer;
- restricted-asset issue, transfer, reissue, freeze, and verifier behavior;
- qualifier and sub-qualifier issue, transfer, tag, and untag behavior;
- asset input and output overflow checks from Ravencoin Core 4.8.0;
- fixed RIP-25 SIGHASH policy;
- ML-DSA network context and cross-network replay resistance;
- PQ sigop-equivalent accounting;
- contextual witness discount and block-weight accounting;
- activation boundaries and reorgs.

## 5. Recognition and Script Ambiguity

The design must replace fixed-offset assumptions with a parser that accepts exactly the intended legacy and activated PQ forms. It must prove that:

- no legacy script changes meaning;
- no script is accepted by both legacy and PQ parsers with different destinations or asset payloads;
- no malformed PQ form falls through to ordinary script execution;
- unknown ownership versions fail closed after activation;
- `IsAssetScript`, `Solver`, destination extraction, and transaction classifiers agree;
- null asset data cannot be confused with spendable asset ownership.

Parser tests need canonical and non-canonical encodings, shortened and extended programs, misplaced opcodes, extra stack elements, trailing bytes, and payload length boundaries.

## 6. Legacy-Node and Activation Analysis

The proposal must state how an unupgraded node evaluates each new output and spend. If an old node treats the new form as spendable without ML-DSA verification, activation must ensure upgraded miners and validators reject unauthorized spends. If old nodes reject the new output form, deployment has hard-fork characteristics and must be treated accordingly.

The activation design must include:

- a dedicated deployment or an explicitly justified reuse of a deployment state;
- pre-activation policy and consensus behavior;
- `DEFINED`, `STARTED`, `LOCKED_IN`, and `ACTIVE` transitions;
- activation-height minus two through activation-height plus two vectors;
- invalidate and reconsider behavior;
- reorgs across `LOCKED_IN` and `ACTIVE`;
- versionbits cache invalidation and restart behavior;
- miner-template and validator equivalence.

No asset extension should be coupled silently to an already deployed RIP-25 bit.

## 7. Wallet, RPC, and Address Encoding

The wallet uses a distinct composite PQ asset destination rather than
reusing a native-RVN address without a defined asset meaning.

The working branch defines a wallet-facing, network-bound `assetID|PQ`
descriptor. `assetID` encodes `HASH160(P)` as a legacy-sized address field;
`P` is the native witness-v2 program. Its decoder requires exactly one
separator, the expected destination types, the program-derived identifier,
and canonical re-encoding on the selected network. The identifier has no
classical private key and is not a native RVN payment address. At active
bit 13, the wallet uses the descriptor for root, subasset, unique,
restricted, qualifier, reissue, transfer, and administrative outputs. The
raw asset constructor accepts it for corresponding forms.
`getnewpqassetaddress` generates an ML-DSA key only after the dependent
rule is active, persists the descriptor, and `listpqassetaddresses`
retrieves owned descriptors after reload and validated key-only salvage.
Restricted-asset verifier and qualifier indexes use the program-derived
asset identifier. The wallet selects a matching funded native
PQ anchor and returns protected asset change or owner authority. For one
consumed program, ordinary positive native change can refresh the anchor at
the same PQ address. Exact-fee, dust, multiple-program, or explicit native
change can omit that refresh. Ordinary RVN payments can also consume an
anchor without spending the asset. In either case the holder must send native
RVN to the affected PQ address before another protected spend. Raw callers
must provide matching funded native anchor inputs themselves. The local
full-chain suite covers representative operations for every named class,
nontrivial restricted verification, encrypted backup recovery, and GUI
compilation; it does not certify public deployment.

Required wallet behavior includes:

- PQ asset ownership detection and balance attribution;
- ML-DSA signing for every asset spend path;
- PQ asset change selection;
- owner-token and reissue authorization;
- coin control and fee estimation for large witnesses;
- encrypted key persistence and recovery;
- watch-only and multisig policy, if supported;
- import, export, backup, restore, rescan, and salvage behavior;
- explicit errors on unsupported mixed legacy and PQ constructions.

All high-level and raw-transaction RPCs must construct only canonical activated forms. RPC acceptance must not be treated as proof of consensus validity.

## 8. Asset Index and Database Compatibility

Asset indexes use 20-byte identifiers. In the PQ-only candidate that
identifier is `HASH160(P)`, rather than an independently selected
classical key identifier. A bare identifier is accounting data, not the complete PQ
destination: custody and spending still require the committed 32-byte
program and a matching funded anchor. The historical two-program collision
described in FINDING-153 belonged to the superseded dual-signature form.

It must specify:

- address-index key format;
- asset cache and database serialization versions;
- reindex and downgrade behavior;
- explorer and RPC address rendering;
- restricted and qualifier database keys;
- mempool overlay behavior;
- database rebuild after chainstate recovery.

Database readers must reject malformed or unknown key versions without interpreting them as legacy records.

## 9. Owner-Token Migration

Existing `ASSET!` outputs cannot become quantum-resistant merely because RIP-25 activates. The current candidate permits a normal legacy-authorized transfer of a historical asset or owner-token UTXO into a new tagged output. The old input remains under its creation-height rules; the new output must meet the active bit-13 rule. A migration mechanism must prove current authorization while moving control to a PQ asset condition.

The proposal must address:

- migration of owner tokens before a quantum emergency;
- reissuable assets whose owner token is lost or stolen;
- unique and restricted asset control;
- partial wallet migration and mixed legacy/PQ holdings;
- replay of migration transactions across networks;
- rescan and restore discovery;
- whether migration can be reversed;
- emergency behavior without creating a confiscation or inflation path.

There is no safe automatic migration after the classical private key has been compromised.

## 10. Mempool and Resource Policy

Policy must bound attacker-controlled input before expensive ML-DSA verification. It must apply exact public-key and signature sizes, exact witness stack shape, canonical asset serialization, transaction-size limits, ancestor and descendant policy, orphan limits, and explicit PQ sigop-equivalent accounting.

Consensus and policy limits must remain distinct. Policy may reject more than consensus, but miners and validators must calculate the same activated consensus cost for a block.

## 11. Required Validation Matrix

The follow-up implementation should not be proposed for activation without:

- consensus `tx_valid` and `tx_invalid` vectors for every asset class;
- correct and incorrect ML-DSA signature vectors;
- script ambiguity and parser differential tests;
- cross-network replay tests;
- activation and reorg tests;
- miner-template versus block-validation tests;
- asset input/output overflow regression tests;
- wallet encrypted-backup recovery tests;
- address-index and reindex tests;
- P2P, orphan, mempool, and block resource tests;
- real-verifier fuzzing under ASan and UBSan;
- cross-platform deterministic validation builds.

## 12. Current Decision

The native-RVN bit-12 rules retain their 8 to 12 to 16 MWU plan. The
4.8.1 working branch implements a dependent PQ-only bit-13 asset rule and
sets the approved public BIP9 windows. Wallet asset builders still reject
a bare native witness-v2 destination: an asset requires its canonical
`assetID|PQ` descriptor. Unit, functional, wallet-recovery, and pinned-Qt
checks passed with all five Build Raven artifacts and a green Final Gate
on candidate SHA `9151946ad`. A second independent audit froze and tracked
its findings. Release qualification still needs the bit-12 network upgrade
plan, historical testnet review, remaining finding closure, and final-SHA
artifacts. The dates in code do not authorize publishing or mining under
the new rule.

## 13. Historical Dual-Signature Compatibility Research

This section records earlier compatibility research. Its P2PKH plus PQ
tail was superseded by the PQ-only format in Section 20. It is not the
current ownership rule or a public-network protection claim.

Directly appending a witness-v2 program or witness to a legacy P2PKH asset
output does not work as a compatible soft fork. The complete script is not a
native witness program, while existing witness validation rejects nonempty
witness data for a non-witness prevout as unexpected. Merely recognizing a
different `OP_RVN_ASSET` offset would not solve spending, and a bare
`OP_2 <hash> OP_RVN_ASSET` output is anyone-can-spend to older script engines.

A first suffix candidate was falsified by a unit test: an opaque
`OP_2 <32-byte program>` after `OP_DROP` is included in the old transfer
deserializer stream. The old parser mistakes it for an asset message and
rejects the output. It must not be implemented.

A second candidate encodes the program in the optional transfer-message slot
using a distinct raw marker byte, canonical CompactSize 32, and the program.
The experimental marker `0x50` is not a protocol assignment. Although the old
parser accepts this form, it consumes the genuine message field, can emit
unwanted owner-token messages, and does not cover issuance or reissuance well.
It is a compatibility probe, not the preferred extension.

A third candidate replaces the final `OP_DROP` byte of a canonical P2PKH asset
output with exactly 32 raw bytes containing the PQ program. The old script
engine treats all bytes after `OP_RVN_ASSET` as opaque. Old transfer and reissue
deserializers ignore exactly 32 residual bytes when no optional message or
metadata hash exists. When a genuine transfer message exists, the protected
form must serialize an explicit eight-byte expiry (zero if absent) before the
32-byte tail; the old parser then retains the original message and expiry and
ignores the tail. Initial issuance and owner-token parsers also ignore the
tail. Local unit tests prove parsing and existing transaction/asset checks for
representative transfer, issue, owner, and reissue scripts, including normal,
unique, restricted, and qualifier transfer names. This is evidence of parser
compatibility, not evidence that the proposed new consensus rule is safe.

A canonical helper recognizes the narrow envelope and rejects
short/long tails, nonminimal pushes, malformed payloads, and transfer messages
without an explicit expiry field. The earlier dual-signature candidate used
this helper only after its effective activation height. An old-valid
script ending in `OP_DROP` plus 31 arbitrary bytes
is byte-identical to a candidate with a 32-byte program beginning `OP_DROP`.
The helper necessarily classifies it as a candidate. The spend rule therefore
checks the creating UTXO's height against the asset extension's
activation height. Applying the rule only at spending height would retroactively
lock historical coins and could split nodes after reorg or reindex.

The earlier proposal would not put an unexpected witness on the asset input.
Instead, the spending transaction would also consume a native RIP-25
witness-v2 UTXO whose program equals the program committed in the asset output.
Its fixed `SIGHASH_ALL` ML-DSA signature commits to every asset input and
output in the same transaction. A quantum attacker with only the P2PKH
private key could not create the required PQ input.

This dual-signature construction has been superseded. Section 20 specifies
the current PQ-only output: its asset input has an empty scriptSig and
does not require a classical signature. The public bit-13 windows are
scheduled in code, but network activation and release remain gated as
described in Section 16 and the security remediation register.

## 14. Contextual Enforcement Map (PQ-Only Candidate)

The candidate's UTXO-aware hook runs in `ConnectBlock` before `UpdateCoins`.
`Consensus::CheckTxPQAssets` requires canonical tagged asset outputs at the
effective height and a matching native witness-v2 input for each distinct
program of a protected asset input. A coin created before the effective
height remains under historical rules, even if its script looks tagged. A
mempool parent has `MEMPOOL_HEIGHT` and is treated as newly created. The same
check runs at mempool admission and after a reorg, with a per-transaction coin
cache to avoid retaining every mempool input in memory.

The block script flags and effective asset height come from the candidate
block's `pindex->pprev`, while mempool policy uses `chainActive.Tip()`.
Effective asset enforcement requires bit 13, the historical bit 8 transfer
parser, and native RIP-25 bit 12 all to be active. Its first enforcement
height is the maximum of their activation heights. This prevents a node
validating a side branch from borrowing the global active tip's state.
The PQ-only prefix remains historically valid before activation, so the
input rule uses its creating height rather than only its spending height.
The wallet does not label an early lookalike as PQ custody, and mempool
policy rejects its premature creation. Reorgs across activation revalidate
affected mempool transactions
through staged removal so asset reissue reservations and compact-block
indexes stay consistent. Reusing the existing RIP-25 bit 12 would be unsafe
because it
would make the newly upgraded nodes enforce a rule older RIP-25 nodes do not.

`CheckTxPQAssets` runs outside the script cache. When it finds an asset input
created after activation, `ConnectBlock` requires the matching PQ anchor
and an empty asset scriptSig even under assumevalid; ECDSA is not required.
A native witness-v2 input is verified
under the contextual RIP-25 flag, not by treating the asset envelope itself
as a witness program. `TestBlockValidity` is the miner's final template
check, but template selection and package policy still need boundary tests.

The candidate permits one valid native witness-v2 anchor input to authorize
several protected asset inputs with the identical program. A different
program requires a different anchor input. The wallet selects a funded
matching anchor and signs it. A newly received asset output does not itself
fund its matching native anchor: the recipient must send RVN to the
descriptor's PQ address. A single-program spend can refresh that anchor
from ordinary native change, but the edge cases listed in Section 17 still
require manual top-up. Bit 13 has future public windows but is not active.

## 15. 4.8.1 Integration Candidate and Release Gate

The 4.8.1 integration request expands RIP-25 to assets. The native-RVN
8 to 12 to 16 MWU plan remains a separate bit-12 deployment concern. This
section records the scheduled bit-13 PQ-only candidate, not a rule already
in force or a release-qualified feature.

Let `H` be the first candidate-block height at which bit 13, bit 8, and
native RIP-25 bit 12 are all active. The asset rule requires both:

1. Every spendable asset output created at height `h >= H`, including an
   owner token, has the canonical 25-byte PQ-only prefix and one bound
   32-byte program. Null asset metadata outputs retain their existing
   rules. An asset output without this form is invalid at `h >= H`.
2. A transaction spending an asset UTXO created at `h >= H` must also spend a
   native RIP-25 witness-v2 UTXO whose 32-byte program equals that asset
   output's committed program. One valid anchor input may authorize several
   tagged asset inputs only when their programs match. The native witness-v2
   fixed `SIGHASH_ALL` signature then binds every input and output of the
   transaction. The asset input has an empty scriptSig and needs no ECDSA.

The candidate block's previous index, not the global active tip, must select
the BIP9 state. Mempool admission uses the current tip and treats an unconfirmed
parent as created after activation. The check must run outside script-cache and
assumevalid shortcuts. Native witness-v2 signature verification must still run
under assumevalid when the asset anchor is needed. A transaction may not remove
the PQ condition by sending its asset to a legacy-only output after activation.

The creation-height rule is essential. A pre-activation PQ-only lookalike
was valid under historical script rules and may be spent without an anchor.
Requiring one retroactively would lock an old-valid coin. The wallet must
not advertise that historical output as PQ custody. Mempool policy rejects
new lookalikes before activation and removes them after a boundary reorg.

This construction still does not retroactively secure existing owner tokens.
Their classical key holder must authorize a migration into a newly tagged
output before that classical key is compromised. A quantum attacker who obtains
the old key can race that migration. Software cannot distinguish the rightful
holder from the attacker using the old signature alone.

Core 4.8.0 accepted a 932-block regtest chain containing 66 PQ-only asset
outputs and 35 later spends from the candidate, including an asset spend
with empty scriptSig. This checks the asset format at ordinary block sizes;
Core 4.8.0 does not enforce ML-DSA. FINDING-160 separately proves a split
at the native 8 MWU boundary and requires a coordinated incompatible-rule
upgrade plan. The bit-12 mainnet dates are now fixed at 2027-07-01 and
2029-01-01 UTC. The available testnet active chain was replayed from
genesis with `-assumevalid=0` and checked through the recorded
snapshots. Final-SHA artifacts and `make check`, the 8 MWU boundary
proof on that SHA, and operator readiness remain release gates. The
historical unexplained CI failure has an accepted
disposition, but any new failure still requires investigation. Reusing
bit 12 for assets would change the meaning of its
earlier deployment and is excluded.

## 16. Proposed Operator Activation Sequence

This is the operator sequence for the approved bit-13 windows, not an
authorization to publish the release or activate a public chain. Testnet
starts on 2027-04-01 and times out on 2028-10-01 at 1310/2016;
mainnet starts on 2029-01-01 and times out on 2030-07-01 at 1714/2016.
The mainnet bit-12 window starts on 2027-07-01 and times out on
2029-01-01 at 1714/2016. The approved FINDING-160 operator plan,
testnet-history review, and final release qualification remain gates
before public deployment. If bit 12 fails or activates too late for a
safe asset rollout, review the bit-13 window before it starts.

1. Deploy and review the native RIP-25 bit-12 implementation. Its BIP9
   transition enables witness-v2 ML-DSA-44 script validation, the 12 MWU
   first block-weight phase, and the contextual 8x witness discount.
   The 16 MWU phase follows the specified later height. Existing RVN
   outputs remain classically spendable until their owners transfer them
   to native witness-v2 addresses.
2. Keep the historical transfer-script deployment on bit 8 effective.
   The proposed bit-13 asset rule also requires bit 12; its effective
   height `H` is the maximum of the three activation heights. Tests must
   cover the `H-2` through `H+2` boundary, restart, invalidate/reconsider,
   and reorgs across BIP9 `LOCKED_IN` and `ACTIVE`.
3. Before public deployment, verify the full shared testnet history and
   bit-12 prerequisite, execute the incompatible block-rule upgrade plan,
   obtain same-SHA final release artifacts, and communicate migration.
   Updating software without a network deployment does not protect assets.
4. At or after `H`, newly created spendable asset outputs of every class,
   including `ASSET!` owner authority, must carry the canonical PQ program.
   A spend of such an output has an empty asset scriptSig and requires a
   native witness-v2 input with the matching program and a valid
   network-context ML-DSA-44 signature. No ECDSA key authorizes the new
   output. The asset itself is not a witness program.
5. Holders of older asset and owner-token outputs must explicitly transfer
   them into tagged outputs with their existing classical authorization.
   They should obtain a canonical `assetID|PQ` descriptor and fund its PQ
   native address with RVN for the first protected spend. There is no
   automatic or retroactive migration, and a compromised classical key
   can race a migration.
6. After each protected asset spend, inspect the native PQ anchor balance.
   Ordinary single-program native change can replenish it, but exact-fee,
   dust, explicit-change, and multiple-program cases may need a manual
   RVN top-up before another spend. Back up and verify the ML-DSA key and
   descriptor association before moving administrative tokens. Preserve
   classical keys for historical UTXOs until those holdings are migrated.

## 17. Current Anchor Funding Procedure

This procedure documents FINDING-148, FINDING-150, and FINDING-151 while the
automatic refresh and raw-funding gaps remain open. A protected asset UTXO is
not lost when its native anchor is spent. Consensus still requires a matching
native witness-v2 input for its next spend, and the wallet rejects a protected
spend when it cannot find one. The owner can restore availability by sending
native RVN to the PQ address in the asset's original descriptor. Confirm that
payment, then retry the asset spend. Check every distinct PQ program involved
in a transaction. A payment to a different PQ address cannot authorize it.

The following situations can leave no reusable anchor even when a protected
asset or owner token returns to the wallet:

- an exact-fee or dust remainder with no native change output;
- a spend that consumes assets under multiple PQ programs, since one native
  change output cannot refresh them all;
- an explicit native RVN change destination other than the relevant PQ
  address;
- an ordinary RVN payment that selects the last funded asset anchor.

For a raw protected asset spend, `fundrawtransaction` can add fee funding but
does not discover the required anchors from the asset inputs. Construct it
with these steps:

1. Identify each protected asset outpoint being spent and record its committed
   32-byte PQ program and associated native witness-v2 address.
2. Select at least one funded native witness-v2 UTXO with the same program for
   each distinct protected program. Include those outpoints explicitly in
   the raw transaction's input list beside the asset outpoints.
3. Use a canonical `assetID|PQ` descriptor for each new asset output. Add a
   positive, spendable native RVN output back to every PQ address whose last
   anchor is being consumed. Reserve enough RVN for fees and future spends.
4. If using `fundrawtransaction` for the remaining fee, inspect its resulting
   inputs and outputs. It neither supplies a missing matching anchor nor
   guarantees that its change replenishes one. Sign the complete transaction,
   inspect it again, then broadcast it. An unanchored protected spend is
   rejected with `bad-pq-asset-anchor`.

## 18. Interpreting the Current Asset Index

FINDING-153 described an earlier dual-signature candidate in which two PQ
programs could share a classical address-index row. The PQ-only format now
derives a distinct synthetic asset identifier from each PQ program. A
historical classical output still has its original address identifier, so
index rows alone do not establish whether an outpoint needs a classical key
or a matching PQ anchor. Use the actual output's script and recorded
descriptor for custody and transfers. A descriptor assembled manually from
owned key material is not necessarily listed by `listpqassetaddresses`,
which lists persisted wallet pairings.

## 19. Migrating Historical Assets and Owner Tokens

FINDING-155 concerns partial movement of a historical asset UTXO. Activation
does not add a PQ program to outputs created earlier. Their classical key can
still authorize a spend, but every spendable asset output newly created after
activation needs a canonical PQ tag. Migrate before the old classical key can
be compromised:

1. Back up the wallet and verify that the destination PQ key and the
   classical keys for historical outputs can be recovered. Inventory
   historical asset and `ASSET!` owner-token
   outpoints, including their quantities and classical addresses.
2. For a single legacy asset UTXO, transfer its entire quantity to a new
   canonical `assetID|PQ` descriptor. Transfer an owner token's full unit
   the same way. If a source address holds several UTXOs, use explicit
   outpoint selection when exact UTXO control is needed.
3. For a partial transfer through `transferfromaddress` or
   `transferfromaddresses`, supply the canonical descriptor in the final
   `asset_change_address` argument as well as a protected recipient. Without
   it, the wallet rejects the transaction rather than creating classical-only
   asset change. The GUI path has no equivalent explicit asset-change field.
4. Verify that every newly created spendable asset and owner-token output has
   its intended PQ program. Fund each destination's native PQ address with
   RVN before its first protected spend. Keep the original descriptor and PQ
   key available for recovery, and retain classical keys for any historical
   holdings.

No software can distinguish the rightful owner from an attacker who already
controls the old classical key. Migration does not retroactively protect the
historical UTXO, and a compromised holder can be raced before confirmation.

## 20. PQ-Only Asset Ownership Decision and Candidate Format

The owner selected the native-RVN migration model for assets and owner tokens.
Outputs created under the bit-13 rule require an ML-DSA-44 key without an
ECDSA signature. Historical asset and owner-token outputs retain their
classical spending rule and migrate voluntarily, with no sunset. The
PQ-only format and wallet path passed paired CI on `9151946ad` under
FINDING-158; public activation and release qualification remain pending.

This means a holder can reach PQ-only custody after migrating all historical
UTXOs, but consensus continues to accept classical spends of unmigrated
historical outputs. Bits 12 and 13 do not by themselves impose a global
PQ-only spending rule on the earlier UTXO set.

An input carrying a native witness-v2 RVN output with the matching PQ program
provides the ML-DSA authorization for an asset input. The asset input has an
empty scriptSig and needs no ECDSA signature. The separate native input still
needs RVN funding and may need replenishment after the spend, as described in
section 17.

The PQ-only asset locking prefix is exactly 25 bytes:

```
OP_1 OP_1 PUSH20 HASH160(P) OP_DROP OP_DROP
OP_RVN_ASSET <canonical asset payload and 32-byte program P>
```

The asset opcode remains at byte 25, so the existing asset parser identifies
the output and extracts `HASH160(P)` as its address identifier. The prefix
executes to one true stack item with an empty scriptSig; it does not execute
ECDSA. The existing script interpreter treats every byte after
`OP_RVN_ASSET` as asset data. Consensus requires a matching valid witness-v2
anchor whenever such an output is spent at an active origin height and
rejects the old dual-signature prefix on new outputs. The
`HASH160(P)` binding prevents a sender from selecting an unrelated qualified
or unfrozen classical address while assigning ownership to a different PQ
program. The 160-bit identifier and restricted-asset semantics were included
in the independent asset review; final release-SHA regression remains due.

A red-then-green unit test shows that this prefix passes the existing
standard script check with an empty scriptSig, is recognized as an asset,
and is parsed only when the address identifier matches `HASH160(P)`.
Core 4.8.0 also accepted a 932-block regtest chain with 66 such outputs
and 35 later spends at ordinary block sizes. This does not resolve the
native block-limit split in FINDING-160. Wallet, RPC, Qt, migration and
consensus regressions passed paired release-target CI at `9151946ad`.
The owner-approved bit-13 dates are in `src/chainparams.cpp` at
`1dfae5b21`, with a red-then-green parameter regression and local
`make check`. The recorded active-chain testnet replay, scan, and
`verifychain 4 2016` passed. Final-SHA CI and package review remain
required; this historical check cannot prove future activation or
unknown side branches.
