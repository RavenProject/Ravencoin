// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "data/tx_invalid.json.h"
#include "data/tx_valid.json.h"
#include "test/test_raven.h"

#include "clientversion.h"
#include "chainparams.h"
#include "checkqueue.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "crypto/mldsa.h"
#include "core_memusage.h"
#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "validation.h"
#include "policy/policy.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/script_error.h"
#include "script/standard.h"
#include "utilstrencodings.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

typedef std::vector<unsigned char> valtype;

namespace {

void AppendLE32(std::vector<unsigned char>& out, uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<unsigned char>(value >> shift));
    }
}

void AppendTestCompactSize(std::vector<unsigned char>& out, uint64_t value)
{
    if (value < 253) {
        out.push_back(static_cast<unsigned char>(value));
    } else if (value <= std::numeric_limits<uint16_t>::max()) {
        out.push_back(253);
        out.push_back(static_cast<unsigned char>(value));
        out.push_back(static_cast<unsigned char>(value >> 8));
    } else {
        BOOST_REQUIRE(value <= std::numeric_limits<uint32_t>::max());
        out.push_back(254);
        AppendLE32(out, static_cast<uint32_t>(value));
    }
}

class RecordingFailStream
{
private:
    std::vector<unsigned char> m_bytes;
    size_t m_pos{0};

public:
    size_t max_read_request{0};

    explicit RecordingFailStream(std::vector<unsigned char> bytes) :
        m_bytes(std::move(bytes)) {}

    int GetType() const { return SER_NETWORK; }
    int GetVersion() const { return PROTOCOL_VERSION; }

    void read(char* destination, size_t size)
    {
        max_read_request = std::max(max_read_request, size);
        if (size > m_bytes.size() - m_pos) {
            throw std::ios_base::failure("test stream truncated");
        }
        if (size != 0) {
            std::memcpy(destination, m_bytes.data() + m_pos, size);
            m_pos += size;
        }
    }

    template <typename T>
    RecordingFailStream& operator>>(T& value)
    {
        ::Unserialize(*this, value);
        return *this;
    }
};

} // namespace

// In script_tests.cpp
extern UniValue read_json(const std::string &jsondata);


static std::map<std::string, unsigned int> mapFlagNames = {
        {std::string("NONE"),                                  (unsigned int) SCRIPT_VERIFY_NONE},
        {std::string("P2SH"),                                  (unsigned int) SCRIPT_VERIFY_P2SH},
        {std::string("STRICTENC"),                             (unsigned int) SCRIPT_VERIFY_STRICTENC},
        {std::string("DERSIG"),                                (unsigned int) SCRIPT_VERIFY_DERSIG},
        {std::string("LOW_S"),                                 (unsigned int) SCRIPT_VERIFY_LOW_S},
        {std::string("SIGPUSHONLY"),                           (unsigned int) SCRIPT_VERIFY_SIGPUSHONLY},
        {std::string("MINIMALDATA"),                           (unsigned int) SCRIPT_VERIFY_MINIMALDATA},
        {std::string("NULLDUMMY"),                             (unsigned int) SCRIPT_VERIFY_NULLDUMMY},
        {std::string("DISCOURAGE_UPGRADABLE_NOPS"),            (unsigned int) SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
        {std::string("CLEANSTACK"),                            (unsigned int) SCRIPT_VERIFY_CLEANSTACK},
        {std::string("MINIMALIF"),                             (unsigned int) SCRIPT_VERIFY_MINIMALIF},
        {std::string("NULLFAIL"),                              (unsigned int) SCRIPT_VERIFY_NULLFAIL},
        {std::string("CHECKLOCKTIMEVERIFY"),                   (unsigned int) SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY},
        {std::string("CHECKSEQUENCEVERIFY"),                   (unsigned int) SCRIPT_VERIFY_CHECKSEQUENCEVERIFY},
        {std::string("WITNESS"),                               (unsigned int) SCRIPT_VERIFY_WITNESS},
        {std::string("DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM"), (unsigned int) SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM},
        {std::string("WITNESS_PUBKEYTYPE"),                    (unsigned int) SCRIPT_VERIFY_WITNESS_PUBKEYTYPE},
        {std::string("PQ_HYBRID"),                             (unsigned int) SCRIPT_VERIFY_PQ_HYBRID},
};

unsigned int ParseScriptFlags(std::string strFlags)
{
    if (strFlags.empty())
    {
        return 0;
    }
    unsigned int flags = 0;
    std::vector<std::string> words;
    boost::algorithm::split(words, strFlags, boost::algorithm::is_any_of(","));

    for (std::string word : words)
    {
        if (!mapFlagNames.count(word))
            BOOST_ERROR("Bad test: unknown verification flag '" << word << "'");
        flags |= mapFlagNames[word];
    }

    return flags;
}

std::string FormatScriptFlags(unsigned int flags)
{
    if (flags == 0)
    {
        return "";
    }
    std::string ret;
    std::map<std::string, unsigned int>::const_iterator it = mapFlagNames.begin();
    while (it != mapFlagNames.end())
    {
        if (flags & it->second)
        {
            ret += it->first + ",";
        }
        it++;
    }
    return ret.substr(0, ret.size() - 1);
}

BOOST_FIXTURE_TEST_SUITE(transaction_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(tx_valid_test)
    {
        BOOST_TEST_MESSAGE("Running TX Valid Test");

        // Read tests from test/data/tx_valid.json
        // Format is an array of arrays
        // Inner arrays are either [ "comment" ]
        // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
        // ... where all scripts are stringified scripts.
        //
        // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
        UniValue tests = read_json(std::string(json_tests::tx_valid, json_tests::tx_valid + sizeof(json_tests::tx_valid)));
        const Consensus::PQSignatureContext mainnetPQContext =
            CreateChainParams("main")->GetConsensus().pqSignatureContext;

        ScriptError err;
        bool sawPQVector = false;
        for (unsigned int idx = 0; idx < tests.size(); idx++)
        {
            UniValue test = tests[idx];
            std::string strTest = test.write();
            if (test[0].isArray())
            {
                if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    continue;
                }

                std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
                std::map<COutPoint, int64_t> mapprevOutValues;
                UniValue inputs = test[0].get_array();
                bool fValid = true;
                for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++)
                {
                    const UniValue &input = inputs[inpIdx];
                    if (!input.isArray())
                    {
                        fValid = false;
                        break;
                    }
                    UniValue vinput = input.get_array();
                    if (vinput.size() < 3 || vinput.size() > 4)
                    {
                        fValid = false;
                        break;
                    }
                    COutPoint outpoint(uint256S(vinput[0].get_str()), vinput[1].get_int());
                    mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                    if (vinput.size() >= 4)
                    {
                        mapprevOutValues[outpoint] = vinput[3].get_int64();
                    }
                }
                if (!fValid)
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    continue;
                }

                std::string transaction = test[1].get_str();
                sawPQVector |= test[2].get_str() == "P2SH,WITNESS,PQ_HYBRID";
                CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
                CTransaction tx(deserialize, stream);

                CValidationState state;
                BOOST_CHECK_MESSAGE(CheckTransaction(tx, state), strTest);
                BOOST_CHECK(state.IsValid());

                PrecomputedTransactionData txdata(tx);
                for (unsigned int i = 0; i < tx.vin.size(); i++)
                {
                    if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                    {
                        BOOST_ERROR("Bad test: " << strTest);
                        break;
                    }

                    CAmount amount = 0;
                    if (mapprevOutValues.count(tx.vin[i].prevout))
                    {
                        amount = mapprevOutValues[tx.vin[i].prevout];
                    }
                    unsigned int verify_flags = ParseScriptFlags(test[2].get_str());
                    const CScriptWitness *witness = &tx.vin[i].scriptWitness;
                    BOOST_CHECK_MESSAGE(VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                                                     witness, verify_flags, TransactionSignatureChecker(&tx, i, amount, txdata, mainnetPQContext), &err),
                                        strTest);
                    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
                }
            }
        }
        BOOST_CHECK_MESSAGE(sawPQVector, "Missing signed RIP-25 tx_valid vector");
    }

    BOOST_AUTO_TEST_CASE(tx_invalid_test)
    {
        BOOST_TEST_MESSAGE("Running TX Invalid Test");

        // Read tests from test/data/tx_invalid.json
        // Format is an array of arrays
        // Inner arrays are either [ "comment" ]
        // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
        // ... where all scripts are stringified scripts.
        //
        // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
        UniValue tests = read_json(std::string(json_tests::tx_invalid, json_tests::tx_invalid + sizeof(json_tests::tx_invalid)));
        const Consensus::PQSignatureContext mainnetPQContext =
            CreateChainParams("main")->GetConsensus().pqSignatureContext;

        // Initialize to SCRIPT_ERR_OK. The tests expect err to be changed to a
        // value other than SCRIPT_ERR_OK.
        ScriptError err = SCRIPT_ERR_OK;
        bool sawPQVector = false;
        for (unsigned int idx = 0; idx < tests.size(); idx++)
        {
            UniValue test = tests[idx];
            std::string strTest = test.write();
            if (test[0].isArray())
            {
                if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    continue;
                }

                std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
                std::map<COutPoint, int64_t> mapprevOutValues;
                UniValue inputs = test[0].get_array();
                bool fValid = true;
                for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++)
                {
                    const UniValue &input = inputs[inpIdx];
                    if (!input.isArray())
                    {
                        fValid = false;
                        break;
                    }
                    UniValue vinput = input.get_array();
                    if (vinput.size() < 3 || vinput.size() > 4)
                    {
                        fValid = false;
                        break;
                    }
                    COutPoint outpoint(uint256S(vinput[0].get_str()), vinput[1].get_int());
                    mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                    if (vinput.size() >= 4)
                    {
                        mapprevOutValues[outpoint] = vinput[3].get_int64();
                    }
                }
                if (!fValid)
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    continue;
                }

                std::string transaction = test[1].get_str();
                sawPQVector |= test[2].get_str() == "P2SH,WITNESS,PQ_HYBRID";
                CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
                CTransaction tx(deserialize, stream);

                CValidationState state;
                fValid = CheckTransaction(tx, state) && state.IsValid();

                PrecomputedTransactionData txdata(tx);
                for (unsigned int i = 0; i < tx.vin.size() && fValid; i++)
                {
                    if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                    {
                        BOOST_ERROR("Bad test: " << strTest);
                        break;
                    }

                    unsigned int verify_flags = ParseScriptFlags(test[2].get_str());
                    CAmount amount = 0;
                    if (mapprevOutValues.count(tx.vin[i].prevout))
                    {
                        amount = mapprevOutValues[tx.vin[i].prevout];
                    }
                    const CScriptWitness *witness = &tx.vin[i].scriptWitness;
                    fValid = VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                                          witness, verify_flags, TransactionSignatureChecker(&tx, i, amount, txdata, mainnetPQContext), &err);
                }
                BOOST_CHECK_MESSAGE(!fValid, strTest);
                BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, ScriptErrorString(err));
            }
        }
        BOOST_CHECK_MESSAGE(sawPQVector, "Missing corrupted RIP-25 tx_invalid vector");
    }

    BOOST_AUTO_TEST_CASE(pq_witness_v2_tx_vector_mutations)
    {
        const UniValue vectors = read_json(std::string(
            json_tests::tx_valid, json_tests::tx_valid + sizeof(json_tests::tx_valid)));
        unsigned int vectorIndex = vectors.size();
        for (unsigned int i = 0; i < vectors.size(); ++i) {
            const UniValue& candidate = vectors[i];
            if (candidate.isArray() && candidate.size() == 3 &&
                candidate[2].isStr() &&
                candidate[2].get_str() == "P2SH,WITNESS,PQ_HYBRID") {
                vectorIndex = i;
                break;
            }
        }
        BOOST_REQUIRE_MESSAGE(vectorIndex < vectors.size(),
                              "Missing signed RIP-25 tx_valid vector");
        const UniValue& vector = vectors[vectorIndex];
        BOOST_REQUIRE(vector[0].isArray());
        BOOST_REQUIRE_EQUAL(vector[0].size(), 1U);
        const UniValue& prevout = vector[0][0];
        BOOST_REQUIRE_EQUAL(prevout.size(), 4U);
        const CScript prevoutScript = ParseScript(prevout[2].get_str());
        BOOST_REQUIRE_EQUAL(prevoutScript.size(), 34U);
        const CAmount amount = prevout[3].get_int64();
        CDataStream stream(ParseHex(vector[1].get_str()), SER_NETWORK, PROTOCOL_VERSION);
        const CTransaction serializedTx(deserialize, stream);
        const CMutableTransaction signedSpend(serializedTx);
        BOOST_REQUIRE_EQUAL(signedSpend.vin.size(), 1U);
        BOOST_REQUIRE_EQUAL(signedSpend.vin[0].scriptWitness.stack.size(), 2U);
        BOOST_REQUIRE_EQUAL(signedSpend.vin[0].scriptWitness.stack[0].size(),
                            mldsa::SIGNATURE_BYTES);
        BOOST_REQUIRE_EQUAL(signedSpend.vin[0].scriptWitness.stack[1].size(),
                            mldsa::PUBLICKEY_BYTES);

        const Consensus::PQSignatureContext mainnetContext =
            CreateChainParams("main")->GetConsensus().pqSignatureContext;
        const Consensus::PQSignatureContext testnetContext =
            CreateChainParams("test")->GetConsensus().pqSignatureContext;
        const unsigned int flags = ParseScriptFlags(vector[2].get_str());

        auto check = [&](const CMutableTransaction& candidate,
                         const CScript& candidatePrevoutScript,
                         const Consensus::PQSignatureContext& context,
                         bool expectedResult, ScriptError expectedError,
                         const char* label) {
            const CTransaction tx(candidate);
            CValidationState state;
            BOOST_REQUIRE_MESSAGE(CheckTransaction(tx, state) && state.IsValid(),
                                  label << ": CheckTransaction must remain valid");
            const PrecomputedTransactionData txdata(tx);
            ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
            const bool result = VerifyScript(
                tx.vin[0].scriptSig, candidatePrevoutScript,
                &tx.vin[0].scriptWitness, flags,
                TransactionSignatureChecker(&tx, 0, amount, txdata, context),
                &err);
            BOOST_CHECK_MESSAGE(result == expectedResult,
                                label << ": unexpected verification result");
            BOOST_CHECK_MESSAGE(err == expectedError,
                                label << ": " << ScriptErrorString(err));
        };

        check(signedSpend, prevoutScript, mainnetContext,
              true, SCRIPT_ERR_OK, "valid ML-DSA witness-v2 spend");

        CMutableTransaction shortSignature(signedSpend);
        shortSignature.vin[0].scriptWitness.stack[0].pop_back();
        check(shortSignature, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_SIZE, "short signature");

        CMutableTransaction longSignature(signedSpend);
        longSignature.vin[0].scriptWitness.stack[0].push_back(0);
        check(longSignature, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_SIZE, "long signature");

        CMutableTransaction appendedHashType(signedSpend);
        appendedHashType.vin[0].scriptWitness.stack[0].push_back(SIGHASH_ALL);
        check(appendedHashType, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_SIZE, "appended sighash byte");

        CMutableTransaction shortPublicKey(signedSpend);
        shortPublicKey.vin[0].scriptWitness.stack[1].pop_back();
        check(shortPublicKey, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_PUBKEY_SIZE, "short public key");

        CMutableTransaction longPublicKey(signedSpend);
        longPublicKey.vin[0].scriptWitness.stack[1].push_back(0);
        check(longPublicKey, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_PUBKEY_SIZE, "long public key");

        CMutableTransaction missingElement(signedSpend);
        missingElement.vin[0].scriptWitness.stack.resize(1);
        check(missingElement, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH, "missing witness element");

        CMutableTransaction extraElement(signedSpend);
        extraElement.vin[0].scriptWitness.stack.emplace_back(1, 0);
        check(extraElement, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH, "extra witness element");

        CScript wrongProgram(prevoutScript);
        wrongProgram[2] ^= 0x01;
        check(signedSpend, wrongProgram, mainnetContext,
              false, SCRIPT_ERR_PQ_WITNESS_PROGRAM_MISMATCH, "wrong witness program");

        const CScript shortProgram = CScript() << OP_2 <<
            std::vector<unsigned char>(prevoutScript.begin() + 2,
                                       prevoutScript.end() - 1);
        check(signedSpend, shortProgram, mainnetContext,
              false, SCRIPT_ERR_WITNESS_PROGRAM_WRONG_LENGTH, "short witness program");

        CMutableTransaction changedPublicKey(signedSpend);
        changedPublicKey.vin[0].scriptWitness.stack[1][0] ^= 0x01;
        check(changedPublicKey, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_WITNESS_PROGRAM_MISMATCH, "changed public key");

        CMutableTransaction changedSignature(signedSpend);
        changedSignature.vin[0].scriptWitness.stack[0][0] ^= 0x01;
        check(changedSignature, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED, "changed signature");

        check(signedSpend, prevoutScript, testnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED, "wrong network context");

        CMutableTransaction changedOutput(signedSpend);
        --changedOutput.vout[0].nValue;
        check(changedOutput, prevoutScript, mainnetContext,
              false, SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED, "changed sighash input");
    }

    BOOST_AUTO_TEST_CASE(basic_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Basic Transaction Test");

        // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
        unsigned char ch[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x6b, 0xff, 0x7f, 0xcd, 0x4f, 0x85, 0x65, 0xef, 0x40, 0x6d, 0xd5, 0xd6, 0x3d, 0x4f, 0xf9, 0x4f, 0x31, 0x8f, 0xe8, 0x20, 0x27, 0xfd, 0x4d, 0xc4, 0x51, 0xb0, 0x44, 0x74, 0x01, 0x9f, 0x74, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x49, 0x30, 0x46, 0x02, 0x21, 0x00, 0xda, 0x0d, 0xc6, 0xae, 0xce, 0xfe, 0x1e, 0x06, 0xef, 0xdf, 0x05, 0x77, 0x37, 0x57, 0xde, 0xb1, 0x68, 0x82, 0x09, 0x30, 0xe3, 0xb0, 0xd0, 0x3f, 0x46, 0xf5, 0xfc, 0xf1, 0x50, 0xbf, 0x99, 0x0c, 0x02, 0x21, 0x00, 0xd2, 0x5b, 0x5c, 0x87, 0x04, 0x00, 0x76, 0xe4, 0xf2, 0x53, 0xf8, 0x26, 0x2e, 0x76, 0x3e, 0x2d, 0xd5, 0x1e, 0x7f, 0xf0, 0xbe, 0x15, 0x77, 0x27, 0xc4, 0xbc, 0x42, 0x80, 0x7f, 0x17, 0xbd, 0x39, 0x01, 0x41, 0x04, 0xe6, 0xc2, 0x6e, 0xf6, 0x7d, 0xc6, 0x10, 0xd2, 0xcd, 0x19, 0x24, 0x84, 0x78, 0x9a, 0x6c, 0xf9, 0xae, 0xa9, 0x93, 0x0b, 0x94, 0x4b, 0x7e, 0x2d, 0xb5, 0x34, 0x2b, 0x9d, 0x9e, 0x5b, 0x9f, 0xf7, 0x9a, 0xff, 0x9a, 0x2e, 0xe1, 0x97, 0x8d, 0xd7, 0xfd, 0x01, 0xdf, 0xc5, 0x22, 0xee, 0x02, 0x28, 0x3d, 0x3b, 0x06, 0xa9, 0xd0, 0x3a, 0xcf, 0x80, 0x96, 0x96, 0x8d, 0x7d, 0xbb, 0x0f, 0x91, 0x78, 0xff, 0xff, 0xff, 0xff, 0x02, 0x8b, 0xa7, 0x94, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xba, 0xde, 0xec, 0xfd, 0xef, 0x05, 0x07, 0x24, 0x7f, 0xc8, 0xf7, 0x42, 0x41, 0xd7, 0x3b, 0xc0, 0x39, 0x97, 0x2d, 0x7b, 0x88, 0xac, 0x40, 0x94, 0xa8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xc1, 0x09, 0x32, 0x48, 0x3f, 0xec, 0x93, 0xed, 0x51, 0xf5, 0xfe, 0x95, 0xe7, 0x25, 0x59, 0xf2, 0xcc, 0x70, 0x43, 0xf9, 0x88, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::vector<unsigned char> vch(ch, ch + sizeof(ch) - 1);
        CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
        CMutableTransaction tx;
        stream >> tx;
        CValidationState state;
        BOOST_CHECK_MESSAGE(CheckTransaction(tx, state) && state.IsValid(), "Simple deserialized transaction should be valid.");

        // Check that duplicate txins fail
        tx.vin.push_back(tx.vin[0]);
        BOOST_CHECK_MESSAGE(!CheckTransaction(tx, state) || !state.IsValid(), "Transaction with duplicate txins should be invalid.");
    }

    //
    // Helper: create two dummy transactions, each with
    // two outputs.  The first has 11 and 50 CENT outputs
    // paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
    // paid to a TX_PUBKEYHASH.
    //
    static std::vector<CMutableTransaction>
    SetupDummyInputs(CBasicKeyStore &keystoreRet, CCoinsViewCache &coinsRet)
    {
        std::vector<CMutableTransaction> dummyTransactions;
        dummyTransactions.resize(2);

        // Add some keys to the keystore:
        CKey key[4];
        for (int i = 0; i < 4; i++)
        {
            key[i].MakeNewKey(i % 2);
            keystoreRet.AddKey(key[i]);
        }

        // Create some dummy input transactions
        dummyTransactions[0].vout.resize(2);
        dummyTransactions[0].vout[0].nValue = 11 * CENT;
        dummyTransactions[0].vout[0].scriptPubKey << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
        dummyTransactions[0].vout[1].nValue = 50 * CENT;
        dummyTransactions[0].vout[1].scriptPubKey << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
        AddCoins(coinsRet, dummyTransactions[0], 0, uint256());

        dummyTransactions[1].vout.resize(2);
        dummyTransactions[1].vout[0].nValue = 21 * CENT;
        dummyTransactions[1].vout[0].scriptPubKey = GetScriptForDestination(key[2].GetPubKey().GetID());
        dummyTransactions[1].vout[1].nValue = 22 * CENT;
        dummyTransactions[1].vout[1].scriptPubKey = GetScriptForDestination(key[3].GetPubKey().GetID());
        AddCoins(coinsRet, dummyTransactions[1], 0, uint256());

        return dummyTransactions;
    }

    BOOST_AUTO_TEST_CASE(get_test)
    {
        BOOST_TEST_MESSAGE("Running Get Test");

        CBasicKeyStore keystore;
        CCoinsView coinsDummy;
        CCoinsViewCache coins(&coinsDummy);
        std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

        CMutableTransaction t1;
        t1.vin.resize(3);
        t1.vin[0].prevout.hash = dummyTransactions[0].GetHash();
        t1.vin[0].prevout.n = 1;
        t1.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
        t1.vin[1].prevout.hash = dummyTransactions[1].GetHash();
        t1.vin[1].prevout.n = 0;
        t1.vin[1].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
        t1.vin[2].prevout.hash = dummyTransactions[1].GetHash();
        t1.vin[2].prevout.n = 1;
        t1.vin[2].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
        t1.vout.resize(2);
        t1.vout[0].nValue = 90 * CENT;
        t1.vout[0].scriptPubKey << OP_1;

        BOOST_CHECK(AreInputsStandard(t1, coins));
        BOOST_CHECK_EQUAL(coins.GetValueIn(t1), (50 + 21 + 22) * CENT);
    }

    void CreateCreditAndSpend(const CKeyStore &keystore, const CScript &outscript, CTransactionRef &output, CMutableTransaction &input, bool success = true)
    {
        CMutableTransaction outputm;
        outputm.nVersion = 1;
        outputm.vin.resize(1);
        outputm.vin[0].prevout.SetNull();
        outputm.vin[0].scriptSig = CScript();
        outputm.vout.resize(1);
        outputm.vout[0].nValue = 1;
        outputm.vout[0].scriptPubKey = outscript;
        CDataStream ssout(SER_NETWORK, PROTOCOL_VERSION);
        ssout << outputm;
        ssout >> output;
        assert(output->vin.size() == 1);
        assert(output->vin[0] == outputm.vin[0]);
        assert(output->vout.size() == 1);
        assert(output->vout[0] == outputm.vout[0]);

        CMutableTransaction inputm;
        inputm.nVersion = 1;
        inputm.vin.resize(1);
        inputm.vin[0].prevout.hash = output->GetHash();
        inputm.vin[0].prevout.n = 0;
        inputm.vout.resize(1);
        inputm.vout[0].nValue = 1;
        inputm.vout[0].scriptPubKey = CScript();
        bool ret = SignSignature(keystore, *output, inputm, 0, SIGHASH_ALL);
        assert(ret == success);
        CDataStream ssin(SER_NETWORK, PROTOCOL_VERSION);
        ssin << inputm;
        ssin >> input;
        assert(input.vin.size() == 1);
        assert(input.vin[0] == inputm.vin[0]);
        assert(input.vout.size() == 1);
        assert(input.vout[0] == inputm.vout[0]);
        assert(input.vin[0].scriptWitness.stack == inputm.vin[0].scriptWitness.stack);
    }

    void CheckWithFlag(const CTransactionRef &output, const CMutableTransaction &input, int flags, bool success)
    {
        ScriptError error;
        CTransaction inputi(input);
        bool ret = VerifyScript(inputi.vin[0].scriptSig, output->vout[0].scriptPubKey, &inputi.vin[0].scriptWitness, flags, TransactionSignatureChecker(&inputi, 0, output->vout[0].nValue), &error);
        assert(ret == success);
    }

    static CScript PushAll(const std::vector<valtype> &values)
    {
        CScript result;
        for (const valtype &v : values)
        {
            if (v.size() == 0)
            {
                result << OP_0;
            } else if (v.size() == 1 && v[0] >= 1 && v[0] <= 16)
            {
                result << CScript::EncodeOP_N(v[0]);
            } else
            {
                result << v;
            }
        }
        return result;
    }

    void ReplaceRedeemScript(CScript &script, const CScript &redeemScript)
    {
        std::vector<valtype> stack;
        EvalScript(stack, script, SCRIPT_VERIFY_STRICTENC, BaseSignatureChecker(), SIGVERSION_BASE);
        assert(stack.size() > 0);
        stack.back() = std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());
        script = PushAll(stack);
    }

    BOOST_AUTO_TEST_CASE(big_witness_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Big Witness Transaction Test");

        CMutableTransaction mtx;
        mtx.nVersion = 1;

        CKey key;
        key.MakeNewKey(true); // Need to use compressed keys in segwit or the signing will fail
        CBasicKeyStore keystore;
        keystore.AddKeyPubKey(key, key.GetPubKey());
        CKeyID hash = key.GetPubKey().GetID();
        CScript scriptPubKey = CScript() << OP_0 << std::vector<unsigned char>(hash.begin(), hash.end());

        std::vector<int> sigHashes;
        sigHashes.push_back(SIGHASH_NONE | SIGHASH_ANYONECANPAY);
        sigHashes.push_back(SIGHASH_SINGLE | SIGHASH_ANYONECANPAY);
        sigHashes.push_back(SIGHASH_ALL | SIGHASH_ANYONECANPAY);
        sigHashes.push_back(SIGHASH_NONE);
        sigHashes.push_back(SIGHASH_SINGLE);
        sigHashes.push_back(SIGHASH_ALL);

        // create a big transaction of 4500 inputs signed by the same key
        for (uint32_t ij = 0; ij < 4500; ij++)
        {
            uint32_t i = mtx.vin.size();
            uint256 prevId;
            prevId.SetHex("0000000000000000000000000000000000000000000000000000000000000100");
            COutPoint outpoint(prevId, i);

            mtx.vin.resize(mtx.vin.size() + 1);
            mtx.vin[i].prevout = outpoint;
            mtx.vin[i].scriptSig = CScript();

            mtx.vout.resize(mtx.vout.size() + 1);
            mtx.vout[i].nValue = 1000;
            mtx.vout[i].scriptPubKey = CScript() << OP_1;
        }

        // sign all inputs
        for (uint32_t i = 0; i < mtx.vin.size(); i++)
        {
            bool hashSigned = SignSignature(keystore, scriptPubKey, mtx, i, 1000, sigHashes.at(i % sigHashes.size()));
            assert(hashSigned);
        }

        CDataStream ssout(SER_NETWORK, PROTOCOL_VERSION);
        auto vstream = WithOrVersion(&ssout, 0);
        vstream << mtx;
        CTransaction tx(deserialize, vstream);

        // check all inputs concurrently, with the cache
        PrecomputedTransactionData txdata(tx);
        boost::thread_group threadGroup;
        CCheckQueue<CScriptCheck> scriptcheckqueue(128);
        CCheckQueueControl<CScriptCheck> control(&scriptcheckqueue);

        for (int i = 0; i < 20; i++)
            threadGroup.create_thread(boost::bind(&CCheckQueue<CScriptCheck>::Thread, boost::ref(scriptcheckqueue)));

        std::vector<Coin> coins;
        for (uint32_t i = 0; i < mtx.vin.size(); i++)
        {
            Coin coin;
            coin.nHeight = 1;
            coin.fCoinBase = false;
            coin.out.nValue = 1000;
            coin.out.scriptPubKey = scriptPubKey;
            coins.emplace_back(std::move(coin));
        }

        for (uint32_t i = 0; i < mtx.vin.size(); i++)
        {
            std::vector<CScriptCheck> vChecks;
            CScriptCheck check(coins[tx.vin[i].prevout.n].out, tx, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false, &txdata);
            vChecks.push_back(CScriptCheck());
            check.swap(vChecks.back());
            control.Add(vChecks);
        }

        bool controlCheck = control.Wait();
        assert(controlCheck);

        threadGroup.interrupt_all();
        threadGroup.join_all();
    }

    BOOST_AUTO_TEST_CASE(witness_test)
    {
        BOOST_TEST_MESSAGE("Running Witness Test");

        CBasicKeyStore keystore, keystore2;
        CKey key1, key2, key3, key1L, key2L;
        CPubKey pubkey1, pubkey2, pubkey3, pubkey1L, pubkey2L;
        key1.MakeNewKey(true);
        key2.MakeNewKey(true);
        key3.MakeNewKey(true);
        key1L.MakeNewKey(false);
        key2L.MakeNewKey(false);
        pubkey1 = key1.GetPubKey();
        pubkey2 = key2.GetPubKey();
        pubkey3 = key3.GetPubKey();
        pubkey1L = key1L.GetPubKey();
        pubkey2L = key2L.GetPubKey();
        keystore.AddKeyPubKey(key1, pubkey1);
        keystore.AddKeyPubKey(key2, pubkey2);
        keystore.AddKeyPubKey(key1L, pubkey1L);
        keystore.AddKeyPubKey(key2L, pubkey2L);
        CScript scriptPubkey1, scriptPubkey2, scriptPubkey1L, scriptPubkey2L, scriptMulti;
        scriptPubkey1 << ToByteVector(pubkey1) << OP_CHECKSIG;
        scriptPubkey2 << ToByteVector(pubkey2) << OP_CHECKSIG;
        scriptPubkey1L << ToByteVector(pubkey1L) << OP_CHECKSIG;
        scriptPubkey2L << ToByteVector(pubkey2L) << OP_CHECKSIG;
        std::vector<CPubKey> oneandthree;
        oneandthree.push_back(pubkey1);
        oneandthree.push_back(pubkey3);
        scriptMulti = GetScriptForMultisig(2, oneandthree);
        keystore.AddCScript(scriptPubkey1);
        keystore.AddCScript(scriptPubkey2);
        keystore.AddCScript(scriptPubkey1L);
        keystore.AddCScript(scriptPubkey2L);
        keystore.AddCScript(scriptMulti);
        keystore.AddCScript(GetScriptForWitness(scriptPubkey1));
        keystore.AddCScript(GetScriptForWitness(scriptPubkey2));
        keystore.AddCScript(GetScriptForWitness(scriptPubkey1L));
        keystore.AddCScript(GetScriptForWitness(scriptPubkey2L));
        keystore.AddCScript(GetScriptForWitness(scriptMulti));
        keystore2.AddCScript(scriptMulti);
        keystore2.AddCScript(GetScriptForWitness(scriptMulti));
        keystore2.AddKeyPubKey(key3, pubkey3);

        CTransactionRef output1, output2;
        CMutableTransaction input1, input2;
        SignatureData sigdata;

        // Normal pay-to-compressed-pubkey.
        CreateCreditAndSpend(keystore, scriptPubkey1, output1, input1);
        CreateCreditAndSpend(keystore, scriptPubkey2, output2, input2);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // P2SH pay-to-compressed-pubkey.
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(scriptPubkey1)), output1, input1);
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(scriptPubkey2)), output2, input2);
        ReplaceRedeemScript(input2.vin[0].scriptSig, scriptPubkey1);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // Witness pay-to-compressed-pubkey (v0).
        CreateCreditAndSpend(keystore, GetScriptForWitness(scriptPubkey1), output1, input1);
        CreateCreditAndSpend(keystore, GetScriptForWitness(scriptPubkey2), output2, input2);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // P2SH witness pay-to-compressed-pubkey (v0).
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptPubkey1))), output1, input1);
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptPubkey2))), output2, input2);
        ReplaceRedeemScript(input2.vin[0].scriptSig, GetScriptForWitness(scriptPubkey1));
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // Normal pay-to-uncompressed-pubkey.
        CreateCreditAndSpend(keystore, scriptPubkey1L, output1, input1);
        CreateCreditAndSpend(keystore, scriptPubkey2L, output2, input2);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // P2SH pay-to-uncompressed-pubkey.
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(scriptPubkey1L)), output1, input1);
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(scriptPubkey2L)), output2, input2);
        ReplaceRedeemScript(input2.vin[0].scriptSig, scriptPubkey1L);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
        CheckWithFlag(output1, input2, 0, true);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH, false);
        CheckWithFlag(output1, input2, STANDARD_SCRIPT_VERIFY_FLAGS, false);

        // Signing disabled for witness pay-to-uncompressed-pubkey (v1).
        CreateCreditAndSpend(keystore, GetScriptForWitness(scriptPubkey1L), output1, input1, false);
        CreateCreditAndSpend(keystore, GetScriptForWitness(scriptPubkey2L), output2, input2, false);

        // Signing disabled for P2SH witness pay-to-uncompressed-pubkey (v1).
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptPubkey1L))), output1, input1, false);
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptPubkey2L))), output2, input2, false);

        // Normal 2-of-2 multisig
        CreateCreditAndSpend(keystore, scriptMulti, output1, input1, false);
        CheckWithFlag(output1, input1, 0, false);
        CreateCreditAndSpend(keystore2, scriptMulti, output2, input2, false);
        CheckWithFlag(output2, input2, 0, false);
        BOOST_CHECK(*output1 == *output2);
        UpdateTransaction(input1, 0, CombineSignatures(output1->vout[0].scriptPubKey, MutableTransactionSignatureChecker(&input1, 0, output1->vout[0].nValue), DataFromTransaction(input1, 0), DataFromTransaction(input2, 0)));
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);

        // P2SH 2-of-2 multisig
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(scriptMulti)), output1, input1, false);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, false);
        CreateCreditAndSpend(keystore2, GetScriptForDestination(CScriptID(scriptMulti)), output2, input2, false);
        CheckWithFlag(output2, input2, 0, true);
        CheckWithFlag(output2, input2, SCRIPT_VERIFY_P2SH, false);
        BOOST_CHECK(*output1 == *output2);
        UpdateTransaction(input1, 0, CombineSignatures(output1->vout[0].scriptPubKey, MutableTransactionSignatureChecker(&input1, 0, output1->vout[0].nValue), DataFromTransaction(input1, 0), DataFromTransaction(input2, 0)));
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);

        // Witness 2-of-2 multisig
        CreateCreditAndSpend(keystore, GetScriptForWitness(scriptMulti), output1, input1, false);
        CheckWithFlag(output1, input1, 0, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false);
        CreateCreditAndSpend(keystore2, GetScriptForWitness(scriptMulti), output2, input2, false);
        CheckWithFlag(output2, input2, 0, true);
        CheckWithFlag(output2, input2, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false);
        BOOST_CHECK(*output1 == *output2);
        UpdateTransaction(input1, 0, CombineSignatures(output1->vout[0].scriptPubKey, MutableTransactionSignatureChecker(&input1, 0, output1->vout[0].nValue), DataFromTransaction(input1, 0), DataFromTransaction(input2, 0)));
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);

        // P2SH witness 2-of-2 multisig
        CreateCreditAndSpend(keystore, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptMulti))), output1, input1, false);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false);
        CreateCreditAndSpend(keystore2, GetScriptForDestination(CScriptID(GetScriptForWitness(scriptMulti))), output2, input2, false);
        CheckWithFlag(output2, input2, SCRIPT_VERIFY_P2SH, true);
        CheckWithFlag(output2, input2, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false);
        BOOST_CHECK(*output1 == *output2);
        UpdateTransaction(input1, 0, CombineSignatures(output1->vout[0].scriptPubKey, MutableTransactionSignatureChecker(&input1, 0, output1->vout[0].nValue), DataFromTransaction(input1, 0), DataFromTransaction(input2, 0)));
        CheckWithFlag(output1, input1, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true);
        CheckWithFlag(output1, input1, STANDARD_SCRIPT_VERIFY_FLAGS, true);
    }

    BOOST_AUTO_TEST_CASE(IsStandard_test)
    {
        BOOST_TEST_MESSAGE("Running IsStandard Test");

        LOCK(cs_main);
        CBasicKeyStore keystore;
        CCoinsView coinsDummy;
        CCoinsViewCache coins(&coinsDummy);
        std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

        CMutableTransaction t;
        t.vin.resize(1);
        t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
        t.vin[0].prevout.n = 1;
        t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
        t.vout.resize(1);
        t.vout[0].nValue = 90 * CENT;
        CKey key;
        key.MakeNewKey(true);
        t.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        std::string reason;
        BOOST_CHECK(IsStandardTx(t, reason));

        // Check dust with default relay fee:
        CAmount nDustThreshold = 182 * dustRelayFee.GetFeePerK() / 1000;
        BOOST_CHECK_EQUAL(nDustThreshold, 546);
        // dust:
        t.vout[0].nValue = nDustThreshold - 1;
        BOOST_CHECK(!IsStandardTx(t, reason));
        // not dust:
        t.vout[0].nValue = nDustThreshold;
        BOOST_CHECK(IsStandardTx(t, reason));

        // Check dust with odd relay fee to verify rounding:
        // nDustThreshold = 182 * 3702 / 1000
        dustRelayFee = CFeeRate(3702);
        // dust:
        t.vout[0].nValue = 673 - 1;
        BOOST_CHECK(!IsStandardTx(t, reason));
        // not dust:
        t.vout[0].nValue = 673;
        BOOST_CHECK(IsStandardTx(t, reason));
        dustRelayFee = CFeeRate(DUST_RELAY_TX_FEE);

        t.vout[0].scriptPubKey = CScript() << OP_1;
        BOOST_CHECK(!IsStandardTx(t, reason));

        // MAX_OP_RETURN_RELAY-byte TX_NULL_DATA (standard)
        t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY, t.vout[0].scriptPubKey.size());
        BOOST_CHECK(IsStandardTx(t, reason));

        // MAX_OP_RETURN_RELAY+1-byte TX_NULL_DATA (non-standard)
        t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3800");
        BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY + 1, t.vout[0].scriptPubKey.size());
        BOOST_CHECK(!IsStandardTx(t, reason));

        // MAX_OP_RETURN_RELAY-byte TX_RESTRICTED_ASSET_DATA(standard)
        // The text after OP_RVN_ASSET, will be an address if it isn't another OP_RVN_ASSET
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET
                                           << ParseHex("04678fdab0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY, t.vout[0].scriptPubKey.size());
        BOOST_CHECK(IsStandardTx(t, reason));

        // MAX_OP_RETURN_RELAY+1-byte TX_RESTRICTED_ASSET_DATA (non-standard)
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3800");
        BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY + 1, t.vout[0].scriptPubKey.size());
        BOOST_CHECK(!IsStandardTx(t, reason));

        // Data payload can be encoded in any way...
        t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("");
        BOOST_CHECK(IsStandardTx(t, reason));
        t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("00") << ParseHex("01");
        BOOST_CHECK(IsStandardTx(t, reason));
        // OP_RESERVED *is* considered to be a PUSHDATA type opcode by IsPushOnly()!
        t.vout[0].scriptPubKey =
                CScript() << OP_RETURN << OP_RESERVED << -1 << 0 << ParseHex("01") << 2 << 3 << 4 << 5 << 6 << 7 << 8
                          << 9 << 10 << 11 << 12 << 13 << 14 << 15 << 16;
        BOOST_CHECK(IsStandardTx(t, reason));
        t.vout[0].scriptPubKey = CScript() << OP_RETURN << 0 << ParseHex("01") << 2
                                           << ParseHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        BOOST_CHECK(IsStandardTx(t, reason));

        // ...so long as it only contains PUSHDATA's
        t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RETURN;
        BOOST_CHECK(!IsStandardTx(t, reason));

        // TX_NULL_DATA w/o PUSHDATA
        t.vout.resize(1);
        t.vout[0].scriptPubKey = CScript() << OP_RETURN;
        BOOST_CHECK(IsStandardTx(t, reason));

        // Only one TX_NULL_DATA permitted in all cases
        t.vout.resize(2);
        t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        t.vout[1].scriptPubKey = CScript() << OP_RETURN
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        BOOST_CHECK(!IsStandardTx(t, reason));

        t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        t.vout[1].scriptPubKey = CScript() << OP_RETURN;
        BOOST_CHECK(!IsStandardTx(t, reason));

        t.vout[0].scriptPubKey = CScript() << OP_RETURN;
        t.vout[1].scriptPubKey = CScript() << OP_RETURN;
        BOOST_CHECK(!IsStandardTx(t, reason));

        // Asset data checks
        // Data payload can be encoded in any way...
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET << ParseHex("");
        BOOST_CHECK(IsStandardTx(t, reason));
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET << ParseHex("00") << ParseHex("01");
        BOOST_CHECK(IsStandardTx(t, reason));
        // OP_RESERVED *is* considered to be a PUSHDATA type opcode by IsPushOnly()!
        t.vout[0].scriptPubKey =
                CScript() << OP_RVN_ASSET << OP_RESERVED << -1 << 0 << ParseHex("01") << 2 << 3 << 4 << 5 << 6 << 7 << 8
                          << 9 << 10 << 11 << 12 << 13 << 14 << 15 << 16;
        BOOST_CHECK(IsStandardTx(t, reason));
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET << 0 << ParseHex("01") << 2
                                           << ParseHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        BOOST_CHECK(IsStandardTx(t, reason));

        // ...so long as it only contains PUSHDATA's don't check the first three bytes on OP_RVN_ASSET TX's
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET << OP_RESERVED;
        BOOST_CHECK(IsStandardTx(t, reason));

        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET << OP_RESERVED << OP_RESERVED << OP_RVN_ASSET;
        BOOST_CHECK(!IsStandardTx(t, reason));

        // TX_RESTRICTED_ASSET_DATA w/o PUSHDATA
        t.vout.resize(1);
        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET;
        BOOST_CHECK(IsStandardTx(t, reason));

        // Only one hundred TX_RESTRICTED_ASSET_DATA permitted in all cases
        t.vout.resize(101);
        for (int i = 0; i < 101; i++) {
            t.vout[i].scriptPubKey = CScript() << OP_RVN_ASSET
                                               << ParseHex(
                                                       "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        }
        BOOST_CHECK(!IsStandardTx(t, reason));

        t.vout[0].scriptPubKey = CScript() << OP_RVN_ASSET
                                           << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
        for (int i = 1; i < 101; i++) {
            t.vout[i].scriptPubKey = CScript() << OP_RVN_ASSET;
        }
        BOOST_CHECK(!IsStandardTx(t, reason));

        t.vout.clear();
        t.vout.resize(99);
        for (int i = 0; i < 99; i++)
            t.vout[i].scriptPubKey = CScript() << OP_RVN_ASSET;
        BOOST_CHECK(IsStandardTx(t, reason));
    }

    BOOST_AUTO_TEST_CASE(compact_witness_empty_element_amplification)
    {
        // A transaction containing one input, no outputs, and N empty witness
        // elements has 58 non-element bytes. This independently constructed
        // vector is exactly the largest P2P message accepted by this binary.
        static const size_t wireSize = MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE2;
        static const size_t fixedSize = 58;
        static const size_t witnessElements = wireSize - fixedSize;

        std::vector<unsigned char> wire;
        wire.reserve(wireSize);
        AppendLE32(wire, 2);                         // nVersion
        wire.push_back(0);                           // witness marker
        wire.push_back(1);                           // witness flag
        wire.push_back(1);                           // one input
        wire.insert(wire.end(), 32, 0);              // previous txid
        AppendLE32(wire, std::numeric_limits<uint32_t>::max());
        wire.push_back(0);                           // empty scriptSig
        AppendLE32(wire, std::numeric_limits<uint32_t>::max());
        wire.push_back(0);                           // no outputs
        AppendTestCompactSize(wire, witnessElements);
        wire.insert(wire.end(), witnessElements, 0); // empty witness items
        AppendLE32(wire, 0);                         // nLockTime
        BOOST_REQUIRE_EQUAL(wire.size(), wireSize);

        CDataStream input(wire, SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx(deserialize, input);
        BOOST_REQUIRE(input.empty());
        BOOST_REQUIRE_EQUAL(tx.vin.size(), 1U);
        BOOST_REQUIRE_EQUAL(tx.vin[0].scriptWitness.stack.size(), witnessElements);
        BOOST_CHECK(tx.vin[0].scriptWitness.stack[0].empty());
        BOOST_CHECK(tx.vin[0].scriptWitness.stack[255].empty());
        BOOST_CHECK(tx.vin[0].scriptWitness.stack[256].empty());
        BOOST_CHECK(tx.vin[0].scriptWitness.stack[witnessElements - 1].empty());

        // The vulnerable vector<vector<byte>> representation owns hundreds of
        // MiB here. Permit at most linear (2x plus index) allocator growth on
        // all supported standard-library implementations.
        BOOST_CHECK_LE(RecursiveDynamicUsage(tx), 2 * wireSize + 1024 * 1024);

        // Unknown witness versions remain forward-compatible even with this
        // element count; a count cap would silently turn that soft-fork rule
        // into a new consensus restriction.
        const CScript futureWitness = CScript() << 3 << std::vector<unsigned char>(32, 0x42);
        ScriptError futureError = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(CScript(), futureWitness,
                                 &tx.vin[0].scriptWitness,
                                 SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS,
                                 BaseSignatureChecker(), &futureError));
        BOOST_CHECK_EQUAL(futureError, SCRIPT_ERR_OK);
        BOOST_CHECK(!VerifyScript(CScript(), futureWitness,
                                  &tx.vin[0].scriptWitness,
                                  SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS |
                                      SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM,
                                  BaseSignatureChecker(), &futureError));
        BOOST_CHECK_EQUAL(futureError,
                          SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM);

        CDataStream encoded(SER_NETWORK, PROTOCOL_VERSION);
        encoded << tx;
        BOOST_REQUIRE_EQUAL(encoded.size(), wire.size());
        BOOST_CHECK_EQUAL(std::memcmp(encoded.data(), wire.data(), wire.size()), 0);

        CTransaction roundTrip(deserialize, encoded);
        BOOST_REQUIRE(encoded.empty());
        BOOST_CHECK(roundTrip.GetWitnessHash() == tx.GetWitnessHash());
        BOOST_CHECK(roundTrip.vin[0].scriptWitness.stack ==
                    tx.vin[0].scriptWitness.stack);
    }

    BOOST_AUTO_TEST_CASE(compact_witness_block_empty_element_amplification)
    {
        // Independently encode an exact 16-MB legacy-header block containing
        // one transaction whose witness consists entirely of empty elements.
        // This exercises the BLOCK deserialization path, not a transaction
        // object assembled by the implementation under test.
        static const size_t wireSize = MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE2;
        static const size_t legacyHeaderSize = 80;
        static const size_t transactionCountSize = 1;
        static const size_t transactionFixedSize = 58;
        static const size_t witnessElements =
            wireSize - legacyHeaderSize - transactionCountSize - transactionFixedSize;

        std::vector<unsigned char> wire;
        wire.reserve(wireSize);
        wire.insert(wire.end(), legacyHeaderSize, 0); // nTime=0: legacy header
        wire.push_back(1);                           // one transaction
        AppendLE32(wire, 2);                         // nVersion
        wire.push_back(0);                           // witness marker
        wire.push_back(1);                           // witness flag
        wire.push_back(1);                           // one input
        wire.insert(wire.end(), 32, 0);              // previous txid
        AppendLE32(wire, std::numeric_limits<uint32_t>::max());
        wire.push_back(0);                           // empty scriptSig
        AppendLE32(wire, std::numeric_limits<uint32_t>::max());
        wire.push_back(0);                           // no outputs
        AppendTestCompactSize(wire, witnessElements);
        wire.insert(wire.end(), witnessElements, 0); // empty witness items
        AppendLE32(wire, 0);                         // nLockTime
        BOOST_REQUIRE_EQUAL(wire.size(), wireSize);

        CDataStream input(wire, SER_NETWORK, PROTOCOL_VERSION);
        CBlock block;
        input >> block;
        BOOST_REQUIRE(input.empty());
        BOOST_REQUIRE_EQUAL(block.vtx.size(), 1U);
        BOOST_REQUIRE_EQUAL(block.vtx[0]->vin.size(), 1U);
        BOOST_REQUIRE_EQUAL(block.vtx[0]->vin[0].scriptWitness.stack.size(),
                            witnessElements);
        BOOST_CHECK(block.vtx[0]->vin[0].scriptWitness.stack[0].empty());
        BOOST_CHECK(block.vtx[0]->vin[0].scriptWitness.stack[255].empty());
        BOOST_CHECK(block.vtx[0]->vin[0].scriptWitness.stack[256].empty());
        BOOST_CHECK(block.vtx[0]->vin[0].scriptWitness.stack[witnessElements - 1].empty());
        BOOST_CHECK_LE(RecursiveDynamicUsage(block), 2 * wireSize + 1024 * 1024);

        CDataStream encoded(SER_NETWORK, PROTOCOL_VERSION);
        encoded << block;
        BOOST_REQUIRE_EQUAL(encoded.size(), wire.size());
        BOOST_CHECK_EQUAL(std::memcmp(encoded.data(), wire.data(), wire.size()), 0);
    }

    BOOST_AUTO_TEST_CASE(compact_witness_truncated_element_is_atomic_and_chunked)
    {
        CScriptWitness witness;
        witness.stack.push_back(std::vector<unsigned char>{0xaa});

        // One element claims a large canonical CompactSize length that fits
        // the encoded-size bound, but no payload follows. Historical vector
        // parsing resized/read in 5-MB chunks before discovering truncation.
        RecordingFailStream stream({0x01, 0xfe, 0xf0, 0xff, 0xff, 0x01});
        BOOST_CHECK_THROW(stream >> witness.stack, std::ios_base::failure);
        BOOST_CHECK_LE(stream.max_read_request, 64U * 1024U);

        // Failed parsing must not partially replace a previously valid stack.
        BOOST_REQUIRE_EQUAL(witness.stack.size(), 1U);
        BOOST_REQUIRE_EQUAL(witness.stack[0].size(), 1U);
        BOOST_CHECK_EQUAL(witness.stack[0][0], 0xaa);
    }

    BOOST_AUTO_TEST_CASE(compact_witness_move_leaves_valid_source)
    {
        CDataStream encoded(ParseHex("0201aa00"), SER_NETWORK, PROTOCOL_VERSION);
        CScriptWitness source;
        encoded >> source.stack;
        BOOST_REQUIRE(encoded.empty());

        CScriptWitness moved(std::move(source));
        BOOST_CHECK(source.stack.empty());
        BOOST_REQUIRE_EQUAL(moved.stack.size(), 2U);
        BOOST_REQUIRE_EQUAL(moved.stack[0].size(), 1U);
        BOOST_CHECK_EQUAL(moved.stack[0][0], 0xaa);
        BOOST_CHECK(moved.stack[1].empty());

        CScriptWitness assigned;
        assigned.stack.push_back(std::vector<unsigned char>{0xbb});
        assigned = std::move(moved);
        BOOST_CHECK(moved.stack.empty());
        BOOST_REQUIRE_EQUAL(assigned.stack.size(), 2U);
        BOOST_CHECK_EQUAL(assigned.stack[0][0], 0xaa);
        BOOST_CHECK(assigned.stack[1].empty());
    }

    BOOST_AUTO_TEST_CASE(compact_witness_preserves_compactsize_boundaries)
    {
        static const size_t elementCount = 260;
        std::vector<unsigned char> wire;
        AppendTestCompactSize(wire, elementCount);
        for (size_t i = 0; i < elementCount; ++i) {
            size_t size = 0;
            if (i == 1) size = 1;
            if (i == 2) size = 252;
            if (i == 3) size = 253;
            if (i == 254) size = 65535;
            if (i == 255) size = 65536;
            if (i == 256) size = 1;
            if (i == 257) size = 253;
            AppendTestCompactSize(wire, size);
            wire.insert(wire.end(), size, static_cast<unsigned char>(i));
        }

        CDataStream input(wire, SER_NETWORK, PROTOCOL_VERSION);
        CScriptWitness witness;
        input >> witness.stack;
        BOOST_REQUIRE(input.empty());
        BOOST_REQUIRE_EQUAL(witness.stack.size(), elementCount);
        BOOST_CHECK(witness.stack[0].empty());
        BOOST_REQUIRE_EQUAL(witness.stack[2].size(), 252U);
        BOOST_CHECK_EQUAL(witness.stack[2][251], 2U);
        BOOST_REQUIRE_EQUAL(witness.stack[3].size(), 253U);
        BOOST_CHECK_EQUAL(witness.stack[3][252], 3U);
        BOOST_REQUIRE_EQUAL(witness.stack[254].size(), 65535U);
        BOOST_CHECK_EQUAL(witness.stack[254][65534], 254U);
        BOOST_REQUIRE_EQUAL(witness.stack[255].size(), 65536U);
        BOOST_CHECK_EQUAL(witness.stack[255][65535], 255U);
        BOOST_REQUIRE_EQUAL(witness.stack[256].size(), 1U);
        BOOST_CHECK_EQUAL(witness.stack[256][0], 0U);
        BOOST_REQUIRE_EQUAL(witness.stack[257].size(), 253U);
        BOOST_CHECK_EQUAL(witness.stack[257][252], 1U);
        BOOST_CHECK(witness.stack[259].empty());

        CDataStream output(SER_NETWORK, PROTOCOL_VERSION);
        output << witness.stack;
        BOOST_REQUIRE_EQUAL(output.size(), wire.size());
        BOOST_CHECK_EQUAL(std::memcmp(output.data(), wire.data(), wire.size()), 0);

        CScriptWitness copied(witness);
        BOOST_CHECK(copied.stack == witness.stack);
        copied.stack[256].push_back(0x77);
        BOOST_REQUIRE_EQUAL(copied.stack[256].size(), 2U);
        BOOST_CHECK_EQUAL(copied.stack[256][1], 0x77);
        BOOST_CHECK(copied.stack != witness.stack);
    }

BOOST_AUTO_TEST_SUITE_END()
