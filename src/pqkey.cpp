// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 Post-Quantum Key Implementation

#include "pqkey.h"
#include "crypto/sha256.h"

#include <cstring>

// --- CPQPubKey ---

uint256 CPQPubKey::GetWitnessProgram() const
{
    uint256 result;
    CSHA256 hasher;
    hasher.Write(vch.data(), vch.size());
    hasher.Finalize(result.begin());
    return result;
}

bool CPQPubKey::Verify(const uint256& hash, const std::vector<unsigned char>& sig,
                       const unsigned char* context, size_t contextlen) const
{
    if (!IsValid())
        return false;

    if (sig.size() != mldsa::SIGNATURE_BYTES)
        return false;

    return mldsa::Verify(sig.data(), sig.size(),
                         hash.begin(), 32,
                         context, contextlen,
                         vch.data());
}

// --- CPQKey ---

void CPQKey::Clear()
{
    if (!keydata.empty())
        memory_cleanse(keydata.data(), keydata.size());
    fValid = false;
    pubkey = CPQPubKey();
}

void CPQKey::MakeNewKey()
{
    Clear();
    unsigned char pk[mldsa::PUBLICKEY_BYTES];

    if (!mldsa::KeyGenRandom(pk, keydata.data())) {
        Clear();
        memory_cleanse(pk, sizeof(pk));
        return;
    }

    pubkey = CPQPubKey(pk, pk + mldsa::PUBLICKEY_BYTES);
    memory_cleanse(pk, sizeof(pk));
    fValid = true;
}

bool CPQKey::SetSeed(const unsigned char* seed)
{
    Clear();
    if (!seed)
        return false;

    unsigned char pk[mldsa::PUBLICKEY_BYTES];

    if (!mldsa::KeyGen(pk, keydata.data(), seed)) {
        Clear();
        memory_cleanse(pk, sizeof(pk));
        return false;
    }

    pubkey = CPQPubKey(pk, pk + mldsa::PUBLICKEY_BYTES);
    memory_cleanse(pk, sizeof(pk));
    fValid = true;
    return true;
}

bool CPQKey::Sign(const uint256& hash, std::vector<unsigned char>& sigOut,
                  const unsigned char* context, size_t contextlen) const
{
    if (!fValid)
        return false;

    sigOut.resize(mldsa::SIGNATURE_BYTES);
    size_t siglen = 0;

    if (!mldsa::Sign(sigOut.data(), &siglen,
                     hash.begin(), 32,
                     context, contextlen,
                     keydata.data())) {
        sigOut.clear();
        return false;
    }

    if (siglen != mldsa::SIGNATURE_BYTES) {
        sigOut.clear();
        return false;
    }

    return true;
}

bool CPQKey::SetKeyData(const KeyData& data)
{
    if (data.size() != mldsa::SECRETKEY_BYTES) {
        Clear();
        return false;
    }

    if (keydata.data() != data.data()) {
        Clear();
        std::memcpy(keydata.data(), data.data(), mldsa::SECRETKEY_BYTES);
    }
    pubkey = CPQPubKey();
    fValid = true;
    return true;
}

bool CPQKey::MatchesPubKey(const CPQPubKey& pubkeyIn) const
{
    static const unsigned char context[] = "RVN/ML-DSA-44/keybind/v1";
    static_assert(sizeof(context) - 1 <= mldsa::MAX_CONTEXT_BYTES,
                  "ML-DSA key-binding context is too long");

    if (!fValid || !pubkeyIn.IsValid())
        return false;

    // Fixed non-secret challenge: possession of the secret key is proven by
    // producing a valid ML-DSA signature that verifies under pubkeyIn.
    uint256 challenge;
    std::memset(challenge.begin(), 0x52, 32); // 'R' for Ravencoin

    std::vector<unsigned char> sig;
    if (!Sign(challenge, sig, context, sizeof(context) - 1))
        return false;

    return pubkeyIn.Verify(challenge, sig, context, sizeof(context) - 1);
}

bool CPQKey::SetKeyData(const KeyData& data, const CPQPubKey& pubkeyIn)
{
    if (!SetKeyData(data))
        return false;

    if (!MatchesPubKey(pubkeyIn)) {
        Clear();
        return false;
    }

    pubkey = pubkeyIn;
    return true;
}
