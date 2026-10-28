# RIP-25: Post-Quantum Signatures via ML-DSA-44

```
RIP: 25
Title: Post-Quantum Signatures via ML-DSA-44
Authors: ALENOC (https://github.com/ALENOC)
Status: Draft
Type: Standards Track (Consensus)
Created: 2026-04-05
License: MIT
```

---

## Abstract

This RIP proposes adding **ML-DSA-44** (FIPS 204) as a post-quantum digital signature scheme to Ravencoin via a new **witness version 2** program. New PQ addresses use ML-DSA-44 exclusively (no ECDSA). Existing ECDSA addresses (witness v0) continue working unchanged. Users can migrate native RVN from ECDSA to ML-DSA-44 addresses before quantum computers can break ECDSA. Ravencoin asset outputs are not covered by this version of RIP-25.

The witness-v2 signature rule uses the SegWit extensibility model and bit-12
signaling. The combined deployment also raises the block weight limit from
8 MWU to 12 MWU at activation and to 16 MWU one nominal year later, and
discounts PQ witness bytes. Those block rules require a coordinated network
upgrade from Core 4.8.0. See Backwards Compatibility below.

---

## Motivation

### The Quantum Threat to Ravencoin

Ravencoin relies exclusively on ECDSA over the secp256k1 elliptic curve for all transaction authorization. The security of ECDSA rests on the Elliptic Curve Discrete Logarithm Problem (ECDLP), which Shor's algorithm solves in polynomial time on a sufficiently large quantum computer.

**Timeline estimates for a Cryptographically Relevant Quantum Computer (CRQC):**

| Source | Estimate |
|--------|----------|
| NSA CNSA 2.0 (2022) | Requires PQ migration to begin immediately; full compliance by 2035 |
| NIST (2024) | "Within the next few decades" |
| IBM Quantum Roadmap | 100,000+ qubit systems by 2033 |
| Global Risk Institute (2024) | ~50% probability of CRQC by 2037 |
| BSI (German Federal Office) | Recommends PQ migration by 2030 |

The consensus places the CRQC threat window at **2034-2041**. Given that blockchain migration takes years to design, implement, test, deploy, and achieve user adoption, preparation must begin now.

### "Harvest Now, Decrypt Later" and Blockchain Immutability

Unlike encrypted communications, blockchain data is:

1. **Publicly available** -- anyone can download the entire Ravencoin blockchain
2. **Immutable** -- public keys exposed in transactions are permanently recorded
3. **Economically motivated** -- UTXOs retain (or appreciate in) value indefinitely
4. **Unrevocable** -- no central authority can rotate compromised keys

### Ravencoin-Specific Risk: The Asset Layer

Ravencoin's unique asset layer amplifies the quantum threat beyond simple coin theft:

- **Admin token theft** (`ASSET!`) gives an attacker control over an asset's supply and administrative properties
- **Unique assets and NFTs** cannot be "replaced" after theft
- **Restricted asset qualifiers** control who can transact with restricted assets
- **Message channel assets** enable impersonation and fraudulent messaging

### Why Act Now

- **Migration timeline**: A conservative 2-3 year development cycle plus multi-year adoption period means activation around 2029-2030
- **FIPS 204 is finalized**: ML-DSA was standardized by NIST in August 2024
- **First-mover advantage**: No major UTXO-based cryptocurrency has deployed production PQ signatures

---

## Specification

### 1. Algorithm Selection: ML-DSA-44

**ML-DSA** (Module-Lattice-Based Digital Signature Algorithm), standardized in NIST FIPS 204, is selected as the post-quantum signature scheme. The ML-DSA-44 parameter set provides the optimal balance for blockchain use:

| Parameter | ECDSA/secp256k1 (current) | ML-DSA-44 (proposed) |
|-----------|---------------------------|----------------------|
| Public key size | 33 bytes (compressed) | 1,312 bytes |
| Private key size | 32 bytes | 2,560 bytes |
| Signature size | ~72 bytes (DER) | 2,420 bytes |
| Security level | 128-bit classical / **0-bit quantum** | 128-bit classical / **128-bit quantum** |
| Verify time (AVX2) | ~0.035 ms | ~0.02 ms |
| Sign time (AVX2) | ~0.015 ms | ~0.08 ms |

#### Why ML-DSA-44 Over Alternatives

| Scheme | Verdict | Rationale |
|--------|---------|-----------|
| **ML-DSA-44 (FIPS 204)** | **Selected** | Best balance of size, speed, implementation simplicity; FIPS standardized; stateless; mature ecosystem |
| ML-DSA-65 / ML-DSA-87 | Rejected | 192/256-bit classical security is overkill. Larger signatures penalize throughput with no practical security gain |
| FN-DSA / FALCON (FIPS 206) | Rejected | Smallest PQ signatures (~666 B) but requires high-precision floating-point arithmetic -- complex, fragile, side-channel prone |
| SLH-DSA / SPHINCS+ (FIPS 205) | Rejected | Enormous signatures (7,856-49,856 B) and very slow verification |
| XMSS / LMS (SP 800-208) | Rejected | **Stateful** -- fundamentally incompatible with the UTXO wallet model |

### 2. Design: ML-DSA-44 Only (Gradual Migration)

#### 2.1 Why ML-DSA Only Instead of Hybrid AND-Composition

An earlier design considered requiring **both** ECDSA + ML-DSA-44 for every witness v2 transaction. This AND-composition approach has a critical flaw for the migration scenario:

- If quantum computers break ECDSA, users who haven't yet migrated to witness v2 lose their funds
- Users who **have** migrated to witness v2 are stuck: their transactions require a valid ECDSA signature, but ECDSA is broken
- The AND-composition provides no escape path when one algorithm fails

The correct design uses **ML-DSA-44 exclusively** for new witness v2 addresses:

- **Old addresses (witness v0):** Continue using ECDSA, unchanged
- **New addresses (witness v2):** Use ML-DSA-44 only, quantum-resistant from day one
- **Migration:** Users send funds from old ECDSA addresses to new ML-DSA addresses at their own pace
- **When ECDSA breaks:** Users who already migrated are fully protected. Non-migrated users must migrate urgently, but their new PQ addresses work without any ECDSA dependency

#### 2.2 Security Properties

| Scenario | Witness v0 (ECDSA) | Witness v2 (ML-DSA-44) |
|----------|--------------------|------------------------|
| Classical adversary | Secure | Secure |
| Quantum adversary (CRQC) | **Broken** | **Secure** |
| ML-DSA algorithmic break | Secure | **Broken** (but no known attack exists) |

The migration approach provides a clean upgrade path: once funds are in witness v2, they are quantum-resistant regardless of what happens to ECDSA.

### 3. Witness Version 2 Deployment

#### 3.1 Address Format

PQ addresses use **witness version 2** with Bech32m encoding (BIP 350):

```
scriptPubKey: OP_2 <32-byte SHA256(mldsa_pubkey)>
address:      rvn1z...  (mainnet, bech32m encoded)
              trvn1z... (testnet)
              rcrt1z... (regtest)
```

The 32-byte SHA256 hash provides 128-bit collision resistance classically and ~85-bit quantum collision resistance.

#### 3.1.1 Deterministic Wallet Key Derivation

The wallet derivation below is a versioned recovery contract. It does not
change witness-v2 consensus validation.

The root source is selected by the wallet type:

- source `0x01`: the exact 32-byte legacy HD private seed selected by
  `CHDChain::seed_id`;
- source `0x02`: the exact 64-byte BIP39 seed output, after applying the
  mnemonic passphrase.

The wallet derives every child as hardened using this exact path:

```
m/25'/coin_type'/0'/0'/index'
```

`coin_type` is 175 on mainnet and 1 on testnet and regtest. `index` is an
unsigned 31-bit value. The BIP32 leaf is the canonical 32-byte big-endian
private scalar returned by `CKey`, with no object dump, host-endian field, or
text formatting. The final ML-DSA seed is:

```
pq_seed = SHA256(ASCII("RVN/ML-DSA-44/keygen/v1") || pq_bip32_leaf)
```

The ASCII domain has no trailing NUL. `pq_seed` is passed to the single
deterministic ML-DSA-44 key-generation wrapper. Wallet derivation does not
replace or otherwise modify a process-global random provider.

The wallet stores the next allocation index in the key-critical
`pqhdchain` record. Version 1 has this exact 45-byte value layout:

```
uint32_le version
uint32_le next_external_index
uint8     seed_source
uint32_le coin_type
byte[32]  lineage_id
```

`lineage_id` contains the raw SHA-256 digest bytes from:

```
SHA256(ASCII("RVN/ML-DSA-44/lineage/v1") ||
       seed_source || BE32(coin_type) || exact_wallet_seed)
```

The domain has no trailing NUL. A persisted record must use version 1, a
known source, `coin_type < 0x80000000`, a nonzero lineage identifier, and
`next_external_index <= 0x80000000`. The terminal value `0x80000000` marks
the branch exhausted. A source, network, or root-seed change starts allocation
at index zero for the new lineage. Existing PQ key records remain unchanged.

The new private-key record and advanced counter are committed in one
synchronous wallet-database transaction. For an encrypted wallet the new key
is persisted only as `cpqkey`; for an unencrypted wallet it is persisted as
`pqkey`. Failure to write either record leaves neither a published key nor an
advanced in-memory counter.

Compatibility and recovery rules are explicit:

- PQ keys created before this derivation contract remain valid individual
  `pqkey` or `cpqkey` records. They cannot be reconstructed from a mnemonic.
- A wallet-file backup preserves those old records and the new `pqhdchain`
  state. The text `dumpwallet` and `importwallet` formats do not carry PQ keys
  and are not PQ backup formats.
- A clean mnemonic restoration reproduces a deterministic PQ key only after
  regenerating the same network and index. This implementation has no
  automatic PQ lookahead or used-index discovery. Recovery therefore requires
  regenerating enough sequential PQ addresses and rescanning the chain.
- Encrypting a legacy non-BIP39 HD wallet rotates its classical HD seed.
  Pre-rotation PQ keys remain recoverable only through their stored wallet
  records or a wallet-file backup. Later PQ keys begin at index zero under the
  new lineage.
- A historical non-HD wallet has no deterministic root for this contract and
  `getnewpqaddress` fails instead of silently creating another random key.

#### 3.2 Transaction Structure

PQ transactions use the existing SegWit serialization format. The witness stack for a PQ input contains:

```
Witness stack (2 elements):
  [0] ML-DSA-44 signature (2,420 bytes)
  [1] ML-DSA-44 public key (1,312 bytes)
```

The `scriptSig` is empty (as with all SegWit inputs). The `scriptPubKey` is the compact 34-byte witness program.

Witness-v2 PQ supports exactly one signature hash mode: implicit `SIGHASH_ALL`.
The 2,420-byte ML-DSA signature is serialized without an appended signature
hash byte. A 2,421-byte value such as `signature || 0x01`, and requests for
`SIGHASH_NONE`, `SIGHASH_SINGLE`, or `SIGHASH_ANYONECANPAY`, are invalid.

#### 3.2.1 ML-DSA Network Context

RIP-25 uses the FIPS 204 context-string interface. The context is exactly 81
bytes and is constructed as:

```
ASCII("RVN/ML-DSA-44/v1/" || lowercase_hex_64(network_genesis_hash))
```

The prefix, slash separators, case, and 64-character hash encoding are fixed.
The terminating C string NUL is not part of the context. Locale-dependent or
display-oriented hash formatting is not used.

```
mainnet: RVN/ML-DSA-44/v1/0000006b444bc2f2ffe627be9d9e7e7a0730000870ef6eb6da46c8eae389df90
testnet: RVN/ML-DSA-44/v1/000000ecfc5e6324a079542221d00e10362bdc894d56500c414060eea8a3ad5a
regtest: RVN/ML-DSA-44/v1/0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c
```

Signing and verification must use the context belonging to the selected
network. A signature made under one of these contexts is invalid under either
of the other two contexts.

#### 3.3 Witness Validation Rules

When a node encounters a witness version 2 program of length 32 bytes:

1. The witness stack MUST contain exactly 2 elements
2. Let `mldsa_sig = witness[0]`, `mldsa_pk = witness[1]`
3. Validate: `mldsa_pk` is exactly 1,312 bytes (ML-DSA-44 public key size)
4. Validate: `mldsa_sig` is exactly 2,420 bytes (ML-DSA-44 signature size)
5. Verify: `SHA256(mldsa_pk) == witness_program` (public key binding)
6. Compute `sighash` using BIP143-style hashing with `SIGVERSION_WITNESS_V2_PQ` and implicit `SIGHASH_ALL`
7. Verify: `ML_DSA_44_Verify(mldsa_pk, sighash, network_context, mldsa_sig)` (ML-DSA check)
8. If all checks pass, the input is valid

Unupgraded nodes treat witness version 2 outputs as "anyone-can-spend"
under BIP141. They do not enforce the ML-DSA signature rule, and miner
signaling does not make them compatible with the higher block limits or
PQ witness discount. See the public activation plan below.

#### 3.4 Script Size Limits

For witness version 2, a new element size limit applies:

```cpp
static const unsigned int MAX_PQ_WITNESS_ELEMENT_SIZE = 4096; // bytes
```

This limit applies only to witness v2 stack elements. Witness v0 and legacy script limits are unchanged.

### 4. Block Weight and Fee Structure

#### 4.1 PQ Witness Discount

ML-DSA signatures and public keys are pure validation overhead. A **PQ witness discount** (scale factor 8) appropriately reflects this by counting PQ witness data at reduced weight.

```cpp
static const int PQ_WITNESS_SCALE_FACTOR = 8;
```

##### Weight Calculation

The standard SegWit weight formula is:

```
weight = stripped_size × 4 + witness_size
```

For PQ witness v2 inputs (detected by a 2-element witness stack: 2,420-byte sig + 1,312-byte pk), `GetTransactionWeight` applies an additional discount. Each PQ witness byte is reduced from **1 WU** (standard segwit) to **0.5 WU** (8x discount):

```
pq_discount = pq_witness_bytes × (PQ_WITNESS_SCALE_FACTOR − WITNESS_SCALE_FACTOR) / PQ_WITNESS_SCALE_FACTOR
            = 3732 × (8 − 4) / 8
            = 1866 WU

adjusted_weight = standard_weight − pq_discount
```

##### Effective Virtual Size

For a typical single-input PQ transaction (~200 bytes stripped, ~3,732 bytes PQ witness):

| Metric | Without PQ discount | With PQ discount |
|--------|---------------------|------------------|
| Weight (WU) | ~4,532 | ~2,666 |
| Virtual size (vbytes) | ~1,133 | ~667 |
| Relay fee (at 0.01 RVN/kB) | ~0.01133 RVN | ~0.00667 RVN |

##### Wallet Fee Estimation

The wallet estimates transaction size before signing using `DummySignTx`. For PQ witness v2 inputs, `ProduceSignature` cannot verify dummy ML-DSA data, so `DummySignTx` detects witness v2 PQ outputs and inserts correctly-sized dummy witness data (2,420 + 1,312 bytes). This ensures the fee calculation accounts for the full PQ witness size and discount.

#### 4.2 Phased Block Weight Increase

| Phase | Max Block Weight | Activation |
|-------|-----------------|------------|
| Current (RIP-2) | 8,000,000 WU | Active |
| Phase 1: PQ Opt-in | 12,000,000 WU | At PQ activation height |
| Phase 2: PQ Standard | 16,000,000 WU | 1 year after Phase 1 |

At 1-minute block times, even Phase 1 provides thousands of PQ transactions per minute, far exceeding current real-world usage.

The 12 and 16 MWU limits are part of the original RIP-25 plan. Core 4.8.0
rejects blocks above 8 MWU. The new acceptance range and the PQ witness
discount make the combined bit-12 activation incompatible with an unupgraded
Core 4.8.0 node. A local two-binary regtest accepted a 7,600,636-WU block
on both binaries, then accepted an 8,801,320-WU block only on the bit-12
candidate; Core 4.8.0 returned `bad-blk-length`. This is a hard-fork
boundary for block validation, even though the witness-v2 signature rule
alone is restrictive. A separate block with a valid ML-DSA witness-v2
spend weighed 8,000,697 WU under the old calculation and about
7,998,831 WU after the PQ discount. Core 4.8.0 returned
`bad-blk-weight`, while the candidate accepted it. The discount alone
also crosses the old validation boundary.

#### 4.2.1 PQ Signature Operation Cost

Starting at bit-12 ACTIVE, each native or P2SH-wrapped witness-v2 ML-DSA
input contributes two units to the block signature-operation cost. Before
activation it contributes zero under this rule. The block limit remains
80,000 cost units, and the miner uses the same contextual count as block
validation. The value is a fixed consensus constant, independent of the
CPU or liboqs backend used by a node.

Five paired portable-backend benchmark runs measured a median ML-DSA to
ECDSA verification-time ratio of 1.748 on the audited x86_64 system. Two
cost units cover that measured ratio; they are not a claim about every
future platform. Tests exercise native and wrapped inputs, pre-activation
counting, exact-limit acceptance, one-over rejection, and miner template
selection with stale cached costs. This cost adjustment is a consensus
change from the initial RIP-25 value of one and is included in the bit-12
incompatible upgrade.

#### 4.3 Dust Threshold

For PQ outputs, the spend cost increases proportionally to the ML-DSA witness size:

```
PQ witness input: mldsa_sig (2420) + mldsa_pk (1312) = 3732 bytes
With contextual PQ discount: 3732 / 8 = ~467 virtual bytes,
or about 1866 weight units for these bytes
```

### 5. Activation Mechanism

#### 5.1 BIP9 Version Bit Signaling

```
Deployment parameters:
  bit:                                    12
  mainnet nStartTime:                     1814400000 (2027-07-01 00:00 UTC)
  mainnet nTimeout:                       1861920000 (2029-01-01 00:00 UTC)
  nOverrideRuleChangeActivationThreshold: 1714  (85% of 2016 blocks)
  nOverrideMinerConfirmationWindow:       2016  (~33.6 hours at the target spacing)
```

The owner approved this 18-month mainnet window on 2026-10-13. BIP9 uses
median time past at window boundaries; the start timestamp is not an
activation date. At least 1714 signals in one complete STARTED window
lead to one LOCKED_IN window, then ACTIVE. If the timeout is reached
before lock-in, the deployment fails. The 85% threshold measures miner
signaling only; it does not establish readiness of validating services.

### 6. Implementation

#### 6.1 Library Integration

The consensus build pins **liboqs 0.16.0** from the reviewed release archive
with SHA256
`162d5b510518ee5f285f82fa1f16402a885176e818bf1b1a4c3c91c9a2f01eae`.
The archive contains mldsa-native revision
`9b0ee84f4cf399043eca59eca4e5f8531ca1d61b` (v1.0.0-beta2). The depends
configuration enables ML-DSA-44 only, disables shared libraries and OpenSSL,
and records the reviewed source checksum in pkg-config metadata. Production
configuration requires the exact version and provenance. There is no
unversioned `-loqs` fallback.

The **liboqs** library (Open Quantum Safe, MIT license) provides the ML-DSA-44 implementation:

- Production-quality, constant-time operations
- AVX2 (x86_64) and NEON (ARM) optimizations
- MIT license (compatible with Ravencoin)
- Active maintenance tracking NIST standard updates

#### 6.2 Key Classes

```cpp
class CPQPubKey {
    std::vector<unsigned char> vch;  // 1,312 bytes
public:
    bool IsValid() const;  // vch.size() == 1312
    uint256 GetWitnessProgram() const;  // SHA256(vch)
    bool Verify(const uint256& hash, const std::vector<unsigned char>& sig,
                const unsigned char* context, size_t contextlen) const;
};

class CPQKey {
    std::vector<unsigned char, secure_allocator<unsigned char>> keydata;  // 2,560 bytes
public:
    void MakeNewKey();
    bool SetSeed(const unsigned char* seed);
    bool Sign(const uint256& hash, std::vector<unsigned char>& sigOut,
              const unsigned char* context, size_t contextlen) const;
    CPQPubKey GetPubKey() const;
};
```

#### 6.3 Key Files Modified

| Category | Files | Changes |
|----------|-------|---------|
| **Crypto** | `crypto/mldsa.h/cpp` | ML-DSA-44 wrapper around the pinned liboqs mldsa-native backend |
| **Keys** | `pqkey.h/cpp` | `CPQKey`/`CPQPubKey` classes |
| **Script** | `script/interpreter.h` | `SCRIPT_VERIFY_PQ_HYBRID` flag, `SIGVERSION_WITNESS_V2_PQ` |
| **Script** | `script/interpreter.cpp` | Witness v2 validation (2-element stack), `WitnessSigOps` for v2 |
| **Script** | `script/script_error.h/cpp` | PQ-specific error codes |
| **Script** | `script/standard.h/cpp` | `TX_WITNESS_V2_PQ_KEYHASH`, `WitnessV2PQDestination` |
| **Script** | `script/sign.h/cpp` | ML-DSA signing via `TransactionSignatureCreator` |
| **Script** | `script/ismine.cpp` | `IsMine` for witness v2 outputs |
| **Consensus** | `consensus/consensus.h/cpp` | Block weight increase, PQ constants (`PQ_WITNESS_SCALE_FACTOR`, `MAX_PQ_WITNESS_ELEMENT_SIZE`) |
| **Consensus** | `consensus/validation.h` | PQ witness weight discount in `GetTransactionWeight` |
| **Consensus** | `consensus/params.h` | `DEPLOYMENT_PQ_HYBRID` flag |
| **Validation** | `validation.cpp/h` | `GetBlockScriptFlags()`, `IsPQHybridDeployed()` |
| **Validation** | `versionbits.cpp` | `pq_hybrid` deployment info registration |
| **Wallet** | `wallet/rpcwallet.cpp` | `getnewpqaddress` RPC command |
| **Wallet** | `wallet/pqderivation.h/cpp` | Versioned hardened PQ BIP32 derivation and lineage identification |
| **Wallet** | `wallet/walletdb.h/cpp` | PQ key persistence and versioned `pqhdchain` allocation state |
| **Wallet** | `wallet/wallet.h/cpp` | Atomic deterministic generation, encrypted/plain persistence, and legacy record loading |
| **Wallet** | `wallet/crypter.h/cpp` | PQ key encryption: `mapCryptedPQKeys`, `AddCryptedPQKey`, `EncryptKeys`/`Unlock` for PQ keys |
| **Keystore** | `keystore.h` | PQ key maps (`PQKeyMap`, `PQPubKeyMap`, `CryptedPQKeyMap`) |
| **Address** | `bech32.h/cpp` (new) | Bech32m encoding/decoding (BIP350) |
| **Address** | `base58.cpp` | `EncodeDestination`/`DecodeDestination` for bech32m |
| **Params** | `chainparams.h/cpp` | Bech32m HRP (`rvn`/`trvn`/`rcrt`), BIP9 deployment |
| **P2P** | `protocol.h` | `NODE_PQ_HYBRID` service flag (bit 5) |
| **P2P** | `net.h` | 16 MB `MAX_PROTOCOL_MESSAGE_LENGTH` |
| **P2P** | `init.cpp` | Advertise `NODE_PQ_HYBRID` service |
| **Policy** | `policy/policy.h/cpp` | PQ dust threshold, `IsWitnessStandard` (2-element stack) |
| **Build** | `configure.ac`, `Makefile.am` | liboqs integration, `--with-liboqs` |
| **Build** | `depends/packages/liboqs.mk`, `packages.mk` | liboqs depends package |
| **Tests** | `test/pqkey_tests.cpp`, `Makefile.test.include` | ML-DSA-44 and CPQKey unit tests |

### 7. Migration Plan

#### 7.1 Phased Rollout

| Phase | Timeline | Description |
|-------|----------|-------------|
| **Phase 0: Preparation** | Months 1-6 | Software release with dormant PQ code. Community education. Testnet deployment. |
| **Phase 1: Activation** | Months 7-12 | Bit 12 activates after signaling and lock-in. PQ addresses are available. Block weight increases to 12 MWU, requiring upgraded validating nodes. |
| **Phase 2: Encouraged** | Months 13-18 | Wallets default to PQ addresses for new keys. Warnings for legacy addresses. |
| **Phase 3: Standard** | Months 19-24 | Block weight increases to 16 MWU. |
| **Phase 4: Deprecation** | Months 25-48 | Legacy-only transactions increasingly discouraged. |

#### 7.2 Wallet Migration

Users migrate native RVN by sending it from legacy addresses to new PQ addresses:

1. Generate new PQ address via `getnewpqaddress` RPC (or wallet UI)
2. Create transaction spending UTXOs from legacy address to PQ address
3. Sign with existing ECDSA key (standard legacy transaction)
4. Broadcast and confirm

After migration, native RVN change outputs can go to PQ addresses. Until
the separate bit-13 rule activates, asset-bearing change still uses the
historical format. After activation, newly created spendable asset change
must use the PQ-only asset format.

#### 7.3 Asset Scope

RIP-25 witness-v2 protects native RVN outputs only. It does not change the Ravencoin asset script envelope, which binds spendable asset outputs to legacy P2PKH key identifiers. This limitation applies to:

- normal and reissuable assets;
- owner tokens such as `ASSET!`;
- unique assets;
- restricted assets such as `$ASSET`, whose administration depends on `ASSET!`;
- qualifier and sub-qualifier assets.

This scope statement describes the approved bit 12 baseline and the default
public-network state. A 4.8.1 working branch has separate, approved future
bit-13 windows for testnet and mainnet. Once bit 13 activates, new asset
and owner-token outputs
commit to a PQ program. Spending them requires a matching native RVN
witness-v2 input signed with ML-DSA-44, without an ECDSA signature. Asset
and owner-token outputs use an asset carrier keyed by the PQ program, not a
direct witness-v2 script. Outputs created before activation remain classically
spendable. Their owners may migrate voluntarily by sending them to new
PQ-protected outputs; activation does not protect historical holdings
retroactively. This candidate is outside the approved PR #1281 semantics
and is not yet qualified for public-network release. See the linked asset
extension note for its constraints and release blockers.

A wallet or raw-transaction RPC must reject a witness-v2 PQ destination when constructing an asset-bearing output. Appending `OP_RVN_ASSET` data to an `OP_2 <32-byte-program>` script does not create a PQ asset output: it makes the script cease to be a witness program, and current consensus rejects the misplaced asset opcode.

Under the default public-network parameters, asset owners retain a post-quantum exposure. Even after bit 13 activation, historical outputs retain that exposure until their owners migrate them. In particular, theft of `ASSET!` can transfer administrative control and can authorize reissuance where the asset remains reissuable. See [RIP-25 PQ Asset Extension Design Note](RIP-0025-PQ-Assets-Followup.md).

#### 7.4 Emergency Response Plan

If ECDSA is broken before migration completes:

1. **Immediate**: Emergency alert. Users with PQ addresses are already safe.
2. **Short-term**: Emergency wallet update with auto-migration to PQ addresses.
3. **Medium-term**: Miners soft-enforce: reject transactions spending from exposed-pubkey ECDSA addresses unless migrating to PQ.

---

## Backwards Compatibility

The original draft described the whole proposal as a soft fork. That
description applies to the witness-v2 signature restriction alone, not to
the higher block limits or PQ witness discount. Core 4.8.0 validating nodes
can reject blocks that a post-activation node accepts. All validating
services that intend to follow the upgraded chain must install compatible
software before bit 12 becomes active. Miner signaling measures miner
readiness; it does not upgrade old nodes or guarantee their acceptance of
12/16 MWU blocks. The public activation plan below is an incompatible
consensus upgrade, approved for documentation by the owner. Release and
activation still depend on the listed qualification gates.

### Public network activation plan for bit 12

1. Distribute the qualified 4.8.1 release before the mainnet bit-12
   STARTED window beginning no earlier than 2027-07-01 00:00 UTC by
   median time past. Its timeout is 2029-01-01 00:00 UTC. Announce the
   exact start, timeout, 1714/2016 threshold, and 2016-block window with
   binaries, checksums, upgrade instructions, and the native RVN migration
   path. Tell miners, pools, validating nodes, exchanges, custodians, and
   explorers that Core 4.8.0 can reject blocks accepted by upgraded nodes.
2. Before requesting bit-12 signaling, validate the complete shared testnet
   history against the network-bound ML-DSA rule. Repeat the two-binary
   block tests around the Core 4.8.0 8 MWU boundary, including a PQ witness
   spend whose old and new weights straddle that boundary. Qualify the
   final versioned SHA with mandatory `make check`, all release targets,
   and the Final Gate. If a required gate is missing or the network is not
   ready, change the public parameters in a reviewed release before the
   STARTED window rather than relying on miner signaling to protect old
   validators.
3. Bit-12 BIP9 signaling measures miner readiness. LOCKED_IN is followed
   by ACTIVE after the next full window. All validating services that need
   to follow the upgraded chain must update before ACTIVE. Signaling does
   not measure their adoption, and there is no automatic rollback to the
   Core 4.8.0 weight rules after ACTIVE.
4. At the bit-12 activation height `H12`, the limit becomes 12 MWU and
   the contextual PQ witness discount takes effect. At `H12 + 525600`
   blocks, one nominal year at the 60-second target spacing, the limit
   becomes 16 MWU. This second phase is height-based, not a fixed calendar
   date. Operators must treat both phases as part of the same incompatible
   network upgrade.
5. The bit-13 mainnet STARTED timestamp is also 2029-01-01 00:00 UTC.
   Bit 13 cannot enforce PQ-only asset spending until bit 12 and the
   transfer-script deployment on bit 8 are ACTIVE.
   If bit 12 fails or activates too late for a safe asset rollout, review
   the bit-13 window in a separate release before it starts. Do not treat
   the shared timestamp as evidence that bit 12 has activated.

The old and new binaries agreed below 8 MWU in the recorded regtest
comparison. Core 4.8.0 rejected a larger candidate-accepted block and a
separate discounted PQ-witness block just across its old 8 MWU boundary.
The exact blocks, weights, and outcomes are recorded in FINDING-160 of the
security remediation register. Approval of this plan does not assert that
testnet history or final release qualification has already passed.

The remaining transaction compatibility properties are:

- **Unupgraded nodes**: See witness v2 outputs as "anyone-can-spend" per BIP141 rules
- **Legacy addresses**: Continue to work indefinitely
- **Legacy transactions**: Continue to be valid. No existing transaction type is modified
- **Asset transactions**: Unchanged by the approved bit-12 baseline. The separate bit-13 rule requires a PQ-only carrier and matching native ML-DSA anchor for newly created spendable asset outputs after its own activation. These outputs need no ECDSA signature; historical outputs retain their classical rule
- **Migration**: Voluntary. Users migrate funds at their own pace

The network context was added before mainnet RIP-25 activation. Mainnet has no
valid pre-context RIP-25 history, so the change does not alter an active
mainnet rule. Testnet and regtest are configured for immediate PQ testing.
Experimental databases produced by an earlier empty-context build are not
silently compatible: an old empty-context PQ signature is invalid under the
network-bound rules. Regtest operators must discard and recreate such chains.
Before a shared testnet deployment, operators must revalidate its full history
and prove that it contains no previously accepted empty-context witness-v2 PQ
spend. If one exists, a separately reviewed activation boundary is required;
deploying this rule directly over that history would be unsafe.

---

## Security Considerations

- **Shor's algorithm** breaks ECDSA in polynomial time on a CRQC. ML-DSA-44 is resistant.
- **ML-DSA-44 security** rests on the Module Learning With Errors (MLWE) problem, studied since 2005 and surviving 8 years of NIST public cryptanalysis
- **Consensus determinism**: ML-DSA verification must produce identical results across all platforms. liboqs provides constant-time, platform-independent implementations.
- **Network replay separation**: FIPS 204 signing and verification use the exact network-genesis context defined above. The full script cache key includes the same context.
- **DoS resistance**: Larger transactions increase bandwidth. The PQ witness discount and block weight limits provide economic protection.
- **Side-channel**: ML-DSA signing uses rejection sampling. Constant-time liboqs implementations mitigate timing attacks.
- **Asset owner-token exposure**: RIP-25 does not protect `ASSET!` or other asset UTXOs. A future activated asset extension is required before asset ownership and administration can be considered quantum-resistant.

---

## References

1. NIST FIPS 204, "Module-Lattice-Based Digital Signature Standard (ML-DSA)," August 2024
2. Shor, P.W., "Polynomial-Time Algorithms for Prime Factorization and Discrete Logarithms on a Quantum Computer," 1997
3. NSA, "Commercial National Security Algorithm Suite 2.0 (CNSA 2.0)," September 2022
4. BIP 141, "Segregated Witness (Consensus layer)"
5. BIP 143, "Transaction Signature Verification for Version 0 Witness Program"
6. BIP 350, "Bech32m format for v1+ witness addresses"
7. Open Quantum Safe, liboqs, https://github.com/open-quantum-safe/liboqs
8. Global Risk Institute, "Quantum Threat Timeline Report," 2024
9. Ravencoin Whitepaper, Fenton, Black, et al., 2018

---

## Copyright

This document is licensed under the MIT License.
