// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 (FIPS 204) Post-Quantum Digital Signature Implementation
// Uses liboqs (Open Quantum Safe) for NIST FIPS 204 compliant ML-DSA-44.
// https://github.com/open-quantum-safe/liboqs

#include "mldsa.h"

#include "crypto/sha256.h"

#include <oqs/oqs.h>

#if !defined(OQS_VERSION_MAJOR) || !defined(OQS_VERSION_MINOR) || !defined(OQS_VERSION_PATCH)
#error "RIP-25 requires liboqs version macros"
#endif

#if OQS_VERSION_MAJOR != 0 || OQS_VERSION_MINOR != 16 || OQS_VERSION_PATCH != 0
#error "RIP-25 requires exactly liboqs 0.16.0"
#endif

#include <array>
#include <cstring>

// Compile-time checks: ensure our constants match liboqs.
static_assert(mldsa::PUBLICKEY_BYTES == OQS_SIG_ml_dsa_44_length_public_key,
              "ML-DSA-44 public key size mismatch with liboqs");
static_assert(mldsa::SECRETKEY_BYTES == OQS_SIG_ml_dsa_44_length_secret_key,
              "ML-DSA-44 secret key size mismatch with liboqs");
static_assert(mldsa::SIGNATURE_BYTES == OQS_SIG_ml_dsa_44_length_signature,
              "ML-DSA-44 signature size mismatch with liboqs");

namespace {

// liboqs 0.16.0 embeds mldsa-native. Its portable C keygen_internal symbol is
// deterministic, cross-platform, and part of the exact backend pinned by
// depends and configure. Keep this backend-specific entry point in this one
// wrapper translation unit.
extern "C" int PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal(
    std::uint8_t* pk, std::uint8_t* sk, const std::uint8_t* seed);

void CleanseKeypair(unsigned char* pk, unsigned char* sk)
{
    if (pk)
        OQS_MEM_cleanse(pk, mldsa::PUBLICKEY_BYTES);
    if (sk)
        OQS_MEM_cleanse(sk, mldsa::SECRETKEY_BYTES);
}

OQS_SIG* NewMLDSA44()
{
    if (!OQS_SIG_alg_is_enabled(OQS_SIG_alg_ml_dsa_44))
        return nullptr;

    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!sig)
        return nullptr;

    if (sig->length_public_key != mldsa::PUBLICKEY_BYTES ||
        sig->length_secret_key != mldsa::SECRETKEY_BYTES ||
        sig->length_signature != mldsa::SIGNATURE_BYTES ||
        !sig->sig_with_ctx_support || !sig->sign_with_ctx_str ||
        !sig->verify_with_ctx_str) {
        OQS_SIG_free(sig);
        return nullptr;
    }

    return sig;
}

} // namespace

namespace mldsa {

bool KeyGen(unsigned char* pk, unsigned char* sk, const unsigned char* seed)
{
    if (!pk || !sk || !seed) {
        CleanseKeypair(pk, sk);
        return false;
    }

    OQS_SIG* descriptor = NewMLDSA44();
    if (!descriptor) {
        CleanseKeypair(pk, sk);
        return false;
    }
    // The descriptor proves the runtime algorithm before the internal
    // deterministic entry point is used.
    OQS_SIG_free(descriptor);

    const int rc = PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal(pk, sk, seed);
    if (rc != 0) {
        CleanseKeypair(pk, sk);
        return false;
    }
    return true;
}

bool KeyGenRandom(unsigned char* pk, unsigned char* sk)
{
    if (!pk || !sk) {
        CleanseKeypair(pk, sk);
        return false;
    }

    OQS_SIG* sig = NewMLDSA44();
    if (!sig) {
        CleanseKeypair(pk, sk);
        return false;
    }

    const OQS_STATUS rc = OQS_SIG_keypair(sig, pk, sk);
    OQS_SIG_free(sig);

    if (rc != OQS_SUCCESS) {
        CleanseKeypair(pk, sk);
        return false;
    }
    return true;
}

bool Sign(unsigned char* sig, size_t* siglen,
          const unsigned char* msg, size_t msglen,
          const unsigned char* context, size_t contextlen,
          const unsigned char* sk)
{
    if (!sig || !siglen || !msg || !context || contextlen == 0 ||
        contextlen > MAX_CONTEXT_BYTES || !sk) {
        if (siglen)
            *siglen = 0;
        return false;
    }

    OQS_SIG* signer = NewMLDSA44();
    if (!signer) {
        OQS_MEM_cleanse(sig, SIGNATURE_BYTES);
        *siglen = 0;
        return false;
    }

    const OQS_STATUS rc = OQS_SIG_sign_with_ctx_str(
        signer, sig, siglen, msg, msglen, context, contextlen, sk);
    OQS_SIG_free(signer);

    if (rc != OQS_SUCCESS || *siglen != SIGNATURE_BYTES) {
        OQS_MEM_cleanse(sig, SIGNATURE_BYTES);
        *siglen = 0;
        return false;
    }
    return true;
}

bool Verify(const unsigned char* sig, size_t siglen,
            const unsigned char* msg, size_t msglen,
            const unsigned char* context, size_t contextlen,
            const unsigned char* pk)
{
    if (!sig || !msg || !context || contextlen == 0 ||
        contextlen > MAX_CONTEXT_BYTES || !pk)
        return false;

    if (siglen != SIGNATURE_BYTES)
        return false;

    OQS_SIG* verifier = NewMLDSA44();
    if (!verifier)
        return false;

    const OQS_STATUS rc = OQS_SIG_verify_with_ctx_str(
        verifier, msg, msglen, sig, siglen, context, contextlen, pk);
    OQS_SIG_free(verifier);

    return rc == OQS_SUCCESS;
}

bool SelfTest()
{
    static const unsigned char context[] = "RVN/ML-DSA-44/selftest/v1";
    static_assert(sizeof(context) - 1 <= MAX_CONTEXT_BYTES,
                  "ML-DSA self-test context is too long");
    static const std::array<unsigned char, 32> expectedPublicKeyHash{{
        0xeb, 0x4e, 0x73, 0x02, 0x84, 0x21, 0x53, 0xb0,
        0xfa, 0x19, 0xe8, 0x62, 0x07, 0x39, 0xad, 0x25,
        0x8a, 0xf4, 0x92, 0x9c, 0x26, 0xdd, 0x89, 0x07,
        0x9a, 0x7e, 0xc7, 0xd4, 0x28, 0x22, 0x08, 0xe1
    }};
    static const std::array<unsigned char, 32> message{{
        0x52, 0x56, 0x4e, 0x2f, 0x4d, 0x4c, 0x2d, 0x44,
        0x53, 0x41, 0x2d, 0x34, 0x34, 0x2f, 0x73, 0x65,
        0x6c, 0x66, 0x74, 0x65, 0x73, 0x74, 0x2f, 0x76,
        0x31, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
    }};

    std::array<unsigned char, SEED_BYTES> seed{};
    std::array<unsigned char, PUBLICKEY_BYTES> pk{};
    std::array<unsigned char, SECRETKEY_BYTES> sk{};
    std::array<unsigned char, SIGNATURE_BYTES> signature{};
    std::array<unsigned char, 32> publicKeyHash{};
    size_t signatureLength = 0;

    const char* runtimeVersion = OQS_version();
    bool ok = runtimeVersion && std::strcmp(runtimeVersion, "0.16.0") == 0;
    ok = ok && KeyGen(pk.data(), sk.data(), seed.data());
    if (ok) {
        CSHA256().Write(pk.data(), pk.size()).Finalize(publicKeyHash.data());
        ok = publicKeyHash == expectedPublicKeyHash;
    }
    ok = ok && Sign(signature.data(), &signatureLength,
                    message.data(), message.size(),
                    context, sizeof(context) - 1, sk.data());
    ok = ok && signatureLength == SIGNATURE_BYTES;
    ok = ok && Verify(signature.data(), signatureLength,
                      message.data(), message.size(),
                      context, sizeof(context) - 1, pk.data());
    if (ok) {
        signature[0] ^= 1;
        ok = !Verify(signature.data(), signatureLength,
                     message.data(), message.size(),
                     context, sizeof(context) - 1, pk.data());
        signature[0] ^= 1;
    }

    OQS_MEM_cleanse(sk.data(), sk.size());
    OQS_MEM_cleanse(signature.data(), signature.size());
    return ok;
}

} // namespace mldsa
