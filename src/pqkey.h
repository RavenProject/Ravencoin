// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 Post-Quantum Key Classes
//
// Witness v2 uses ML-DSA-44 signatures only (no ECDSA).
// Old addresses keep using ECDSA (witness v0).
// New PQ addresses use ML-DSA-44 exclusively.
// Gradual wallet migration makes the system quantum-resistant.

#ifndef RAVEN_PQKEY_H
#define RAVEN_PQKEY_H

#include "crypto/mldsa.h"
#include "serialize.h"
#include "uint256.h"
#include "support/allocators/secure.h"

#include <vector>

/**
 * An ML-DSA-44 public key for post-quantum witness v2 addresses.
 *
 * Size: 1312 bytes (FIPS 204 ML-DSA-44)
 * Witness program: SHA256(mldsa_pubkey) = 32 bytes
 */
class CPQPubKey
{
private:
    std::vector<unsigned char> vch;

public:
    CPQPubKey() : vch() {}
    CPQPubKey(const unsigned char* pbegin, const unsigned char* pend) : vch(pbegin, pend) {}
    CPQPubKey(const std::vector<unsigned char>& v) : vch(v) {}

    unsigned int size() const { return vch.size(); }
    const unsigned char* data() const { return vch.data(); }
    const unsigned char* begin() const { return vch.data(); }
    const unsigned char* end() const { return vch.data() + vch.size(); }

    bool IsValid() const { return vch.size() == mldsa::PUBLICKEY_BYTES; }

    /** Compute witness v2 program: SHA256(mldsa_pubkey) */
    uint256 GetWitnessProgram() const;

    /** Verify an ML-DSA-44 signature over a 32-byte hash */
    bool Verify(const uint256& hash, const std::vector<unsigned char>& sig) const;

    std::vector<unsigned char> GetVch() const { return vch; }

    friend bool operator==(const CPQPubKey& a, const CPQPubKey& b) { return a.vch == b.vch; }
    friend bool operator!=(const CPQPubKey& a, const CPQPubKey& b) { return a.vch != b.vch; }
    friend bool operator<(const CPQPubKey& a, const CPQPubKey& b) { return a.vch < b.vch; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vch);
    }
};

/**
 * An ML-DSA-44 private key for post-quantum signing.
 *
 * Size: 2560 bytes (FIPS 204 ML-DSA-44)
 * Uses secure allocator to protect key material in memory.
 */
class CPQKey
{
private:
    bool fValid;
    std::vector<unsigned char, secure_allocator<unsigned char>> keydata;
    CPQPubKey pubkey;

public:
    CPQKey() : fValid(false), keydata(mldsa::SECRETKEY_BYTES, 0) {}

    ~CPQKey()
    {
        if (keydata.size() > 0)
            memory_cleanse(keydata.data(), keydata.size());
    }

    bool IsValid() const { return fValid; }

    /** Generate a new random ML-DSA-44 keypair */
    void MakeNewKey();

    /** Generate a deterministic ML-DSA-44 keypair from a 32-byte seed */
    bool SetSeed(const unsigned char* seed);

    CPQPubKey GetPubKey() const { return pubkey; }

    /** Sign a 32-byte hash with ML-DSA-44 */
    bool Sign(const uint256& hash, std::vector<unsigned char>& sigOut) const;

    /** Get raw secret key data (for wallet serialization) */
    const std::vector<unsigned char, secure_allocator<unsigned char>>& GetKeyData() const { return keydata; }

    /** Set key from raw data (for wallet deserialization), recomputes pubkey */
    bool SetKeyData(const std::vector<unsigned char>& data);
};

#endif // RAVEN_PQKEY_H
