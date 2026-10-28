// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 (FIPS 204) post-quantum digital signature wrapper.
// Uses the pinned liboqs 0.16.0 mldsa-native backend.

#ifndef RAVEN_CRYPTO_MLDSA_H
#define RAVEN_CRYPTO_MLDSA_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mldsa {

// ML-DSA-44 (FIPS 204) constants. These must match liboqs exactly.
static const size_t PUBLICKEY_BYTES  = 1312;
static const size_t SECRETKEY_BYTES  = 2560;
static const size_t SIGNATURE_BYTES  = 2420;
static const size_t SEED_BYTES       = 32;
static const size_t MAX_CONTEXT_BYTES = 255;

/**
 * Generate an ML-DSA-44 keypair from a 32-byte seed.
 * Deterministic: the same seed always produces the same keypair. This calls
 * the pinned portable mldsa-native internal key-generation entry point
 * directly and never changes liboqs process-global RNG state.
 *
 * @param[out] pk   Public key buffer (must be PUBLICKEY_BYTES)
 * @param[out] sk   Secret key buffer (must be SECRETKEY_BYTES)
 * @param[in]  seed 32-byte seed
 * @return true on success
 */
bool KeyGen(unsigned char* pk, unsigned char* sk, const unsigned char* seed);

/**
 * Generate an ML-DSA-44 keypair from random entropy.
 * Uses OQS_SIG_keypair() internally.
 *
 * @param[out] pk  Public key buffer (must be PUBLICKEY_BYTES)
 * @param[out] sk  Secret key buffer (must be SECRETKEY_BYTES)
 * @return true on success
 */
bool KeyGenRandom(unsigned char* pk, unsigned char* sk);

/**
 * Sign a message using ML-DSA-44.
 * Uses the FIPS 204 context-string API. Empty contexts are rejected.
 *
 * @param[out] sig     Signature buffer (must be SIGNATURE_BYTES)
 * @param[out] siglen  Actual signature length (always SIGNATURE_BYTES for ML-DSA-44)
 * @param[in]  msg     Message to sign
 * @param[in]  msglen  Message length
 * @param[in]  context Domain-separation context, without a trailing NUL
 * @param[in]  contextlen Context length from 1 through MAX_CONTEXT_BYTES
 * @param[in]  sk      Secret key (SECRETKEY_BYTES)
 * @return true on success
 */
bool Sign(unsigned char* sig, size_t* siglen,
          const unsigned char* msg, size_t msglen,
          const unsigned char* context, size_t contextlen,
          const unsigned char* sk);

/**
 * Verify an ML-DSA-44 signature.
 * Uses OQS_SIG_verify() internally.
 *
 * @param[in] sig     Signature (SIGNATURE_BYTES)
 * @param[in] siglen  Signature length
 * @param[in] msg     Message
 * @param[in] msglen  Message length
 * @param[in] context Domain-separation context, without a trailing NUL
 * @param[in] contextlen Context length from 1 through MAX_CONTEXT_BYTES
 * @param[in] pk      Public key (PUBLICKEY_BYTES)
 * @return true if signature is valid
 */
bool Verify(const unsigned char* sig, size_t siglen,
            const unsigned char* msg, size_t msglen,
            const unsigned char* context, size_t contextlen,
            const unsigned char* pk);

/**
 * Run a fixed known-answer and sign/verify sanity check against the loaded
 * consensus crypto backend. Nodes must refuse startup when this fails.
 */
bool SelfTest();

} // namespace mldsa

#endif // RAVEN_CRYPTO_MLDSA_H
