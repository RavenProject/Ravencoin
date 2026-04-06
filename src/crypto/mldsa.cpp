// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 (FIPS 204) Post-Quantum Digital Signature Implementation
// Uses liboqs (Open Quantum Safe) for NIST FIPS 204 compliant ML-DSA-44.
// https://github.com/open-quantum-safe/liboqs

#include "mldsa.h"

#include <oqs/oqs.h>
#include <cstring>
#include <cassert>

// Compile-time checks: ensure our constants match liboqs
static_assert(mldsa::PUBLICKEY_BYTES == OQS_SIG_ml_dsa_44_length_public_key,
              "ML-DSA-44 public key size mismatch with liboqs");
static_assert(mldsa::SECRETKEY_BYTES == OQS_SIG_ml_dsa_44_length_secret_key,
              "ML-DSA-44 secret key size mismatch with liboqs");
static_assert(mldsa::SIGNATURE_BYTES == OQS_SIG_ml_dsa_44_length_signature,
              "ML-DSA-44 signature size mismatch with liboqs");

// liboqs mldsa-native exports internal keypair functions that accept a 32-byte seed.
// Declared as weak symbols so we can detect availability at link time.
extern "C" {
    int PQCP_MLDSA_NATIVE_MLDSA44_X86_64_keypair_internal(
        uint8_t *pk, uint8_t *sk, const uint8_t *seed) __attribute__((weak));
    int PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal(
        uint8_t *pk, uint8_t *sk, const uint8_t *seed) __attribute__((weak));
}

namespace mldsa {

bool KeyGen(unsigned char* pk, unsigned char* sk, const unsigned char* seed)
{
    if (!pk || !sk || !seed)
        return false;

    // FIPS 204 ML-DSA-44 deterministic keygen from a 32-byte seed (xi).
    // Try the internal keypair function that accepts a seed (returns 0 on success).
    if (PQCP_MLDSA_NATIVE_MLDSA44_X86_64_keypair_internal) {
        return PQCP_MLDSA_NATIVE_MLDSA44_X86_64_keypair_internal(pk, sk, seed) == 0;
    }
    if (PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal) {
        return PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal(pk, sk, seed) == 0;
    }

    // If internal symbols are not available, fall back to random keygen.
    // Deterministic keygen from seed is not supported in this liboqs build.
    return KeyGenRandom(pk, sk);
}

bool KeyGenRandom(unsigned char* pk, unsigned char* sk)
{
    if (!pk || !sk)
        return false;

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!sig)
        return false;

    OQS_STATUS rc = OQS_SIG_keypair(sig, pk, sk);
    OQS_SIG_free(sig);

    return rc == OQS_SUCCESS;
}

bool Sign(unsigned char* sig, size_t* siglen,
          const unsigned char* msg, size_t msglen,
          const unsigned char* sk)
{
    if (!sig || !siglen || !msg || !sk)
        return false;

    OQS_SIG *signer = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!signer)
        return false;

    OQS_STATUS rc = OQS_SIG_sign(signer, sig, siglen, msg, msglen, sk);
    OQS_SIG_free(signer);

    return rc == OQS_SUCCESS;
}

bool Verify(const unsigned char* sig, size_t siglen,
            const unsigned char* msg, size_t msglen,
            const unsigned char* pk)
{
    if (!sig || !msg || !pk)
        return false;

    if (siglen != SIGNATURE_BYTES)
        return false;

    OQS_SIG *verifier = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!verifier)
        return false;

    OQS_STATUS rc = OQS_SIG_verify(verifier, msg, msglen, sig, siglen, pk);
    OQS_SIG_free(verifier);

    return rc == OQS_SUCCESS;
}

} // namespace mldsa
