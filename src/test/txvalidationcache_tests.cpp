// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/validation.h"
#include "chainparams.h"
#include "key.h"
#include "validation.h"
#include "miner.h"
#include "pubkey.h"
#include "txmempool.h"
#include "random.h"
#include "script/standard.h"
#include "script/sign.h"
#include "test/test_raven.h"
#include "utiltime.h"
#include "core_io.h"
#include "keystore.h"
#include "policy/policy.h"
#include "pqkey.h"

#include <boost/test/unit_test.hpp>

#include "util.h"

bool CheckInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData &txdata, std::vector<CScriptCheck> *pvChecks, const Consensus::PQSignatureContext& pqSignatureContext = Consensus::NullPQSignatureContext());

BOOST_AUTO_TEST_SUITE(tx_validationcache_tests)

    static bool
    ToMemPool(CMutableTransaction &tx)
    {
        LOCK(cs_main);

        CValidationState state;
        return AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), nullptr /* pfMissingInputs */,
                                  nullptr /* plTxnReplaced */, true /* bypass_limits */, 0 /* nAbsurdFee */);
    }

    BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend_test, TestChain100Setup)
    {

        BOOST_TEST_MESSAGE("Running TX MemPool Block DoubleSpend Test");

        // Make sure skipping validation of transctions that were
        // validated going into the memory pool does not allow
        // double-spends in blocks to pass validation when they should not.

        CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

        // Create a double-spend of mature coinbase txn:
        std::vector<CMutableTransaction> spends;
        spends.resize(2);
        for (int i = 0; i < 2; i++)
        {
            spends[i].nVersion = 1;
            spends[i].vin.resize(1);
            spends[i].vin[0].prevout.hash = coinbaseTxns[0].GetHash();
            spends[i].vin[0].prevout.n = 0;
            spends[i].vout.resize(1);
            spends[i].vout[0].nValue = 11 * CENT;
            spends[i].vout[0].scriptPubKey = scriptPubKey;

            // Sign:
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
            BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
            vchSig.push_back((unsigned char) SIGHASH_ALL);
            spends[i].vin[0].scriptSig << vchSig;
        }

        CBlock block;

        // Test 1: block with both of those transactions should be rejected.
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());

        // Test 2: ... and should be rejected if spend1 is in the memory pool
        BOOST_CHECK(ToMemPool(spends[0]));
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        mempool.clear();

        // Test 3: ... and should be rejected if spend2 is in the memory pool
        BOOST_CHECK(ToMemPool(spends[1]));
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        mempool.clear();

        // Final sanity test: first spend in mempool, second in block, that's OK:
        std::vector<CMutableTransaction> oneSpend;
        oneSpend.push_back(spends[0]);
        BOOST_CHECK(ToMemPool(spends[1]));
        block = CreateAndProcessBlock(oneSpend, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
        // spends[1] should have been removed from the mempool when the
        // block with spends[0] is accepted:
        BOOST_CHECK_EQUAL(mempool.size(), (uint64_t)0);
    }

    // Run CheckInputs (using pcoinsTip) on the given transaction, for all script
    // flags.  Test that CheckInputs passes for all flags that don't overlap with
    // the failing_flags argument, but otherwise fails.
    // CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY (and future NOP codes that may
    // get reassigned) have an interaction with DISCOURAGE_UPGRADABLE_NOPS: if
    // the script flags used contain DISCOURAGE_UPGRADABLE_NOPS but don't contain
    // CHECKLOCKTIMEVERIFY (or CHECKSEQUENCEVERIFY), but the script does contain
    // OP_CHECKLOCKTIMEVERIFY (or OP_CHECKSEQUENCEVERIFY), then script execution
    // should fail.
    // Capture this interaction with the upgraded_nop argument: set it when evaluating
    // any script flag that is implemented as an upgraded NOP code.
    void ValidateCheckInputsForAllFlags(CMutableTransaction &tx, uint32_t failing_flags, bool add_to_cache, bool upgraded_nop)
    {
        PrecomputedTransactionData txdata(tx);
        // If we add many more flags, this loop can get too expensive, but we can
        // rewrite in the future to randomly pick a set of flags to evaluate.
        for (uint32_t test_flags = 0; test_flags < (1U << 16); test_flags += 1)
        {
            CValidationState state;
            // Filter out incompatible flag choices
            if ((test_flags & SCRIPT_VERIFY_CLEANSTACK))
            {
                // CLEANSTACK requires P2SH and WITNESS, see VerifyScript() in
                // script/interpreter.cpp
                test_flags |= SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS;
            }
            if ((test_flags & SCRIPT_VERIFY_WITNESS))
            {
                // WITNESS requires P2SH
                test_flags |= SCRIPT_VERIFY_P2SH;
            }
            bool ret = CheckInputs(tx, state, pcoinsTip, true, test_flags, true, add_to_cache, txdata, nullptr);
            // CheckInputs should succeed iff test_flags doesn't intersect with
            // failing_flags
            bool expected_return_value = !(test_flags & failing_flags);
            if (expected_return_value && upgraded_nop)
            {
                // If the script flag being tested corresponds to an upgraded NOP,
                // then script execution should fail if DISCOURAGE_UPGRADABLE_NOPS
                // is set.
                expected_return_value = !(test_flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS);
            }
            BOOST_CHECK_EQUAL(ret, expected_return_value);

            // Test the caching
            if (ret && add_to_cache)
            {
                // Check that we get a cache hit if the tx was valid
                std::vector<CScriptCheck> scriptchecks;
                BOOST_CHECK(CheckInputs(tx, state, pcoinsTip, true, test_flags, true, add_to_cache, txdata, &scriptchecks));
                BOOST_CHECK(scriptchecks.empty());
            } else
            {
                // Check that we get script executions to check, if the transaction
                // was invalid, or we didn't add to cache.
                std::vector<CScriptCheck> scriptchecks;
                BOOST_CHECK(CheckInputs(tx, state, pcoinsTip, true, test_flags, true, add_to_cache, txdata, &scriptchecks));
                BOOST_CHECK_EQUAL(scriptchecks.size(), tx.vin.size());
            }
        }
    }

    BOOST_FIXTURE_TEST_CASE(checkinputs_test, TestChain100Setup)
    {

        BOOST_TEST_MESSAGE("Running CheckInputs Test");

        TurnOffSegwit();
        TurnOffCSV();
        TurnOffBIP34();
        TurnOffBIP65();
        TurnOffBIP66();

        // Test that passing CheckInputs with one set of script flags doesn't imply
        // that we would pass again with a different set of flags.
        InitScriptExecutionCache();

        CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
        CScript p2sh_scriptPubKey = GetScriptForDestination(CScriptID(p2pk_scriptPubKey));
        CScript p2pkh_scriptPubKey = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
        CScript p2wpkh_scriptPubKey = GetScriptForWitness(p2pkh_scriptPubKey);

        CBasicKeyStore keystore;
        keystore.AddKey(coinbaseKey);
        keystore.AddCScript(p2pk_scriptPubKey);

        // flags to test: SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, SCRIPT_VERIFY_CHECKSEQUENCE_VERIFY, SCRIPT_VERIFY_NULLDUMMY, uncompressed pubkey thing

        // Create 2 outputs that match the three scripts above, spending the first
        // coinbase tx.
        CMutableTransaction spend_tx;

        spend_tx.nVersion = 1;
        spend_tx.vin.resize(1);
        spend_tx.vin[0].prevout.hash = coinbaseTxns[0].GetHash();
        spend_tx.vin[0].prevout.n = 0;
        spend_tx.vout.resize(4);
        spend_tx.vout[0].nValue = 11 * CENT;
        spend_tx.vout[0].scriptPubKey = p2sh_scriptPubKey;
        spend_tx.vout[1].nValue = 11 * CENT;
        spend_tx.vout[1].scriptPubKey = p2wpkh_scriptPubKey;
        spend_tx.vout[2].nValue = 11 * CENT;
        spend_tx.vout[2].scriptPubKey =
                CScript() << OP_CHECKLOCKTIMEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
        spend_tx.vout[3].nValue = 11 * CENT;
        spend_tx.vout[3].scriptPubKey =
                CScript() << OP_CHECKSEQUENCEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

        // Sign, with a non-DER signature
        {
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(p2pk_scriptPubKey, spend_tx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
            BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
            vchSig.push_back((unsigned char) 0); // padding byte makes this non-DER
            vchSig.push_back((unsigned char) SIGHASH_ALL);
            spend_tx.vin[0].scriptSig << vchSig;
        }

        LOCK(cs_main);

        // Test that invalidity under a set of flags doesn't preclude validity
        // under other (eg consensus) flags.
        // spend_tx is invalid according to DERSIG
        {
            CValidationState state;
            PrecomputedTransactionData ptd_spend_tx(spend_tx);

            BOOST_CHECK(!CheckInputs(spend_tx, state, pcoinsTip, true, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG, true, true, ptd_spend_tx, nullptr));

            // If we call again asking for scriptchecks (as happens in
            // ConnectBlock), we should add a script check object for this -- we're
            // not caching invalidity (if that changes, delete this test case).
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputs(spend_tx, state, pcoinsTip, true, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG, true, true, ptd_spend_tx, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), (uint64_t)1);

            // Test that CheckInputs returns true iff DERSIG-enforcing flags are
            // not present.  Don't add these checks to the cache, so that we can
            // test later that block validation works fine in the absence of cached
            // successes.
            ValidateCheckInputsForAllFlags(spend_tx, SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC, false, false);

            // And if we produce a block with this tx, it should be valid (DERSIG not
            // enabled yet), even though there's no cache entry.
            CBlock block;

            block = CreateAndProcessBlock({spend_tx}, p2pk_scriptPubKey);
            BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
            BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());
        }

        // Test P2SH: construct a transaction that is valid without P2SH, and
        // then test validity with P2SH.
        {
            CMutableTransaction invalid_under_p2sh_tx;
            invalid_under_p2sh_tx.nVersion = 1;
            invalid_under_p2sh_tx.vin.resize(1);
            invalid_under_p2sh_tx.vin[0].prevout.hash = spend_tx.GetHash();
            invalid_under_p2sh_tx.vin[0].prevout.n = 0;
            invalid_under_p2sh_tx.vout.resize(1);
            invalid_under_p2sh_tx.vout[0].nValue = 11 * CENT;
            invalid_under_p2sh_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;
            std::vector<unsigned char> vchSig2(p2pk_scriptPubKey.begin(), p2pk_scriptPubKey.end());
            invalid_under_p2sh_tx.vin[0].scriptSig << vchSig2;

            ValidateCheckInputsForAllFlags(invalid_under_p2sh_tx, SCRIPT_VERIFY_P2SH, true, false);
        }

        // Test CHECKLOCKTIMEVERIFY
        {
            CMutableTransaction invalid_with_cltv_tx;
            invalid_with_cltv_tx.nVersion = 1;
            invalid_with_cltv_tx.nLockTime = 100;
            invalid_with_cltv_tx.vin.resize(1);
            invalid_with_cltv_tx.vin[0].prevout.hash = spend_tx.GetHash();
            invalid_with_cltv_tx.vin[0].prevout.n = 2;
            invalid_with_cltv_tx.vin[0].nSequence = 0;
            invalid_with_cltv_tx.vout.resize(1);
            invalid_with_cltv_tx.vout[0].nValue = 11 * CENT;
            invalid_with_cltv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(spend_tx.vout[2].scriptPubKey, invalid_with_cltv_tx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
            BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
            vchSig.push_back((unsigned char) SIGHASH_ALL);
            invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

            ValidateCheckInputsForAllFlags(invalid_with_cltv_tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, true, true);

            // Make it valid, and check again
            invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
            CValidationState state;
            PrecomputedTransactionData txdata(invalid_with_cltv_tx);
            BOOST_CHECK(CheckInputs(invalid_with_cltv_tx, state, pcoinsTip, true, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, true, true, txdata, nullptr));
        }

        // TEST CHECKSEQUENCEVERIFY
        {
            CMutableTransaction invalid_with_csv_tx;
            invalid_with_csv_tx.nVersion = 2;
            invalid_with_csv_tx.vin.resize(1);
            invalid_with_csv_tx.vin[0].prevout.hash = spend_tx.GetHash();
            invalid_with_csv_tx.vin[0].prevout.n = 3;
            invalid_with_csv_tx.vin[0].nSequence = 100;
            invalid_with_csv_tx.vout.resize(1);
            invalid_with_csv_tx.vout[0].nValue = 11 * CENT;
            invalid_with_csv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(spend_tx.vout[3].scriptPubKey, invalid_with_csv_tx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
            BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
            vchSig.push_back((unsigned char) SIGHASH_ALL);
            invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

            ValidateCheckInputsForAllFlags(invalid_with_csv_tx, SCRIPT_VERIFY_CHECKSEQUENCEVERIFY, true, true);

            // Make it valid, and check again
            invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
            CValidationState state;
            PrecomputedTransactionData txdata(invalid_with_csv_tx);
            BOOST_CHECK(CheckInputs(invalid_with_csv_tx, state, pcoinsTip, true, SCRIPT_VERIFY_CHECKSEQUENCEVERIFY, true, true, txdata, nullptr));
        }

        // TODO: add tests for remaining script flags

        // Test that passing CheckInputs with a valid witness doesn't imply success
        // for the same tx with a different witness.
        {
            CMutableTransaction valid_with_witness_tx;
            valid_with_witness_tx.nVersion = 1;
            valid_with_witness_tx.vin.resize(1);
            valid_with_witness_tx.vin[0].prevout.hash = spend_tx.GetHash();
            valid_with_witness_tx.vin[0].prevout.n = 1;
            valid_with_witness_tx.vout.resize(1);
            valid_with_witness_tx.vout[0].nValue = 11 * CENT;
            valid_with_witness_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            SignatureData sigdata;
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &valid_with_witness_tx, 0, 11 * CENT, SIGHASH_ALL), spend_tx.vout[1].scriptPubKey, sigdata);
            UpdateTransaction(valid_with_witness_tx, 0, sigdata);

            // This should be valid under all script flags.
            ValidateCheckInputsForAllFlags(valid_with_witness_tx, 0, true, false);

            // Remove the witness, and check that it is now invalid.
            valid_with_witness_tx.vin[0].scriptWitness.SetNull();
            ValidateCheckInputsForAllFlags(valid_with_witness_tx, SCRIPT_VERIFY_WITNESS, true, false);
        }

        {
            // Test a transaction with multiple inputs.
            CMutableTransaction tx;

            tx.nVersion = 1;
            tx.vin.resize(2);
            tx.vin[0].prevout.hash = spend_tx.GetHash();
            tx.vin[0].prevout.n = 0;
            tx.vin[1].prevout.hash = spend_tx.GetHash();
            tx.vin[1].prevout.n = 1;
            tx.vout.resize(1);
            tx.vout[0].nValue = 22 * CENT;
            tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            for (int i = 0; i < 2; ++i)
            {
                SignatureData sigdata;
                ProduceSignature(MutableTransactionSignatureCreator(&keystore, &tx, i, 11 * CENT, SIGHASH_ALL), spend_tx.vout[i].scriptPubKey, sigdata);
                UpdateTransaction(tx, i, sigdata);
            }

            // This should be valid under all script flags
            ValidateCheckInputsForAllFlags(tx, 0, true, false);

            // Check that if the second input is invalid, but the first input is
            // valid, the transaction is not cached.
            // Invalidate vin[1]
            tx.vin[1].scriptWitness.SetNull();

            CValidationState state;
            PrecomputedTransactionData txdata(tx);
            // This transaction is now invalid under segwit, because of the second input.
            BOOST_CHECK(!CheckInputs(tx, state, pcoinsTip, true, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true, true, txdata, nullptr));

            std::vector<CScriptCheck> scriptchecks;
            // Make sure this transaction was not cached (ie because the first
            // input was valid)
            BOOST_CHECK(CheckInputs(tx, state, pcoinsTip, true, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true, true, txdata, &scriptchecks));
            // Should get 2 script checks back -- caching is on a whole-transaction basis.
            BOOST_CHECK_EQUAL(scriptchecks.size(), (uint64_t)2);
        }
    }

    BOOST_FIXTURE_TEST_CASE(pq_script_cache_separates_network_context, TestChain100Setup)
    {
        LOCK(cs_main);
        InitScriptExecutionCache();

        const std::unique_ptr<CChainParams> mainParams = CreateChainParams("main");
        const std::unique_ptr<CChainParams> testParams = CreateChainParams("test");
        BOOST_REQUIRE(mainParams);
        BOOST_REQUIRE(testParams);
        const Consensus::PQSignatureContext& mainContext =
            mainParams->GetConsensus().pqSignatureContext;
        const Consensus::PQSignatureContext& testContext =
            testParams->GetConsensus().pqSignatureContext;

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
        const COutPoint prevout(fundingTx.GetHash(), 0);
        pcoinsTip->AddCoin(prevout,
            Coin(fundingTx.vout[0], chainActive.Height(), false), false);

        CMutableTransaction spend;
        spend.vin.emplace_back(prevout);
        spend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        BOOST_REQUIRE(SignSignature(keystore, fundingTx, spend, 0, SIGHASH_ALL,
                                    mainContext));
        const CTransaction tx(spend);
        PrecomputedTransactionData txdata(tx);
        const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS |
                                   SCRIPT_VERIFY_PQ_HYBRID;

        CValidationState mainState;
        BOOST_REQUIRE(CheckInputs(tx, mainState, *pcoinsTip, true, flags,
                                  true, true, txdata, nullptr, mainContext));

        // Identical tx, wtxid, and flags must not reuse a success cached for a
        // different network context.
        CValidationState testState;
        BOOST_CHECK(!CheckInputs(tx, testState, *pcoinsTip, true, flags,
                                 true, true, txdata, nullptr, testContext));

        std::vector<CScriptCheck> checks;
        CValidationState cachedMainState;
        BOOST_CHECK(CheckInputs(tx, cachedMainState, *pcoinsTip, true, flags,
                                true, true, txdata, &checks, mainContext));
        BOOST_CHECK(checks.empty());

        CValidationState missingContextState;
        BOOST_CHECK(!CheckInputs(tx, missingContextState, *pcoinsTip, true,
                                 flags, true, true, txdata, nullptr,
                                 Consensus::NullPQSignatureContext()));
        BOOST_CHECK(missingContextState.IsError());

        // Assumevalid may skip classical scripts, but never an active native
        // witness-v2 signature. This call returned true before FINDING-069.
        CValidationState assumedWrongContextState;
        BOOST_CHECK(!CheckInputs(tx, assumedWrongContextState, *pcoinsTip,
                                 false, flags, false, false, txdata, nullptr,
                                 testContext));

        // Even if a caller supplies a deferred-check vector, selective
        // assumevalid validation executes PQ checks inline and cannot be lost
        // through a disabled worker queue.
        std::vector<CScriptCheck> assumedDeferredChecks;
        CValidationState assumedDeferredState;
        BOOST_CHECK(!CheckInputs(tx, assumedDeferredState, *pcoinsTip,
                                 false, flags, false, false, txdata,
                                 &assumedDeferredChecks, testContext));
        BOOST_CHECK(assumedDeferredChecks.empty());

        // P2SH is checked conservatively because it can hide witness-v2.
        const CScript pqScript =
            GetScriptForWitnessV2PQ(pubkey.GetWitnessProgram());
        BOOST_REQUIRE(keystore.AddCScript(pqScript));
        CMutableTransaction wrappedFunding;
        wrappedFunding.vout.emplace_back(amount,
            GetScriptForDestination(CScriptID(pqScript)));
        const CTransaction wrappedFundingTx(wrappedFunding);
        const COutPoint wrappedPrevout(wrappedFundingTx.GetHash(), 0);
        pcoinsTip->AddCoin(wrappedPrevout,
            Coin(wrappedFundingTx.vout[0], chainActive.Height(), false), false);

        CMutableTransaction wrappedSpend;
        wrappedSpend.vin.emplace_back(wrappedPrevout);
        wrappedSpend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        BOOST_REQUIRE(SignSignature(keystore, wrappedFundingTx, wrappedSpend,
                                    0, SIGHASH_ALL, mainContext));
        const CTransaction wrappedTx(wrappedSpend);
        PrecomputedTransactionData wrappedTxData(wrappedTx);
        CValidationState assumedWrappedMainContextState;
        BOOST_CHECK(CheckInputs(wrappedTx,
                                assumedWrappedMainContextState,
                                *pcoinsTip, false, flags, false, false,
                                wrappedTxData, nullptr, mainContext));
        CValidationState assumedWrappedWrongContextState;
        BOOST_CHECK(!CheckInputs(wrappedTx,
                                 assumedWrappedWrongContextState,
                                 *pcoinsTip, false, flags, false, false,
                                 wrappedTxData, nullptr, testContext));

        // Active witness-v2 includes malformed program lengths. Assumevalid
        // must not classify those prevouts as classical and skip rejection.
        CMutableTransaction malformedV2Funding;
        malformedV2Funding.vout.emplace_back(
            amount, CScript() << OP_2 << std::vector<unsigned char>(31, 0x01));
        const CTransaction malformedV2FundingTx(malformedV2Funding);
        const COutPoint malformedV2Prevout(malformedV2FundingTx.GetHash(), 0);
        pcoinsTip->AddCoin(malformedV2Prevout,
            Coin(malformedV2FundingTx.vout[0], chainActive.Height(), false), false);
        CMutableTransaction malformedV2Spend;
        malformedV2Spend.vin.emplace_back(malformedV2Prevout);
        malformedV2Spend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        const CTransaction malformedV2Tx(malformedV2Spend);
        PrecomputedTransactionData malformedV2TxData(malformedV2Tx);
        CValidationState assumedMalformedV2State;
        BOOST_CHECK(!CheckInputs(malformedV2Tx, assumedMalformedV2State,
                                 *pcoinsTip, false, flags, false, false,
                                 malformedV2TxData, nullptr, mainContext));

        // The Core assumevalid optimization remains available to classical
        // non-P2SH inputs even while the RIP-25 flag is active.
        CMutableTransaction classicalFunding;
        classicalFunding.vout.emplace_back(amount, CScript() << OP_FALSE);
        const CTransaction classicalFundingTx(classicalFunding);
        const COutPoint classicalPrevout(classicalFundingTx.GetHash(), 0);
        pcoinsTip->AddCoin(classicalPrevout,
            Coin(classicalFundingTx.vout[0], chainActive.Height(), false), false);
        CMutableTransaction classicalSpend;
        classicalSpend.vin.emplace_back(classicalPrevout);
        classicalSpend.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);
        const CTransaction classicalTx(classicalSpend);
        PrecomputedTransactionData classicalTxData(classicalTx);
        CValidationState assumedClassicalState;
        BOOST_CHECK(CheckInputs(classicalTx, assumedClassicalState,
                                *pcoinsTip, false, flags, false, false,
                                classicalTxData, nullptr, mainContext));
    }

BOOST_AUTO_TEST_SUITE_END()
