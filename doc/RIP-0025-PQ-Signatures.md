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

This RIP proposes adding **ML-DSA-44** (FIPS 204) as a post-quantum digital signature scheme to Ravencoin via a new **witness version 2** program. New PQ addresses use ML-DSA-44 exclusively (no ECDSA). Existing ECDSA addresses (witness v0) continue working unchanged. Users gradually migrate funds from ECDSA to ML-DSA-44 addresses, making the system quantum-resistant before quantum computers can break ECDSA.

The upgrade is deployed as a **soft fork** following the SegWit extensibility model. A phased block weight increase from 8 MWU to 16 MWU, combined with a PQ witness discount factor, ensures that network throughput remains adequate during and after migration.

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

- **Admin token theft** (`$ASSET!`) gives an attacker control over an asset's entire supply and properties
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

#### 3.2 Transaction Structure

PQ transactions use the existing SegWit serialization format. The witness stack for a PQ input contains:

```
Witness stack (2 elements):
  [0] ML-DSA-44 signature (2,420 bytes)
  [1] ML-DSA-44 public key (1,312 bytes)
```

The `scriptSig` is empty (as with all SegWit inputs). The `scriptPubKey` is the compact 34-byte witness program.

#### 3.3 Witness Validation Rules

When a node encounters a witness version 2 program of length 32 bytes:

1. The witness stack MUST contain exactly 2 elements
2. Let `mldsa_sig = witness[0]`, `mldsa_pk = witness[1]`
3. Validate: `mldsa_pk` is exactly 1,312 bytes (ML-DSA-44 public key size)
4. Validate: `mldsa_sig` is exactly 2,420 bytes (ML-DSA-44 signature size)
5. Verify: `SHA256(mldsa_pk) == witness_program` (public key binding)
6. Compute `sighash` using BIP143-style hashing with `SIGVERSION_WITNESS_V2_PQ`
7. Verify: `ML_DSA_44_Verify(mldsa_pk, sighash, mldsa_sig)` (ML-DSA check)
8. If all checks pass, the input is valid

For unupgraded nodes, witness version 2 outputs are treated as "anyone-can-spend" per BIP141 rules, which is safe as long as a supermajority of miners enforce the new rules.

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

#### 4.3 Dust Threshold

For PQ outputs, the spend cost increases proportionally to the ML-DSA witness size:

```
PQ witness input: mldsa_sig (2420) + mldsa_pk (1312) = 3732 bytes
With PQ discount: 3732 / 8 = ~467 weight units
```

### 5. Activation Mechanism

#### 5.1 BIP9 Version Bit Signaling

```
Deployment parameters:
  bit:                                    11
  nStartTime:                             <6 months after release>
  nTimeout:                               <18 months after start>
  nOverrideRuleChangeActivationThreshold: 1714  (85% of 2016 blocks)
  nOverrideMinerConfirmationWindow:       2016  (~33.6 hours)
```

The 85% threshold provides additional safety margin for this cryptographically significant upgrade.

### 6. Implementation

#### 6.1 Library Integration

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
    bool Verify(const uint256& hash, const std::vector<unsigned char>& sig) const;
};

class CPQKey {
    std::vector<unsigned char, secure_allocator<unsigned char>> keydata;  // 2,560 bytes
public:
    void MakeNewKey();
    bool SetSeed(const unsigned char* seed);
    bool Sign(const uint256& hash, std::vector<unsigned char>& sigOut) const;
    CPQPubKey GetPubKey() const;
};
```

#### 6.3 Key Files Modified

| Category | Files | Changes |
|----------|-------|---------|
| **Crypto** | `crypto/mldsa.h/cpp` | ML-DSA-44 wrapper around liboqs |
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
| **Wallet** | `wallet/walletdb.h/cpp` | PQ key persistence: `WritePQKey`, `WriteCryptedPQKey`, `ReadKeyValue` handlers for `"pqkey"`/`"cpqkey"` |
| **Wallet** | `wallet/wallet.h/cpp` | `AddPQKeyPubKey` (disk persist), `AddCryptedPQKey`, `LoadPQKey`/`LoadCryptedPQKey` |
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
| **Phase 1: Activation** | Months 7-12 | Soft fork activates via BIP9. PQ addresses available. Block weight increases to 12 MWU. |
| **Phase 2: Encouraged** | Months 13-18 | Wallets default to PQ addresses for new keys. Warnings for legacy addresses. |
| **Phase 3: Standard** | Months 19-24 | Block weight increases to 16 MWU. |
| **Phase 4: Deprecation** | Months 25-48 | Legacy-only transactions increasingly discouraged. |

#### 7.2 Wallet Migration

Users migrate by sending their funds from legacy addresses to new PQ addresses:

1. Generate new PQ address via `getnewpqaddress` RPC (or wallet UI)
2. Create transaction spending UTXOs from legacy address to PQ address
3. Sign with existing ECDSA key (standard legacy transaction)
4. Broadcast and confirm

After migration, all new change outputs can go to PQ addresses.

#### 7.3 Emergency Response Plan

If ECDSA is broken before migration completes:

1. **Immediate**: Emergency alert. Users with PQ addresses are already safe.
2. **Short-term**: Emergency wallet update with auto-migration to PQ addresses.
3. **Medium-term**: Miners soft-enforce: reject transactions spending from exposed-pubkey ECDSA addresses unless migrating to PQ.

---

## Backwards Compatibility

This proposal is a **soft fork**. Backwards compatibility is maintained as follows:

- **Unupgraded nodes**: See witness v2 outputs as "anyone-can-spend" per BIP141 rules
- **Legacy addresses**: Continue to work indefinitely
- **Legacy transactions**: Continue to be valid. No existing transaction type is modified
- **Asset transactions**: All asset operations work with both legacy and PQ addresses
- **Migration**: Voluntary. Users migrate funds at their own pace

---

## Security Considerations

- **Shor's algorithm** breaks ECDSA in polynomial time on a CRQC. ML-DSA-44 is resistant.
- **ML-DSA-44 security** rests on the Module Learning With Errors (MLWE) problem, studied since 2005 and surviving 8 years of NIST public cryptanalysis
- **Consensus determinism**: ML-DSA verification must produce identical results across all platforms. liboqs provides constant-time, platform-independent implementations.
- **DoS resistance**: Larger transactions increase bandwidth. The PQ witness discount and block weight limits provide economic protection.
- **Side-channel**: ML-DSA signing uses rejection sampling. Constant-time liboqs implementations mitigate timing attacks.

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
