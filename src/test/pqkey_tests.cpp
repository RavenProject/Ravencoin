// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: ML-DSA-44 Post-Quantum Key Unit Tests

#include "pqkey.h"
#include "crypto/mldsa.h"
#include "uint256.h"
#include "test/test_raven.h"
#include "utilstrencodings.h"

#include <boost/test/unit_test.hpp>

#include <vector>
#include <cstring>

BOOST_FIXTURE_TEST_SUITE(pqkey_tests, BasicTestingSetup)

// ============================================================
// ML-DSA-44 Low-Level Tests
// ============================================================

BOOST_AUTO_TEST_CASE(mldsa_keygen_deterministic)
{
    // Same seed must produce same keypair
    unsigned char seed[32];
    memset(seed, 0x42, 32);

    unsigned char pk1[mldsa::PUBLICKEY_BYTES], sk1[mldsa::SECRETKEY_BYTES];
    unsigned char pk2[mldsa::PUBLICKEY_BYTES], sk2[mldsa::SECRETKEY_BYTES];

    BOOST_CHECK(mldsa::KeyGen(pk1, sk1, seed));
    BOOST_CHECK(mldsa::KeyGen(pk2, sk2, seed));

    BOOST_CHECK(memcmp(pk1, pk2, mldsa::PUBLICKEY_BYTES) == 0);
    BOOST_CHECK(memcmp(sk1, sk2, mldsa::SECRETKEY_BYTES) == 0);
}

BOOST_AUTO_TEST_CASE(mldsa_keygen_different_seeds)
{
    // Different seeds must produce different keypairs
    unsigned char seed1[32], seed2[32];
    memset(seed1, 0x01, 32);
    memset(seed2, 0x02, 32);

    unsigned char pk1[mldsa::PUBLICKEY_BYTES], sk1[mldsa::SECRETKEY_BYTES];
    unsigned char pk2[mldsa::PUBLICKEY_BYTES], sk2[mldsa::SECRETKEY_BYTES];

    BOOST_CHECK(mldsa::KeyGen(pk1, sk1, seed1));
    BOOST_CHECK(mldsa::KeyGen(pk2, sk2, seed2));

    BOOST_CHECK(memcmp(pk1, pk2, mldsa::PUBLICKEY_BYTES) != 0);
}

BOOST_AUTO_TEST_CASE(mldsa_sign_verify_roundtrip)
{
    // Sign and verify must succeed for matching key/message
    unsigned char seed[32];
    memset(seed, 0xAB, 32);

    unsigned char pk[mldsa::PUBLICKEY_BYTES], sk[mldsa::SECRETKEY_BYTES];
    BOOST_CHECK(mldsa::KeyGen(pk, sk, seed));

    unsigned char msg[] = "RIP-25 test message for ML-DSA-44";
    size_t msglen = sizeof(msg) - 1;

    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    BOOST_CHECK(mldsa::Sign(sig, &siglen, msg, msglen, sk));
    BOOST_CHECK_EQUAL(siglen, mldsa::SIGNATURE_BYTES);

    // Verify with correct key and message
    BOOST_CHECK(mldsa::Verify(sig, siglen, msg, msglen, pk));
}

BOOST_AUTO_TEST_CASE(mldsa_verify_wrong_message)
{
    // Verification must fail for wrong message
    unsigned char seed[32];
    memset(seed, 0xCD, 32);

    unsigned char pk[mldsa::PUBLICKEY_BYTES], sk[mldsa::SECRETKEY_BYTES];
    BOOST_CHECK(mldsa::KeyGen(pk, sk, seed));

    unsigned char msg1[] = "correct message";
    unsigned char msg2[] = "wrong message!!";

    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    BOOST_CHECK(mldsa::Sign(sig, &siglen, msg1, sizeof(msg1) - 1, sk));

    // Must fail with different message
    BOOST_CHECK(!mldsa::Verify(sig, siglen, msg2, sizeof(msg2) - 1, pk));
}

BOOST_AUTO_TEST_CASE(mldsa_verify_wrong_key)
{
    // Verification must fail for wrong public key
    unsigned char seed1[32], seed2[32];
    memset(seed1, 0x11, 32);
    memset(seed2, 0x22, 32);

    unsigned char pk1[mldsa::PUBLICKEY_BYTES], sk1[mldsa::SECRETKEY_BYTES];
    unsigned char pk2[mldsa::PUBLICKEY_BYTES], sk2[mldsa::SECRETKEY_BYTES];

    BOOST_CHECK(mldsa::KeyGen(pk1, sk1, seed1));
    BOOST_CHECK(mldsa::KeyGen(pk2, sk2, seed2));

    unsigned char msg[] = "test message";
    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    BOOST_CHECK(mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1, sk1));

    // Must succeed with correct key
    BOOST_CHECK(mldsa::Verify(sig, siglen, msg, sizeof(msg) - 1, pk1));

    // Must fail with wrong key
    BOOST_CHECK(!mldsa::Verify(sig, siglen, msg, sizeof(msg) - 1, pk2));
}

BOOST_AUTO_TEST_CASE(mldsa_verify_tampered_signature)
{
    // Verification must fail for tampered signature
    unsigned char seed[32];
    memset(seed, 0xEF, 32);

    unsigned char pk[mldsa::PUBLICKEY_BYTES], sk[mldsa::SECRETKEY_BYTES];
    BOOST_CHECK(mldsa::KeyGen(pk, sk, seed));

    unsigned char msg[] = "tamper test";
    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    BOOST_CHECK(mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1, sk));

    // Tamper with signature
    sig[100] ^= 0xFF;

    BOOST_CHECK(!mldsa::Verify(sig, siglen, msg, sizeof(msg) - 1, pk));
}

BOOST_AUTO_TEST_CASE(mldsa_verify_wrong_siglen)
{
    unsigned char seed[32];
    memset(seed, 0x33, 32);

    unsigned char pk[mldsa::PUBLICKEY_BYTES], sk[mldsa::SECRETKEY_BYTES];
    BOOST_CHECK(mldsa::KeyGen(pk, sk, seed));

    unsigned char msg[] = "size test";
    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    BOOST_CHECK(mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1, sk));

    // Wrong signature length must fail
    BOOST_CHECK(!mldsa::Verify(sig, siglen - 1, msg, sizeof(msg) - 1, pk));
    BOOST_CHECK(!mldsa::Verify(sig, 0, msg, sizeof(msg) - 1, pk));
}

BOOST_AUTO_TEST_CASE(mldsa_sizes_correct)
{
    // Verify constants match FIPS 204 ML-DSA-44
    BOOST_CHECK_EQUAL(mldsa::PUBLICKEY_BYTES, 1312u);
    BOOST_CHECK_EQUAL(mldsa::SECRETKEY_BYTES, 2560u);
    BOOST_CHECK_EQUAL(mldsa::SIGNATURE_BYTES, 2420u);
    BOOST_CHECK_EQUAL(mldsa::SEED_BYTES, 32u);
}

// ============================================================
// CPQKey / CPQPubKey Tests (ML-DSA-44 Only)
// ============================================================

BOOST_AUTO_TEST_CASE(pqkey_generation)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_CHECK(key.IsValid());

    CPQPubKey pub = key.GetPubKey();
    BOOST_CHECK(pub.IsValid());
    BOOST_CHECK_EQUAL(pub.size(), mldsa::PUBLICKEY_BYTES);
}

BOOST_AUTO_TEST_CASE(pqkey_deterministic_from_seed)
{
    unsigned char seed[32];
    memset(seed, 0xBE, 32);

    CPQKey key1, key2;
    BOOST_CHECK(key1.SetSeed(seed));
    BOOST_CHECK(key2.SetSeed(seed));

    CPQPubKey pub1 = key1.GetPubKey();
    CPQPubKey pub2 = key2.GetPubKey();

    BOOST_CHECK(pub1 == pub2);
}

BOOST_AUTO_TEST_CASE(pqkey_sign_verify_roundtrip)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_CHECK(key.IsValid());

    CPQPubKey pub = key.GetPubKey();
    BOOST_CHECK(pub.IsValid());

    uint256 hash;
    memset(hash.begin(), 0xAA, 32);

    std::vector<unsigned char> sig;
    BOOST_CHECK(key.Sign(hash, sig));
    BOOST_CHECK_EQUAL(sig.size(), mldsa::SIGNATURE_BYTES);

    BOOST_CHECK(pub.Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(pqkey_verify_wrong_hash)
{
    CPQKey key;
    key.MakeNewKey();

    CPQPubKey pub = key.GetPubKey();

    uint256 hash1, hash2;
    memset(hash1.begin(), 0xAA, 32);
    memset(hash2.begin(), 0xBB, 32);

    std::vector<unsigned char> sig;
    BOOST_CHECK(key.Sign(hash1, sig));

    // Must fail with different hash
    BOOST_CHECK(!pub.Verify(hash2, sig));
}

BOOST_AUTO_TEST_CASE(pqkey_verify_wrong_pubkey)
{
    CPQKey key1, key2;
    key1.MakeNewKey();
    key2.MakeNewKey();

    CPQPubKey pub2 = key2.GetPubKey();

    uint256 hash;
    memset(hash.begin(), 0xCC, 32);

    std::vector<unsigned char> sig;
    BOOST_CHECK(key1.Sign(hash, sig));

    // Verify with wrong key must fail
    BOOST_CHECK(!pub2.Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(pqkey_witness_program)
{
    CPQKey key;
    key.MakeNewKey();

    CPQPubKey pub = key.GetPubKey();

    // Witness program must be 32 bytes (SHA256 of ML-DSA pubkey)
    uint256 wp = pub.GetWitnessProgram();
    BOOST_CHECK(!wp.IsNull());

    // Same key must produce same witness program
    uint256 wp2 = pub.GetWitnessProgram();
    BOOST_CHECK(wp == wp2);
}

BOOST_AUTO_TEST_CASE(pqkey_different_keys_different_witness_programs)
{
    CPQKey key1, key2;
    key1.MakeNewKey();
    key2.MakeNewKey();

    uint256 wp1 = key1.GetPubKey().GetWitnessProgram();
    uint256 wp2 = key2.GetPubKey().GetWitnessProgram();

    BOOST_CHECK(wp1 != wp2);
}

BOOST_AUTO_TEST_CASE(pqkey_multiple_signatures)
{
    CPQKey key;
    key.MakeNewKey();

    CPQPubKey pub = key.GetPubKey();

    // Sign multiple different messages
    for (int i = 0; i < 5; i++) {
        uint256 hash;
        memset(hash.begin(), i, 32);

        std::vector<unsigned char> sig;
        BOOST_CHECK(key.Sign(hash, sig));
        BOOST_CHECK(pub.Verify(hash, sig));
    }
}

BOOST_AUTO_TEST_CASE(pqkey_set_key_data)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_CHECK(key.IsValid());

    // Get raw key data
    const auto& keydata = key.GetKeyData();
    BOOST_CHECK_EQUAL(keydata.size(), mldsa::SECRETKEY_BYTES);

    // Create new key from raw data
    CPQKey key2;
    std::vector<unsigned char> data(keydata.begin(), keydata.end());
    BOOST_CHECK(key2.SetKeyData(data));
    BOOST_CHECK(key2.IsValid());
}

BOOST_AUTO_TEST_CASE(pqkey_invalid_state)
{
    CPQKey key;
    BOOST_CHECK(!key.IsValid());

    uint256 hash;
    memset(hash.begin(), 0x11, 32);

    std::vector<unsigned char> sig;
    BOOST_CHECK(!key.Sign(hash, sig));
}

BOOST_AUTO_TEST_CASE(pqpubkey_invalid_size)
{
    // Empty pubkey
    CPQPubKey pub;
    BOOST_CHECK(!pub.IsValid());

    // Wrong size pubkey
    std::vector<unsigned char> bad(100, 0);
    CPQPubKey pub2(bad);
    BOOST_CHECK(!pub2.IsValid());
}

BOOST_AUTO_TEST_CASE(pqpubkey_verify_rejects_wrong_sig_size)
{
    CPQKey key;
    key.MakeNewKey();
    CPQPubKey pub = key.GetPubKey();

    uint256 hash;
    memset(hash.begin(), 0xDD, 32);

    // Wrong size signature
    std::vector<unsigned char> bad_sig(100, 0);
    BOOST_CHECK(!pub.Verify(hash, bad_sig));
}

BOOST_AUTO_TEST_SUITE_END()
