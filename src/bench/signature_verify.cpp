// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"
#include "chainparams.h"
#include "crypto/mldsa.h"
#include "key.h"
#include "support/allocators/secure.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace {

uint256 BenchmarkDigest()
{
    uint256 digest;
    for (size_t i = 0; i < digest.size(); ++i)
        digest.begin()[i] = static_cast<unsigned char>(i);
    return digest;
}

// Compare production verifier wrappers with valid signatures. Key generation
// and signing happen before State starts timing. Each iteration must verify
// successfully, including in builds where assertions are disabled.
void VerifyMLDSA44(benchmark::State& state)
{
    const uint256 digest = BenchmarkDigest();
    const Consensus::PQSignatureContext& context = GetParams().GetConsensus().pqSignatureContext;
    if (!Consensus::IsValidPQSignatureContext(context))
        throw std::runtime_error("ML-DSA benchmark requires a valid network context");

    const std::array<unsigned char, mldsa::SEED_BYTES> seed{};
    std::array<unsigned char, mldsa::PUBLICKEY_BYTES> publicKey{};
    SecureVector secretKey(mldsa::SECRETKEY_BYTES);
    std::array<unsigned char, mldsa::SIGNATURE_BYTES> signature{};
    size_t signatureLength = 0;

    if (!mldsa::KeyGen(publicKey.data(), secretKey.data(), seed.data()) ||
        !mldsa::Sign(signature.data(), &signatureLength,
                     digest.begin(), digest.size(), context.data(), context.size(),
                     secretKey.data())) {
        throw std::runtime_error("ML-DSA benchmark setup failed");
    }
    memory_cleanse(secretKey.data(), secretKey.size());
    if (signatureLength != signature.size() ||
        !mldsa::Verify(signature.data(), signatureLength,
                       digest.begin(), digest.size(), context.data(), context.size(),
                       publicKey.data())) {
        throw std::runtime_error("ML-DSA benchmark signature is invalid");
    }

    while (state.KeepRunning()) {
        if (!mldsa::Verify(signature.data(), signatureLength,
                           digest.begin(), digest.size(), context.data(), context.size(),
                           publicKey.data())) {
            throw std::runtime_error("ML-DSA benchmark verification failed");
        }
    }
}

void VerifySecp256k1ECDSA(benchmark::State& state)
{
    const uint256 digest = BenchmarkDigest();
    CKey key;
    key.MakeNewKey(true);
    const CPubKey publicKey = key.GetPubKey();
    std::vector<unsigned char> signature;
    if (!key.Sign(digest, signature) || !publicKey.Verify(digest, signature))
        throw std::runtime_error("secp256k1 benchmark setup failed");

    while (state.KeepRunning()) {
        if (!publicKey.Verify(digest, signature))
            throw std::runtime_error("secp256k1 benchmark verification failed");
    }
}

} // namespace

BENCHMARK(VerifyMLDSA44);
BENCHMARK(VerifySecp256k1ECDSA);
