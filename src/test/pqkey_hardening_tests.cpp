// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: adversarial regression tests for PQ key/wallet and v4.8 port hardening.

#include "chain.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/rip25.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "crypto/mldsa.h"
#include "crypto/sha256.h"
#include "hash.h"
#include "keystore.h"
#include "policy/policy.h"
#include "pqkey.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_raven.h"
#include "utilstrencodings.h"
#include "wallet/pqderivation.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

// The pinned liboqs 0.16.0 backend exposes the FIPS 204 internal signing API
// for deterministic known-answer testing. Production signing stays randomized.
extern "C" int PQCP_MLDSA_NATIVE_MLDSA44_C_signature_internal(
    unsigned char* signature, size_t* signatureLength,
    const unsigned char* message, size_t messageLength,
    const unsigned char* prefix, size_t prefixLength,
    const unsigned char* randomness, const unsigned char* secretKey,
    int externalMu);

BOOST_FIXTURE_TEST_SUITE(pqkey_hardening_tests, BasicTestingSetup)

namespace {

static const unsigned char TEST_CONTEXT[] = "RVN/ML-DSA-44/unit-test/v1";

const Consensus::PQSignatureContext& NetworkContext(const char* network)
{
    static const std::unique_ptr<CChainParams> mainParams = CreateChainParams("main");
    static const std::unique_ptr<CChainParams> testParams = CreateChainParams("test");
    static const std::unique_ptr<CChainParams> regtestParams = CreateChainParams("regtest");

    if (std::strcmp(network, "main") == 0)
        return mainParams->GetConsensus().pqSignatureContext;
    if (std::strcmp(network, "test") == 0)
        return testParams->GetConsensus().pqSignatureContext;
    return regtestParams->GetConsensus().pqSignatureContext;
}

class NetworkSelectionRestore
{
    const std::string original = GetParams().NetworkIDString();

public:
    ~NetworkSelectionRestore() { SelectParams(original); }
};

} // namespace

BOOST_AUTO_TEST_CASE(pq_secret_material_uses_secure_allocator_and_legacy_encoding)
{
    static_assert(std::is_same<CPQKey::KeyData::allocator_type,
                               secure_allocator<unsigned char>>::value,
                  "PQ secret keys require secure_allocator");

    const CPQKey::KeyData secureSecret(mldsa::SECRETKEY_BYTES, 0x5a);
    const std::vector<unsigned char> legacySecret(secureSecret.begin(), secureSecret.end());

    CDataStream secureEncoding(SER_DISK, 0);
    CDataStream legacyEncoding(SER_DISK, 0);
    secureEncoding << secureSecret;
    legacyEncoding << legacySecret;
    BOOST_CHECK_EQUAL_COLLECTIONS(secureEncoding.begin(), secureEncoding.end(),
                                  legacyEncoding.begin(), legacyEncoding.end());

    const std::vector<unsigned char> pubkeyBytes(mldsa::PUBLICKEY_BYTES, 0x33);
    const CPQPubKey pubkey(pubkeyBytes);
    std::vector<unsigned char> legacyHashInput(pubkey.begin(), pubkey.end());
    legacyHashInput.insert(legacyHashInput.end(), legacySecret.begin(), legacySecret.end());
    BOOST_CHECK(Hash(legacyHashInput.begin(), legacyHashInput.end()) ==
                Hash(pubkey.begin(), pubkey.end(), secureSecret.begin(), secureSecret.end()));
}

BOOST_AUTO_TEST_CASE(rip25_v48_consensus_constants_and_deployment_bits)
{
    std::unique_ptr<CChainParams> mainParams = CreateChainParams("main");
    BOOST_REQUIRE(mainParams);
    const Consensus::Params& consensus = mainParams->GetConsensus();

    // Ravencoin 4.8.0 owns bit 11; RIP-25 moves only its signaling bit to 12.
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].bit, 11);
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_PQ_HYBRID].bit, 12);
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].nOverrideRuleChangeActivationThreshold, 1411);
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].nOverrideMinerConfirmationWindow, 2016);
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_PQ_HYBRID].nOverrideRuleChangeActivationThreshold, 1714);
    BOOST_CHECK_EQUAL(consensus.vDeployments[Consensus::DEPLOYMENT_PQ_HYBRID].nOverrideMinerConfirmationWindow, 2016);

    // August-2026 forged header-height protection must remain present.
    BOOST_CHECK_EQUAL(consensus.nHeightHeaderCheckActivation, 4487776);

    const auto checkpoint = mainParams->Checkpoints().mapCheckpoints.find(4487775);
    BOOST_REQUIRE(checkpoint != mainParams->Checkpoints().mapCheckpoints.end());
    BOOST_CHECK(checkpoint->second == uint256S("0x000000000002d64509e06e76ddbbe418c725291687ec62b41ecfc40386a091fd"));

    // Approved RIP-25 resource policy is an invariant of the 4.8 port.
    BOOST_CHECK_EQUAL(MAX_BLOCK_WEIGHT_RIP2, 8000000u);
    BOOST_CHECK_EQUAL(MAX_BLOCK_WEIGHT_RIP25_PHASE1, 12000000u);
    BOOST_CHECK_EQUAL(MAX_BLOCK_WEIGHT_RIP25_PHASE2, 16000000u);
    BOOST_CHECK_EQUAL(PQ_WITNESS_SCALE_FACTOR, 8);
}

BOOST_AUTO_TEST_CASE(rip25_network_signature_contexts_are_canonical)
{
    const Consensus::PQSignatureContext& mainContext = NetworkContext("main");
    const Consensus::PQSignatureContext& testContext = NetworkContext("test");
    const Consensus::PQSignatureContext& regtestContext = NetworkContext("regtest");

    BOOST_REQUIRE(Consensus::IsValidPQSignatureContext(mainContext));
    BOOST_REQUIRE(Consensus::IsValidPQSignatureContext(testContext));
    BOOST_REQUIRE(Consensus::IsValidPQSignatureContext(regtestContext));
    BOOST_CHECK(mainContext != testContext);
    BOOST_CHECK(mainContext != regtestContext);
    BOOST_CHECK(testContext != regtestContext);

    BOOST_CHECK_EQUAL(std::string(mainContext.begin(), mainContext.end()),
        "RVN/ML-DSA-44/v1/0000006b444bc2f2ffe627be9d9e7e7a0730000870ef6eb6da46c8eae389df90");
    BOOST_CHECK_EQUAL(std::string(testContext.begin(), testContext.end()),
        "RVN/ML-DSA-44/v1/000000ecfc5e6324a079542221d00e10362bdc894d56500c414060eea8a3ad5a");
    BOOST_CHECK_EQUAL(std::string(regtestContext.begin(), regtestContext.end()),
        "RVN/ML-DSA-44/v1/0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c");
}

BOOST_AUTO_TEST_CASE(rip25_block_weight_phase_boundaries)
{
    std::unique_ptr<CChainParams> mainParams = CreateChainParams("main");
    std::unique_ptr<CChainParams> regtestParams = CreateChainParams("regtest");
    BOOST_REQUIRE(mainParams);
    BOOST_REQUIRE(regtestParams);

    // Mainnet is not force-enabled: with no active chain state the RIP-2 ceiling remains 8 MWU.
    BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(nullptr, mainParams->GetConsensus()), MAX_BLOCK_WEIGHT_RIP2);
    BOOST_CHECK_EQUAL(GetMaxBlockSerializedSizeForPrev(nullptr, mainParams->GetConsensus()), MAX_BLOCK_SERIALIZED_SIZE_RIP2);

    const Consensus::Params& regtest = regtestParams->GetConsensus();
    BOOST_REQUIRE(regtest.nPQHybridEnabled);
    BOOST_REQUIRE(regtest.nPowTargetSpacing > 0);

    const int64_t blocksPerYear = (365LL * 24 * 60 * 60) / regtest.nPowTargetSpacing;
    BOOST_REQUIRE(blocksPerYear > 1);

    CBlockIndex prev;
    prev.nHeight = 0;
    BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(&prev, regtest), MAX_BLOCK_WEIGHT_RIP25_PHASE1);
    BOOST_CHECK_EQUAL(GetMaxBlockSerializedSizeForPrev(&prev, regtest), MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE1);

    // Candidate height activation + blocksPerYear - 1 is still Phase 1.
    prev.nHeight = static_cast<int>(blocksPerYear - 2);
    BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(&prev, regtest), MAX_BLOCK_WEIGHT_RIP25_PHASE1);
    BOOST_CHECK_EQUAL(GetMaxBlockSerializedSizeForPrev(&prev, regtest), MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE1);

    // Candidate height activation + blocksPerYear is the first Phase-2 block.
    prev.nHeight = static_cast<int>(blocksPerYear - 1);
    BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(&prev, regtest), MAX_BLOCK_WEIGHT_RIP25_PHASE2);
    BOOST_CHECK_EQUAL(GetMaxBlockSerializedSizeForPrev(&prev, regtest), MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE2);
}

BOOST_AUTO_TEST_CASE(rip25_approved_pq_witness_discount_accounting)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vin[0].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x11);
    mtx.vin[0].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x22);

    const CTransaction tx(mtx);
    const int64_t standardWeight = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1)
                                 + ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    const int64_t pqBytes = mldsa::SIGNATURE_BYTES + mldsa::PUBLICKEY_BYTES;
    const int64_t expectedDiscount = pqBytes * (PQ_WITNESS_SCALE_FACTOR - WITNESS_SCALE_FACTOR) / PQ_WITNESS_SCALE_FACTOR;

    BOOST_CHECK_EQUAL(GetPQWitnessInputDiscount(tx.vin[0]), expectedDiscount);
    BOOST_CHECK_EQUAL(GetTransactionWeight(tx), standardWeight - expectedDiscount);

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(mtx));
    BOOST_CHECK_EQUAL(GetBlockWeightRIP25(block), GetBlockWeight(block) - expectedDiscount);

    // Shape mismatch must not receive the approved PQ discount.
    CMutableTransaction malformed = mtx;
    malformed.vin[0].scriptWitness.stack[0].resize(mldsa::SIGNATURE_BYTES - 1);
    const CTransaction malformedTx(malformed);
    const int64_t malformedStandardWeight = ::GetSerializeSize(malformedTx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1)
                                          + ::GetSerializeSize(malformedTx, SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK_EQUAL(GetPQWitnessInputDiscount(malformedTx.vin[0]), 0);
    BOOST_CHECK_EQUAL(GetTransactionWeight(malformedTx), malformedStandardWeight);
}

BOOST_AUTO_TEST_CASE(mldsa_rejects_null_inputs)
{
    unsigned char seed[mldsa::SEED_BYTES];
    std::memset(seed, 0x42, sizeof(seed));

    unsigned char pk[mldsa::PUBLICKEY_BYTES];
    unsigned char sk[mldsa::SECRETKEY_BYTES];
    unsigned char sig[mldsa::SIGNATURE_BYTES];
    size_t siglen = 0;
    const unsigned char msg[] = "RIP-25 null input regression";

    std::memset(pk, 0x5a, sizeof(pk));
    std::memset(sk, 0x5a, sizeof(sk));
    BOOST_CHECK(!mldsa::KeyGen(nullptr, sk, seed));
    BOOST_CHECK(std::all_of(std::begin(sk), std::end(sk), [](unsigned char byte) { return byte == 0; }));

    std::memset(pk, 0x5a, sizeof(pk));
    BOOST_CHECK(!mldsa::KeyGen(pk, nullptr, seed));
    BOOST_CHECK(std::all_of(std::begin(pk), std::end(pk), [](unsigned char byte) { return byte == 0; }));

    std::memset(pk, 0x5a, sizeof(pk));
    std::memset(sk, 0x5a, sizeof(sk));
    BOOST_CHECK(!mldsa::KeyGen(pk, sk, nullptr));
    BOOST_CHECK(std::all_of(std::begin(pk), std::end(pk), [](unsigned char byte) { return byte == 0; }));
    BOOST_CHECK(std::all_of(std::begin(sk), std::end(sk), [](unsigned char byte) { return byte == 0; }));

    std::memset(sk, 0x5a, sizeof(sk));
    BOOST_CHECK(!mldsa::KeyGenRandom(nullptr, sk));
    BOOST_CHECK(std::all_of(std::begin(sk), std::end(sk), [](unsigned char byte) { return byte == 0; }));

    std::memset(pk, 0x5a, sizeof(pk));
    BOOST_CHECK(!mldsa::KeyGenRandom(pk, nullptr));
    BOOST_CHECK(std::all_of(std::begin(pk), std::end(pk), [](unsigned char byte) { return byte == 0; }));

    BOOST_REQUIRE(mldsa::KeyGen(pk, sk, seed));
    siglen = 123;
    BOOST_CHECK(!mldsa::Sign(nullptr, &siglen, msg, sizeof(msg) - 1,
                             TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, sk));
    BOOST_CHECK_EQUAL(siglen, 0U);
    BOOST_CHECK(!mldsa::Sign(sig, nullptr, msg, sizeof(msg) - 1,
                             TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, sk));
    BOOST_CHECK(!mldsa::Sign(sig, &siglen, nullptr, sizeof(msg) - 1,
                             TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, sk));
    BOOST_CHECK(!mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1,
                             nullptr, sizeof(TEST_CONTEXT) - 1, sk));
    BOOST_CHECK(!mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1,
                             TEST_CONTEXT, 0, sk));
    BOOST_CHECK(!mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1,
                             TEST_CONTEXT, mldsa::MAX_CONTEXT_BYTES + 1, sk));
    BOOST_CHECK(!mldsa::Sign(sig, &siglen, msg, sizeof(msg) - 1,
                             TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, nullptr));
    BOOST_CHECK(!mldsa::Verify(nullptr, mldsa::SIGNATURE_BYTES, msg, sizeof(msg) - 1,
                               TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, pk));
    BOOST_CHECK(!mldsa::Verify(sig, mldsa::SIGNATURE_BYTES, nullptr, sizeof(msg) - 1,
                               TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, pk));
    BOOST_CHECK(!mldsa::Verify(sig, mldsa::SIGNATURE_BYTES, msg, sizeof(msg) - 1,
                               nullptr, sizeof(TEST_CONTEXT) - 1, pk));
    BOOST_CHECK(!mldsa::Verify(sig, mldsa::SIGNATURE_BYTES, msg, sizeof(msg) - 1,
                               TEST_CONTEXT, 0, pk));
    BOOST_CHECK(!mldsa::Verify(sig, mldsa::SIGNATURE_BYTES, msg, sizeof(msg) - 1,
                               TEST_CONTEXT, mldsa::MAX_CONTEXT_BYTES + 1, pk));
    BOOST_CHECK(!mldsa::Verify(sig, mldsa::SIGNATURE_BYTES, msg, sizeof(msg) - 1,
                               TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1, nullptr));
}

BOOST_AUTO_TEST_CASE(deterministic_keygen_does_not_depend_on_global_rng)
{
    unsigned char seed[mldsa::SEED_BYTES];
    std::memset(seed, 0x5a, sizeof(seed));

    unsigned char pk1[mldsa::PUBLICKEY_BYTES], sk1[mldsa::SECRETKEY_BYTES];
    unsigned char pk2[mldsa::PUBLICKEY_BYTES], sk2[mldsa::SECRETKEY_BYTES];
    unsigned char randomPk[mldsa::PUBLICKEY_BYTES], randomSk[mldsa::SECRETKEY_BYTES];

    BOOST_REQUIRE(mldsa::KeyGen(pk1, sk1, seed));
    BOOST_REQUIRE(mldsa::KeyGenRandom(randomPk, randomSk));
    BOOST_REQUIRE(mldsa::KeyGen(pk2, sk2, seed));

    BOOST_CHECK(std::memcmp(pk1, pk2, mldsa::PUBLICKEY_BYTES) == 0);
    BOOST_CHECK(std::memcmp(sk1, sk2, mldsa::SECRETKEY_BYTES) == 0);
}

BOOST_AUTO_TEST_CASE(mldsa_backend_compatibility_kat)
{
    std::array<std::array<unsigned char, mldsa::SEED_BYTES>, 4> seeds{};
    seeds[1].fill(0xff);
    for (size_t i = 0; i < seeds[2].size(); ++i)
        seeds[2][i] = static_cast<unsigned char>(i);
    const std::array<unsigned char, mldsa::SEED_BYTES> walletLikeSeed{{
        0x0c, 0x7e, 0x4e, 0x8f, 0x2d, 0x85, 0x6a, 0x97,
        0x41, 0x73, 0x3b, 0x1f, 0x9b, 0x2b, 0x8d, 0x44,
        0x31, 0xd5, 0x97, 0xee, 0x36, 0xf3, 0x7c, 0x91,
        0xf6, 0x21, 0x0f, 0x74, 0xd7, 0x90, 0x5a, 0x2c
    }};
    seeds[3] = walletLikeSeed;

    static const char* expectedPublicKeyHashes[] = {
        "eb4e7302842153b0fa19e8620739ad258af4929c26dd89079a7ec7d4282208e1",
        "62c4f1b3164db7fa896a3343e900eb3e13c9f76de122020feba37ee063d49ef0",
        "9f107644c1084526af3bc8098680b05499a2325a644e388fb4f970e058d19d46",
        "0d2697f8bb6693644aa76ed6aab823c3b89ae28ab4241dd25ba147289c9476b4"
    };
    static const char* expectedSecretKeyHashes[] = {
        "0f9086044d77b6d610c7e92418d9f70a398c69febc7e99f8254aaea98dcfbe77",
        "6433074c5ffc9e0f2b1d68bb3fda84e439da0a2d93f508a101e9b44835f0b22c",
        "04bf6b9f579166a627961dfc5c3bf9717df868db88863856356c4668c8b56b0b",
        "9c9754163be250124d49606b6d5fa2c4a633038792149870a08e7e4f4894bdac"
    };

    std::array<unsigned char, mldsa::PUBLICKEY_BYTES> publicKey{};
    std::array<unsigned char, mldsa::SECRETKEY_BYTES> secretKey{};
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> digest{};
    for (size_t i = 0; i < seeds.size(); ++i) {
        BOOST_REQUIRE(mldsa::KeyGen(publicKey.data(), secretKey.data(), seeds[i].data()));

        CSHA256().Write(publicKey.data(), publicKey.size()).Finalize(digest.data());
        BOOST_CHECK_EQUAL(HexStr(digest.begin(), digest.end()), expectedPublicKeyHashes[i]);

        CSHA256().Write(secretKey.data(), secretKey.size()).Finalize(digest.data());
        BOOST_CHECK_EQUAL(HexStr(digest.begin(), digest.end()), expectedSecretKeyHashes[i]);
        memory_cleanse(secretKey.data(), secretKey.size());
    }
}

BOOST_AUTO_TEST_CASE(mldsa_acvp_keygen_kat)
{
    // FIPS 204 keyGen test group 1, case 1 from the liboqs 0.12.0 ACVP
    // internalProjection.json. These expected values come from the published
    // vector, not from the key-generation result under test.
    const std::vector<unsigned char> seed = ParseHex(
        "93EF2E6EF1FB08999D142ABE0295482370D3F43BDB254A78E2B0D5168ECA065F");
    BOOST_REQUIRE_EQUAL(seed.size(), mldsa::SEED_BYTES);

    std::array<unsigned char, mldsa::PUBLICKEY_BYTES> publicKey{};
    std::array<unsigned char, mldsa::SECRETKEY_BYTES> secretKey{};
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> digest{};
    BOOST_REQUIRE(mldsa::KeyGen(publicKey.data(), secretKey.data(), seed.data()));

    CSHA256().Write(publicKey.data(), publicKey.size()).Finalize(digest.data());
    BOOST_CHECK_EQUAL(HexStr(digest.begin(), digest.end()),
        "6995b20ecd5cde41719035028a712ccf35b1adf53b913030423d9d6fa188d673");
    CSHA256().Write(secretKey.data(), secretKey.size()).Finalize(digest.data());
    BOOST_CHECK_EQUAL(HexStr(digest.begin(), digest.end()),
        "16a35d4b59f932aeada987dc689b075add0df57b4815bb103be7443ee3c1c561");
    memory_cleanse(secretKey.data(), secretKey.size());
}

BOOST_AUTO_TEST_CASE(mldsa_acvp_deterministic_signature_bytes_kat)
{
    // FIPS 204 ML-DSA-sigGen, liboqs 0.12.0 ACVP internalProjection.json,
    // test group 1, case 4. This vector fixes every byte of the output.
    // The internal API takes an empty prefix and zero randomness for this case.
    std::vector<unsigned char> secretKey = ParseHex(
        "abcec4a46e695fc6ebe64a191389f0d0ae180f911d5b824f4ed9111728ff4f9493ef3a7512dacf766d576898d33c4c8f"
        "4001b777ee5ec2e2dc1a8e3e181b43418af45100b92a3835d02b9892e609b2aa8c6af7661ce0bc8362ae0da172a79e84"
        "ff4cdad8607e4924ff41db6ec28dccd09b8d1f5657ba17c848babc71bb242a50c44069a0200101914d130822c1142884"
        "36508b0422d83004e23848032532ca902509a0009a848440004cc3304d04c0601cc84889340950b68918c81103410d63"
        "1870db444ae4224d8b96491b0224c2802904365290222d424626e0060613b12de1187203b18442228144027159186c22"
        "092c012751083160dc48061045089ac444601609240224da006623945149c200182232ca462a0ba16d22b2215028700a"
        "c108d1828c00b24014083221b5810c4501240225e44291984622c90826532650dba8495b226294b049603050242382dc"
        "0429d2260254262cc3a868001546089168cc067184b86c518285da221110402494149044b8102220299ab40190824d52"
        "3050229668101072913411ccc625d2880d1b1951c3888c222509da20322003209106028c188000b02d92421111b92da1"
        "0890a228700b4464a1b01153966800b925423860884688630800c3c2910ba9511a1902c2b229532052d9906dd9c08019"
        "2400249589e004914180600022440908012435624c48864bb46123c900588869c1001251248960b64844322513c5318b"
        "909118a8691a850449b800a3b02598a2052293455342701b3722590860911248939221d9024e20426014394109373183"
        "46851a836d192751c93686111749a0b4649bb285ccc049c0c4611a066203c304a032248cb861a34408cb485248486624"
        "4610a1068909280c01810c028531232102e4442c0a988c19c54192187223b168d844862295405824408c322222c27110"
        "170a910668d438529b025212086458185208078dd388240c1092c2c60d549640031989093530988425d104524c146e62"
        "a06c5aa004cc126ac3284490268a43468021454460b249a390681a4745d2965188220e6032249aa6280a034a9cb6888b"
        "180cc1021012b70d0340480cb3444c0010011549e2b23094064504378441b2401aa3884aa885112229d2402550b0048c"
        "2621180872912851481842499020c9126d1a1289a4404c4a82710301228920449c48624006920813819ca670d0c808d9"
        "38721a118a1bb589111150c3c6695386851a0204d2426d19116419a38152c861403a93cb8575520d2a3a7317cae1963e"
        "2705b7596c8e5dbf0daeaf8755df38a5df16297cfc84097b480d9729e4cc62170739a1a8a2057ea7fefb06275344adb6"
        "934e1c2da7e7f3e831fa35e6a4b8d8def435235ce957df5fa1d842962711443beade91070833c84264b45e2380b09420"
        "2e079a0a7c6058a54e6f552f202760230f6d95f5ea873709be4d7603ac010cfbadafe229cada1f2bc717f877856d8b93"
        "0d0e215c4ba2212d66e21a2d1f09b1f1a9bc8c298cfd65b318fe91847279f204201203e0922e82bd298d9bf18b8fbcf7"
        "2070f7c7c51d5480e60674341cf263fd179862f37d5665fe35ed0b2a86b7115c90093f5785309cd56c48bbc50570a0c2"
        "d066bd0eccd3c86e2a6c8b098afd9c0e235ceb920d58f0b913bfb633bfe21bb1668d9c45638f5ce9650caaa83db2d9b4"
        "b24b1f518b19226abda06239698a90f30a50af69ae1d20fa00e3d88ff6f2c2466e45a39a1c946fb695888383cb6a59a7"
        "c8395082134a82dea3da7fe6d9e6f76c7e86a50ca04990c70dd5f9af062ed14cc661f453bf309da08056e19f2f7b34a1"
        "5235230c15ec6859d7dcf0ed892ddff4e5096b36b406a10cb35aa81f72827c5982e3c5bfbb989e062cb4a7f0f76b008a"
        "b8ca5ec1cebdabcaa1e97809b44c5f49281415337978184811adb8131d2dfa2477d27532e92409493d46c597a6886250"
        "593fd58d305d0760ce8f772337d42b0f7dbfc483a941e8cdf32ce3e97309c3c404b6e4101678f123438853fc8a71c835"
        "d1ad0c7712460dbe83c1abc6bb0834c0271a6627e7dcb93efc25f78417bbc801488e5a051455343757f6bfaf923867c4"
        "5ed5bf37304b11e012ee63a3b8d84dce7a15d5ab940d87fe1181eaba3c97bca702f5de4df74848a99d2b1f34fe2b0363"
        "3d6aac900a09c278556172df5d9cde361a8acd779465cbaf50de5c8f4ca0d15df3f74347c6ddf7d9b3e3e5e197bfe0aa"
        "170949fb42b78364a72b1b106156ec09a6e4ec72f3f814781d3ce7b7aa2b02e49caa25ac36daade0fd570a61589553a0"
        "cae582ba2894c82c0380a713b0b74924e006a6b341f21ae2aaec2016d2687f1ab696337f5a268b3b6f3730f507d6122d"
        "c92cab36107e864bb3edeea6fc1c5309a9b51582cdfcc1a899929ad7cdbecbcbf9d38121d58c3b3e6d9be001b117e4a7"
        "f762816174b761efcf291a1cc5da354029962eb8b0f6166a9c9eaf26921d1777e621c50c41614300605b1ee2a0cc41bc"
        "666ce90a15733c69a82451fd41f23efaa73a2482c4e3d476ccbbfb59b25140ffa0c1ceceabd3b036f2611c83aa834e6c"
        "fe03963941bdad4aefb11d01ec43293bcd22ecb8784ef5ce2a6042f200f1b9d6c595ef920c3cacd1c1c3cb61b6b46454"
        "a3b28a472ae0203038a5602cb9b001620c98bc09bc2db5621c8db085d88561058aea691afd199c6d4bb1511137ed2800"
        "722a81670b44fac51dfa6683675bf34c52f6eb7eba35d22c907a207ae5ce6c3c40ab0a26b88dae777e10b4fc33ab38c3"
        "08ca2532032a7f306e9eca723b58119c3be662817a1eab6069fa05c3b0ed31060d5794121a83fbb152c7fc05bb753c9d"
        "29bc329e745d7c7d493372c26c0336aab37b884fb41741b344ee4d247d6b5d049e5e322cc97ec6647ec7551824c6aa9c"
        "d249f49fe1652ecfd01c3e7eb026fdce7320d21ec9e4d460e50d6440c7364a15ae3c107ce8ee1e8a33ebc9d2b5585b8f"
        "69771f687eb6940c21f45750079d68d3dd6ce1cd7ca8d91a64d093a25a96628169b675cda9f14fada4af3d11b6524465"
        "b89dd4ef93a9a159f8df2a134fea301110eb77e1eca51166d26cb036bf92f1655167bd32d12be04a91ff0b3c52c69de3"
        "76856fab9b4e14524e5858717ecbd0865719bf1ddeddf8cc141396f9f0b4ed38b0caada08b64451ad8bd38557660cfef"
        "46ec0059b4aea6a7534a3db767c537e60210a1af84ec939413fd7ebbf14fe96d6ee82a0c632edc63715c0c6654aa4fe2"
        "98f43ed5b47af7350c32c8d7f696f9a96b81e9832f486a66d9b304a6531139561fe5a967061bdffa4793ea986c3a2693"
        "c21dad4428fe98f168eb928fcbeb8fe0a611049c1f430ccd80f9181d276afedaea40261fe1b038f5677ad507eb48b976"
        "8964baec928197ac26acb1a89cddaf51c4336b6f49985c13926e76aa7d69f5f0844f23e7b2b977565587718903b39173"
        "f7f18af84264370bd61020f2a76ed281687419e334443159bf6a21533f41e030654f6876aabb21025b6d2312304ff8be"
        "ffb7ad2e225bf79f1b6f8c33aa90d9dc18b369846fa06548e72efb2ec4fd6bd833f1872df9659f62af040345cb5b8399"
        "a4836f7f5a9f920f0484009c1f6871d2");
    const std::vector<unsigned char> message = ParseHex(
        "22aa98c685e1552b525b4302c943037f668279c224b6270dcaf2b06c4f4ab1254c48de253829fe6dffa9cb6bb294f054"
        "711bae3fbacfb900cfd1f0844e55d51ec6f697b998759b14c13392ddb6f7deba77ffc22468781ce402");
    const std::vector<unsigned char> expectedSignature = ParseHex(
        "0313b5dd1e344bf95eb5d825837f570922337633494f8f2ee3618aeb906df766f5addccb8ec1c5cc51aae0b9fad876fc"
        "ff5d6814357e8c63fe59fd72db3ce4d1650660a0cf64339d8b7c6ddb7a7a3c8ec6c88f2f0f70b377d9d5e21f0d40e175"
        "73f07c94cdf844cb6794ce2cb8e4a11254ff0012d562dcf4d9fbbd18348d13e1b69395c69dccbf618d7a38ea6a51b9f1"
        "f8cc70cd36d087682a558a1ea9bf9de6ace5b350b6eb977f435cc46fd43f155b29822d68d1ca9603ba43f1db28bf4d3c"
        "e284523bb440d1e2c124cf92dc74f19b74cbc177de9b08377b0e363401d23e97ae967e13ab77e9b7c7488e49b45e8351"
        "36ec2874a24cc79059de5b0a67c842f2e09b37b3039dc186427f760ce60790a9fb58aa388880ec720a783efeb4e11abd"
        "817a7ea884e5efc279ad9e8ac2c58a41020c7468a749933348ebf78a0f0597b29b00a126050fa40f532b0f4411b733ba"
        "a5c80ca1b52828cb236268989ea2641431a359e6595f49b4feabfa85ed4ba85443e2d7b82dc7523ff627d336da155189"
        "7438100c20f0184a963d0b9af7f7cc9fd64a2878d8fc5ade2d0d928445eebed286f2b280af573ee62e70f698ba0c1438"
        "1a5b4e001c53c49a77e9f181ec65b49e6cd8bbe30b5f309686f7dc80176f654771df17fdf136d6c05498cc00c476e716"
        "08a39cb29a19a986297cb9c1e3ffb4ba08ed42ad12908287f018d49a0e1ae89a9bb3f932df98ca91e691ab5f3bfadea9"
        "a233350a9ede63cfc45427cad109468fce4abf7fdb83c6ec83865134380d3ffca4d94c5604dd9daffac204c14009fe06"
        "311b81310940c43fbfcf049f99f5deea649a333f6a3ae6232ec7cde62c95338c23085d776b9b8f4454e03bd0fb280466"
        "18c33a7ccb409be7bcbc0906b5a9424b41998a3a1e65e5ec667a14339e3bb44354047a2868d64b78fe5faedddffde497"
        "368c3cc130d0821c1deb2c43119195c6c4cff1d5a778965ff97685dd93775837ea0852284c670dd418dc9fdc9c44db7e"
        "da60e58030d8fbdf86d48b98230e6ad19e97d21d7358510e20ee374b8486341302efa590b96c1dc253226a12cafdb3b4"
        "a31ee2270f764b74f17bf9c5681b6578777739ff767d4e094ce80ad9804261767097bdb163430332c428cb3ccd9aa881"
        "316b51556c718a95816797cd5c8a2a640b85dfda520d1a0129f0e9de772724ee8b74b4df6793ebc0a801b6f77a2647ca"
        "2a5f564d336bc23ecf591327a8dc4214953d99ea3a9ed9943f07809b4c36a8bff093721949a2aef2cdd9687934849ca3"
        "5ba1f38743ac663e48b39a2a32122b6951cf6f1be17fa96628d8dee8760cf7f2699045df30484722601e7a45deb277cb"
        "eb749f5cb4383c8643241158ef13d8723b9b00c98f8bc68c5f42dfc4a1b8d95f4652aef67798a8b3c0c2ad40cb7a2b88"
        "a23650147e32cb605324a99370bdb640edac9ce57ec22689dd45d1922811584b01685615520dbb0ffbb028e43b77b54a"
        "f4a3ebc6b9f857e9100fd9c64028867999bb9c7ff05d37704d41c94cd63dfd1a7673f817931a4f2c42ce572b24fb5e77"
        "9897fa4e6e2191524783c710a099d1a6bb9eae47e849187432a2039bbe1d37d0e7c797907f99bdb8e7fb33c3b2fc9e06"
        "708312beeef2935d443f29b646fb5ad52c99eb71559571781a4c86250d262329ff3daaca18a5e7b77c3ce593cf3a92b1"
        "2b9ae86ca3af05ebbbd75b6befc368982d96f2e5fbc128b6a8d65fdb0b0f7859689f0b6f0a3d1efcbf1915dc61c7feeb"
        "d077f71122e77775b4279f4f57aba521be4682daa5b836b5b4d44cdb726dff3ac9cdda9f0d1cf4e79bc9e90dff7562cf"
        "206e756c38e708bd30820a14c595288730f42c8ed99a7c1f2fa6ca9759cd31b8f28fd35b9e0980acc3432f462cd9cb4c"
        "594ab63fd250ddd4877269056677ea7e3b0aacd4757aeba398688423015d952d0cf67e5027ba4b6b376f6380b5823717"
        "d64513efb35015fbd20fb2284e1e340c9b94f87cfba24310325d7597ab34baca6753215ab994390c2e5a82eeb06f3382"
        "93e72c5d0ff786666ff2ee2d429461f1112551ad0928844d7698d3ab0c4054881a623525d728e514e12550aab227389e"
        "5f0c2341a6aa34bab5472bc9e465dc24e9910175b2b968f2e7883cd22cc07b3d203cfdcdd877cbb28368dbe668f561e3"
        "fa1c5d3391d4a408a71e0bf0c32c1fd6494def7678e58b829ab428509c33f2491725959485928b2e7fa96ec7aff12ba3"
        "196087d3c83dc242e5ec95bf95c8581693fc0b744e758e1e85959ab63e9b4b0a4547be5abd7d29bf9da2d192b4bca491"
        "d1dc856ec80ad6c3c738fb775f95a217d76093852aee0cc203db1316c6253bb75ce2d92906b47a5b733e4a8b0c28b128"
        "3419249b5a05f6302ce110f85790eb46ded7109b0dac58a4d25255b6950f3b2c3421140e4825b6ac968091ea7ae9790b"
        "c8d03f3f8f4c1bf1006e821e5da2c65c542183f4230cec934acb81fea94d959fb6a21c7ab20652c9b247edef72674fe9"
        "15858795a9a00564f602136c364a6d9638cb1bbd8a705b90429d13a19de93520229282d5122ee112a32a68e23577e358"
        "fb28ec45478d05f64513a9e814a5f1fc7cde4d03ec51ed41d29abbc0f60292e2f7d0aa4490c38b583329e2d5a1e61c73"
        "d6033cb73a7a4f75abbb29a31e52cd1eedc3dc4f0b9152fae6b29dd1f9739456ac5b1890b3bb00415027c634f1e6374f"
        "310c95e680086862b8bdea0833997746814fc602f97999a2309d4cc05362303f72bbe2eead04d566579a00a8265e238f"
        "c578fdb2a63c57250b5714f2c6485bc6247752853f75abf066637645a5a6ac0e85459ec2e24d704ed7da4ba0fd23748d"
        "1d853bcf84d4d00eb4b6da6d036b8f29797882ef5400f2f7d6efcda0418cbc5752ae5431b59d2d518fd752467b7ee13f"
        "699cee1581aa1a305b2adaca12647fa8ff00b427f28aa33fc094a374521437e3b4a109113e1ad8ad23e2aebd69edeced"
        "dfef0b5aa0a484ae32bfb2b05f8f7f65077f5b35c99b61ec28060437d9cb982afa28ffe8ad4c02bfbfb071ca8e9eea15"
        "cceed97d47be2313e3c344793d40b45f709c8a2c639c09662a81f117839ab62135b6715fd57f7b0368c6ee1e3234655e"
        "ae94213d24d92989a46cbd5c7ef116243a085462c5454949d41d2d570ac810b54c727fa5e187d0d9f7fa7db3682d9946"
        "0f5077aed0a90d513c0d833e5f5a91a5a26cf47ad134c074d658ac0164acbaae8fae161e00c7f8b043fff46fc5c900c9"
        "ed14c44c98addedcd5f3bb7038868f8460ee12472de2a24bcd0a611d9e039d60000d131d242e3c436e808fcdd3e2f556"
        "67698d91929da3a4f0f72d4b58636c6d9cc9d9eff0141c233b3f4a53595f696b6f848ac2f7fe00000000000000000000"
        "000000000000000000000000000000000f1a2536");
    BOOST_REQUIRE_EQUAL(secretKey.size(), mldsa::SECRETKEY_BYTES);
    BOOST_REQUIRE_EQUAL(message.size(), 89U);
    BOOST_REQUIRE_EQUAL(expectedSignature.size(), mldsa::SIGNATURE_BYTES);

    const std::array<unsigned char, 32> zeroRandomness{};
    std::array<unsigned char, mldsa::SIGNATURE_BYTES> signature{};
    size_t signatureLength = 0;
    BOOST_REQUIRE_EQUAL(PQCP_MLDSA_NATIVE_MLDSA44_C_signature_internal(
        signature.data(), &signatureLength, message.data(), message.size(),
        nullptr, 0, zeroRandomness.data(), secretKey.data(), 0), 0);
    BOOST_REQUIRE_EQUAL(signatureLength, mldsa::SIGNATURE_BYTES);
    BOOST_CHECK(std::equal(signature.begin(), signature.end(),
                           expectedSignature.begin()));
    memory_cleanse(secretKey.data(), secretKey.size());
    memory_cleanse(signature.data(), signature.size());
}

BOOST_AUTO_TEST_CASE(secret_public_key_binding)
{
    CPQKey key1;
    CPQKey key2;
    key1.MakeNewKey();
    key2.MakeNewKey();
    BOOST_REQUIRE(key1.IsValid());
    BOOST_REQUIRE(key2.IsValid());

    const CPQPubKey pub1 = key1.GetPubKey();
    const CPQPubKey pub2 = key2.GetPubKey();

    BOOST_CHECK(key1.MatchesPubKey(pub1));
    BOOST_CHECK(!key1.MatchesPubKey(pub2));
    BOOST_CHECK(key2.MatchesPubKey(pub2));
    BOOST_CHECK(!key2.MatchesPubKey(pub1));
}

BOOST_AUTO_TEST_CASE(import_matching_secret_public_key_pair)
{
    CPQKey source;
    source.MakeNewKey();
    BOOST_REQUIRE(source.IsValid());

    const CPQPubKey expectedPub = source.GetPubKey();
    const auto& secret = source.GetKeyData();
    CPQKey::KeyData raw(secret.begin(), secret.end());

    CPQKey imported;
    BOOST_REQUIRE(imported.SetKeyData(raw, expectedPub));
    BOOST_CHECK(imported.IsValid());
    BOOST_CHECK(imported.GetPubKey() == expectedPub);
    BOOST_CHECK(imported.MatchesPubKey(expectedPub));
}

BOOST_AUTO_TEST_CASE(import_rejects_mismatched_public_key_and_invalidates_key)
{
    CPQKey source;
    CPQKey other;
    source.MakeNewKey();
    other.MakeNewKey();
    BOOST_REQUIRE(source.IsValid());
    BOOST_REQUIRE(other.IsValid());

    const auto& secret = source.GetKeyData();
    CPQKey::KeyData raw(secret.begin(), secret.end());
    const CPQPubKey wrongPub = other.GetPubKey();

    CPQKey imported;
    BOOST_CHECK(!imported.SetKeyData(raw, wrongPub));
    BOOST_CHECK(!imported.IsValid());
    BOOST_CHECK(!imported.GetPubKey().IsValid());
    BOOST_CHECK(std::all_of(imported.GetKeyData().begin(), imported.GetKeyData().end(),
                            [](unsigned char byte) { return byte == 0; }));

    uint256 hash;
    std::memset(hash.begin(), 0xa5, 32);
    std::vector<unsigned char> signature;
    BOOST_CHECK(!imported.Sign(hash, signature,
                               TEST_CONTEXT, sizeof(TEST_CONTEXT) - 1));
}

BOOST_AUTO_TEST_CASE(import_rejects_wrong_secret_size)
{
    CPQKey key;
    CPQKey pubSource;
    pubSource.MakeNewKey();
    BOOST_REQUIRE(pubSource.IsValid());

    CPQKey::KeyData tooShort(mldsa::SECRETKEY_BYTES - 1, 0);
    CPQKey::KeyData tooLong(mldsa::SECRETKEY_BYTES + 1, 0);

    BOOST_CHECK(!key.SetKeyData(tooShort, pubSource.GetPubKey()));
    BOOST_CHECK(!key.IsValid());
    BOOST_CHECK(!key.SetKeyData(tooLong, pubSource.GetPubKey()));
    BOOST_CHECK(!key.IsValid());
}

BOOST_AUTO_TEST_CASE(failed_reinitialization_cleanses_prior_secret)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(std::any_of(key.GetKeyData().begin(), key.GetKeyData().end(),
                              [](unsigned char byte) { return byte != 0; }));

    BOOST_CHECK(!key.SetSeed(nullptr));
    BOOST_CHECK(!key.IsValid());
    BOOST_CHECK(!key.GetPubKey().IsValid());
    BOOST_CHECK(std::all_of(key.GetKeyData().begin(), key.GetKeyData().end(),
                            [](unsigned char byte) { return byte == 0; }));
}

BOOST_AUTO_TEST_CASE(witness_v2_active_rules_accept_valid_and_reject_invalid_mldsa)
{
    const Consensus::PQSignatureContext& context = NetworkContext("main");
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();

    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));

    const CAmount amount = 10 * COIN;
    CMutableTransaction funding;
    funding.vout.emplace_back(amount, GetScriptForWitnessV2PQ(witnessProgram));
    const CTransaction fundingTx(funding);

    CMutableTransaction spend;
    spend.vin.emplace_back(COutPoint(fundingTx.GetHash(), 0));
    spend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
    BOOST_REQUIRE(SignSignature(keystore, fundingTx, spend, 0, SIGHASH_ALL, context));
    BOOST_REQUIRE_EQUAL(spend.vin[0].scriptWitness.stack.size(), 2U);
    BOOST_REQUIRE_EQUAL(spend.vin[0].scriptWitness.stack[0].size(), mldsa::SIGNATURE_BYTES);
    BOOST_REQUIRE_EQUAL(spend.vin[0].scriptWitness.stack[1].size(), mldsa::PUBLICKEY_BYTES);

    const int unsupportedHashTypes[] = {
        SIGHASH_NONE,
        SIGHASH_SINGLE,
        SIGHASH_ALL | SIGHASH_ANYONECANPAY
    };
    for (int hashType : unsupportedHashTypes) {
        CMutableTransaction unsupportedSpend;
        unsupportedSpend.vin.emplace_back(COutPoint(fundingTx.GetHash(), 0));
        unsupportedSpend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        BOOST_CHECK(!SignSignature(keystore, fundingTx, unsupportedSpend, 0,
                                   hashType, context));
        BOOST_CHECK(unsupportedSpend.vin[0].scriptWitness.IsNull());
    }

    auto verifySpend = [&](const CMutableTransaction& candidate,
                           unsigned int flags,
                           const Consensus::PQSignatureContext& verifyContext,
                           ScriptError& error) {
        const CTransaction tx(candidate);
        return VerifyScript(tx.vin[0].scriptSig,
                            fundingTx.vout[0].scriptPubKey,
                            &tx.vin[0].scriptWitness,
                            flags,
                            TransactionSignatureChecker(&tx, 0, amount, verifyContext),
                            &error);
    };

    const unsigned int preActivationFlags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS;
    const unsigned int activeFlags = preActivationFlags | SCRIPT_VERIFY_PQ_HYBRID;
    ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;

    BOOST_CHECK(verifySpend(spend, activeFlags, context, error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

    BOOST_CHECK(!verifySpend(spend, activeFlags,
                             Consensus::NullPQSignatureContext(), error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED);

    CMutableTransaction emptyWitness = spend;
    emptyWitness.vin[0].scriptWitness.stack.clear();

    // Before activation, witness-v2 retains normal future-witness consensus
    // semantics. Relay separately rejects newly-created v2 outputs.
    BOOST_CHECK(verifySpend(emptyWitness, preActivationFlags,
                            Consensus::NullPQSignatureContext(), error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

    BOOST_CHECK(!verifySpend(emptyWitness, activeFlags, context, error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH);

    CMutableTransaction malformedSignature = spend;
    malformedSignature.vin[0].scriptWitness.stack[0][0] ^= 0x01;
    BOOST_CHECK(!verifySpend(malformedSignature, activeFlags, context, error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED);
}

BOOST_AUTO_TEST_CASE(witness_v2_signatures_are_bound_to_network_context)
{
    const Consensus::PQSignatureContext& mainContext = NetworkContext("main");
    const Consensus::PQSignatureContext& testContext = NetworkContext("test");
    const Consensus::PQSignatureContext& regtestContext = NetworkContext("regtest");
    const Consensus::PQSignatureContext* contexts[] = {
        &mainContext, &testContext, &regtestContext
    };

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();

    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));

    const CAmount amount = 10 * COIN;
    CMutableTransaction funding;
    funding.vout.emplace_back(amount,
        GetScriptForWitnessV2PQ(pubkey.GetWitnessProgram()));
    const CTransaction fundingTx(funding);
    const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS |
                               SCRIPT_VERIFY_PQ_HYBRID;

    for (size_t signingNetwork = 0; signingNetwork < 3; ++signingNetwork) {
        CMutableTransaction spend;
        spend.vin.emplace_back(COutPoint(fundingTx.GetHash(), 0));
        spend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        BOOST_REQUIRE(SignSignature(keystore, fundingTx, spend, 0, SIGHASH_ALL,
                                    *contexts[signingNetwork]));

        const CTransaction tx(spend);
        for (size_t verifyingNetwork = 0; verifyingNetwork < 3; ++verifyingNetwork) {
            ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
            const bool accepted = VerifyScript(
                tx.vin[0].scriptSig, fundingTx.vout[0].scriptPubKey,
                &tx.vin[0].scriptWitness, flags,
                TransactionSignatureChecker(&tx, 0, amount,
                                            *contexts[verifyingNetwork]),
                &error);
            BOOST_CHECK_EQUAL(accepted, signingNetwork == verifyingNetwork);
            BOOST_CHECK_EQUAL(error, signingNetwork == verifyingNetwork
                ? SCRIPT_ERR_OK : SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED);
        }
    }
}

BOOST_AUTO_TEST_CASE(rip25_production_full_chain_kat)
{
    // The source is the published BIP39 "abandon ... about"/"TREZOR" seed.
    // Expected derivation seeds were computed independently with BIP32
    // HMAC-SHA512; transaction IDs and sighashes with explicit little-endian
    // serialization and SHA256d; addresses with BIP350 Bech32m.
    const std::vector<unsigned char> walletSeed = ParseHex(
        "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e5349553"
        "1f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04");
    BOOST_REQUIRE_EQUAL(walletSeed.size(), pqderivation::BIP39_SEED_BYTES);

    struct KatCase {
        const char* network;
        uint32_t coinType;
        const char* keypath;
        const char* pqSeed;
        const char* program;
        const char* address;
        const char* fundingHex;
        const char* fundingTxid;
        const char* spendHex;
        const char* spendTxid;
        const char* sighash;
    };
    const KatCase cases[] = {
        {
            "main", 175, "m/25'/175'/0'/0'/0'",
            "5312ca47967e38c2c45a56837491a4b4a627bc697c4f247a7a090a854d798222",
            "ffc2fc161fdad5c12334fc2c5c0a52f2c21ad02ff5d585155a6b58c23b002e43",
            "rvn1zgvhqqw7ztp4459v96h6jl5q6cte9yzju9n7rgg7p6hdp79huctlshjd5ac",
            "02000000010000000000000000000000000000000000000000000000000000000000000000"
            "ffffffff025151ffffffff0100ca9a3b00000000225220432e003bc2586b5a1585d5f5"
            "2fd01ac2f2520a5c2cfc3423c1d5da1f16fcc2ff00000000",
            "f5c6dacd7f7e9dc26ee4ce89a8ecd0a33655eacac16c8049210ade78a3808a2f",
            "02000000012f8a80a378de0a2149806cc1caea5536a3d0eca889cee46ec29d7e7fcdda"
            "c6f50000000000ffffffff0118c69a3b00000000015100000000",
            "d9fdfa163ea3bb3b70fe42d536d061a21156dcf36fb035f872e056be99925da6",
            "07cbfdb9a30791a779eb668df4178aa044d39a0b970568fce797e0940295be0e"
        },
        {
            "test", 1, "m/25'/1'/0'/0'/0'",
            "e0f3d1cfb06da142ccdbdc54aed4e131c9ab16403d97a33964bad3dc99f45e2d",
            "3b2b571eb1bf9f935a19f2acbe99ce27fb7d3519a54e4b2f6017f5f3a876c9ad",
            "trvn1z4hyhd28n75tkqt6tf6j3jdtalvnuaxd74nepjk5nn7lmz8jh9vaspj3xdu",
            "02000000010000000000000000000000000000000000000000000000000000000000000000"
            "ffffffff025151ffffffff0100ca9a3b00000000225220adc976a8f3f517602f4b4ea5"
            "19357dfb27ce99beacf2195a939fbfb11e572b3b00000000",
            "a5f00dcc242140aba60071686f3f17e0edd2a115a7f29c54155db04f848f8bbf",
            "0200000001bf8b8f844fb05d15549cf2a715a1d2ede0173f6f687100a6ab402124cc0d"
            "f0a50000000000ffffffff0118c69a3b00000000015100000000",
            "58a317ed4cbc902aa6a0be3ee1eed0d2d31b1a2b367b81e86cf866e3a76c4626",
            "65e96681e7e05607b0c659de85296f6937c01ffb10eecb95c4ff71ce35d3c64e"
        },
        {
            "regtest", 1, "m/25'/1'/0'/0'/0'",
            "e0f3d1cfb06da142ccdbdc54aed4e131c9ab16403d97a33964bad3dc99f45e2d",
            "3b2b571eb1bf9f935a19f2acbe99ce27fb7d3519a54e4b2f6017f5f3a876c9ad",
            "rcrt1z4hyhd28n75tkqt6tf6j3jdtalvnuaxd74nepjk5nn7lmz8jh9vasg4ztx8",
            "02000000010000000000000000000000000000000000000000000000000000000000000000"
            "ffffffff025151ffffffff0100ca9a3b00000000225220adc976a8f3f517602f4b4ea5"
            "19357dfb27ce99beacf2195a939fbfb11e572b3b00000000",
            "a5f00dcc242140aba60071686f3f17e0edd2a115a7f29c54155db04f848f8bbf",
            "0200000001bf8b8f844fb05d15549cf2a715a1d2ede0173f6f687100a6ab402124cc0d"
            "f0a50000000000ffffffff0118c69a3b00000000015100000000",
            "58a317ed4cbc902aa6a0be3ee1eed0d2d31b1a2b367b81e86cf866e3a76c4626",
            "65e96681e7e05607b0c659de85296f6937c01ffb10eecb95c4ff71ce35d3c64e"
        }
    };

    NetworkSelectionRestore restoreNetwork;
    for (const KatCase& vector : cases) {
        SelectParams(vector.network);
        BOOST_CHECK_EQUAL(pqderivation::GetKeypath(vector.coinType, 0), vector.keypath);

        SecureVector pqSeed;
        BOOST_REQUIRE(pqderivation::DeriveSeed(walletSeed.data(), walletSeed.size(),
                                               vector.coinType, 0, pqSeed));
        BOOST_REQUIRE_EQUAL(pqSeed.size(), mldsa::SEED_BYTES);
        BOOST_CHECK_EQUAL(HexStr(pqSeed.begin(), pqSeed.end()), vector.pqSeed);

        CPQKey key;
        BOOST_REQUIRE(key.SetSeed(pqSeed.data()));
        SecureVector().swap(pqSeed);
        const CPQPubKey pubkey = key.GetPubKey();
        const uint256 program = pubkey.GetWitnessProgram();
        BOOST_CHECK_EQUAL(program.GetHex(), vector.program);
        BOOST_CHECK_EQUAL(EncodeDestination(WitnessV2PQDestination(program)),
                          vector.address);

        CBasicKeyStore keystore;
        BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));

        // A deterministic coinbase-shaped funding transaction supplies the
        // witness-v2 output without relying on mutable chainstate fixtures.
        const CAmount amount = 10 * COIN;
        CMutableTransaction funding;
        funding.vin.emplace_back(COutPoint(), CScript() << OP_1 << OP_1);
        funding.vout.emplace_back(amount, GetScriptForWitnessV2PQ(program));
        const CTransaction fundingTx(funding);
        CValidationState fundingState;
        BOOST_REQUIRE(CheckTransaction(fundingTx, fundingState));
        CDataStream fundingWire(SER_NETWORK,
            PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
        fundingWire << fundingTx;
        BOOST_CHECK_EQUAL(HexStr(fundingWire.begin(), fundingWire.end()),
                          vector.fundingHex);
        BOOST_CHECK_EQUAL(fundingTx.GetHash().GetHex(), vector.fundingTxid);

        CMutableTransaction spend;
        spend.vin.emplace_back(COutPoint(fundingTx.GetHash(), 0));
        spend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        const Consensus::PQSignatureContext& context = NetworkContext(vector.network);
        BOOST_REQUIRE(SignSignature(keystore, fundingTx, spend, 0, SIGHASH_ALL,
                                    context));
        const CTransaction spendTx(spend);
        CValidationState spendState;
        BOOST_REQUIRE(CheckTransaction(spendTx, spendState));
        BOOST_REQUIRE_EQUAL(spendTx.vin[0].scriptWitness.stack.size(), 2U);
        BOOST_CHECK_EQUAL(spendTx.vin[0].scriptWitness.stack[0].size(),
                          mldsa::SIGNATURE_BYTES);
        BOOST_CHECK_EQUAL(spendTx.vin[0].scriptWitness.stack[1].size(),
                          mldsa::PUBLICKEY_BYTES);
        BOOST_CHECK(std::equal(pubkey.begin(), pubkey.end(),
                               spendTx.vin[0].scriptWitness.stack[1].begin()));

        CDataStream spendNoWitness(SER_NETWORK,
            PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
        spendNoWitness << spendTx;
        BOOST_CHECK_EQUAL(HexStr(spendNoWitness.begin(), spendNoWitness.end()),
                          vector.spendHex);
        BOOST_CHECK_EQUAL(spendTx.GetHash().GetHex(), vector.spendTxid);

        const uint256 sighash = SignatureHash(CScript(), spendTx, 0,
            SIGHASH_ALL, amount, SIGVERSION_WITNESS_V2_PQ);
        BOOST_CHECK_EQUAL(sighash.GetHex(), vector.sighash);
        const PrecomputedTransactionData cache(spendTx);
        BOOST_CHECK(SignatureHash(CScript(), spendTx, 0, SIGHASH_ALL, amount,
                                  SIGVERSION_WITNESS_V2_PQ, &cache) == sighash);

        const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS |
                                   SCRIPT_VERIFY_PQ_HYBRID;
        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(spendTx.vin[0].scriptSig,
            fundingTx.vout[0].scriptPubKey,
            &spendTx.vin[0].scriptWitness, flags,
            TransactionSignatureChecker(&spendTx, 0, amount, context), &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

        // The production signer may hedge with randomness. Capture and
        // round-trip its witness, but never require a fixed signature/wtxid.
        BOOST_CHECK(spendTx.GetWitnessHash() != spendTx.GetHash());
        CDataStream witnessWire(SER_NETWORK, PROTOCOL_VERSION);
        witnessWire << spendTx;
        CMutableTransaction decoded;
        witnessWire >> decoded;
        const CTransaction decodedTx(decoded);
        BOOST_CHECK(decodedTx.GetHash() == spendTx.GetHash());
        BOOST_CHECK(decodedTx.GetWitnessHash() == spendTx.GetWitnessHash());
    }
}

BOOST_AUTO_TEST_SUITE_END()
