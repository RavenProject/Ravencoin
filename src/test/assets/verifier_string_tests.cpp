// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>

#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <base58.h>
#include <chainparams.h>

#include "LibBoolEE.h"

BOOST_FIXTURE_TEST_SUITE(verifier_string_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(boolean_expression_evaluator_test)
    {
        BOOST_TEST_MESSAGE("Running Boolean Expression Evaluator Test");

        LibBoolEE::Vals vals = { { "#KY_C", true }, { "#CI.A", false } };
        BOOST_CHECK(LibBoolEE::resolve("#KY_C & !#CI.A", vals));
        BOOST_CHECK_THROW(LibBoolEE::resolve("#KY_C|#MISS", vals), std::runtime_error);
        BOOST_CHECK_THROW(LibBoolEE::resolve("BAD -- EXPRESSION -- BUST", vals), std::runtime_error);

        //! Check for valid syntax
        std::string valid_verifier_syntax = "((#KYC & !#ABC) | #DEF & #GHI & #RET) | (#TEST)";

        std::string stripped_valid_verifier_syntax = GetStrippedVerifierString(valid_verifier_syntax);

        std::set<std::string> setQualifiers;
        ExtractVerifierStringQualifiers(stripped_valid_verifier_syntax, setQualifiers);

        vals.clear();

        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK(LibBoolEE::resolve(stripped_valid_verifier_syntax, vals));

        // Clear the vals, and insert them with false, this should make the resolve return false
        vals.clear();
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, false));
        }

        BOOST_CHECK(!LibBoolEE::resolve(stripped_valid_verifier_syntax, vals));

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax #DEF#XXX won't work
        std::string not_valid_qualifier = "((#KYC & !#ABC) | #DEF#XXX~ & #GHI & #RET)";
        std::string stripped_not_valid_qualifier = GetStrippedVerifierString(not_valid_qualifier);
        ExtractVerifierStringQualifiers(stripped_not_valid_qualifier, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_qualifier, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, missing a parenthesis '(' at the beginning
        std::string not_valid_missing_parenthesis = "(#KYC & !#ABC) | #DEF & #GHI & #RET)";

        std::string stripped_not_valid_missing_parenthesis = GetStrippedVerifierString(not_valid_missing_parenthesis);
        ExtractVerifierStringQualifiers(stripped_not_valid_missing_parenthesis, setQualifiers);

        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_missing_parenthesis, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has two & in a row
        std::string not_valid_double_and = "((#KYC && !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_double_and = GetStrippedVerifierString(not_valid_double_and);
        ExtractVerifierStringQualifiers(stripped_not_valid_double_and, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_double_and, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has two | in a row
        std::string not_valid_double_or = "((#KYC & !#ABC) || #DEF & #GHI & #RET)";
        std::string stripped_not_valid_double_or = GetStrippedVerifierString(not_valid_double_or);
        ExtractVerifierStringQualifiers(stripped_not_valid_double_or, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_double_or, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has & | without a qualifier inbetween
        std::string not_valid_missing_qualifier = "((#KYC & | !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_missing_qualifier = GetStrippedVerifierString(not_valid_missing_qualifier);
        ExtractVerifierStringQualifiers(stripped_not_valid_missing_qualifier, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_missing_qualifier, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has () without qualifier inside them
        std::string not_valid_open_close = "()((#KYC & !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_open_close = GetStrippedVerifierString(not_valid_open_close);
        ExtractVerifierStringQualifiers(stripped_not_valid_open_close, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_open_close, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has (#YES) followed by no comparator
        std::string not_valid_open_close_no_comparator = "((#KYC & !#ABC) | (#YES) #DEF & #GHI & #RET)";
        std::string stripped_not_valid_open_close_no_comparator = GetStrippedVerifierString(not_valid_open_close_no_comparator);
        ExtractVerifierStringQualifiers(stripped_not_valid_open_close_no_comparator, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(not_valid_open_close_no_comparator, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, could return true on the #KYC, but is missing a ending parenthesis
        std::string bad_syntax_after_true_statement = "((#KYC) | #GHI";
        std::string stripped_bad_syntax_after_true_statement = GetStrippedVerifierString(bad_syntax_after_true_statement);
        ExtractVerifierStringQualifiers(stripped_bad_syntax_after_true_statement, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(bad_syntax_after_true_statement, vals), std::runtime_error);
    }


    BOOST_AUTO_TEST_CASE(verifier_check_asset_txout)
    {
        BOOST_TEST_MESSAGE("Running CheckVerifierAssetTxOut Tests");

        std::string error = "";
        CTxOut txout;
        CScript scriptPubKey;
        txout.nValue = 0;

        /// Check invalid verifier strings ///
        {
            // Using a unstripped verifier string (contains whitespaces and #)
            CNullAssetTxVerifierString invalid_verifier_1("#KYC&#KYC2");
            invalid_verifier_1.ConstructTransaction(scriptPubKey);
            txout.scriptPubKey = scriptPubKey;

            BOOST_CHECK_MESSAGE(!CheckVerifierAssetTxOut(txout, error), "Verifier String didn't fail when it wasn't stripped of #");

            scriptPubKey.clear();
            CNullAssetTxVerifierString invalid_verifier_2("KYC & KYC2");
            invalid_verifier_2.ConstructTransaction(scriptPubKey);
            txout.scriptPubKey = scriptPubKey;

            BOOST_CHECK_MESSAGE(!CheckVerifierAssetTxOut(txout, error), "Verifier String didn't fail when it wasn't stripped of whitespaces");

        }

        /// Check valid verifier strings ///
        {
            scriptPubKey.clear();
            CNullAssetTxVerifierString valid_verifier_1("KYC&KYC2");
            valid_verifier_1.ConstructTransaction(scriptPubKey);
            txout.scriptPubKey = scriptPubKey;

            BOOST_CHECK_MESSAGE(CheckVerifierAssetTxOut(txout, error), "Verifier String failed when it was stripped correctly");
        }
    }

    BOOST_AUTO_TEST_CASE(check_verifier_string)
    {
        BOOST_TEST_MESSAGE("Running CheckVerifierString Tests");

        std::string error = "";
        std::set<std::string> setFoundQualifiers;

        /// Check invalid verifier strings ///
        {
            std::string invalid_verifier = "";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 1 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 2 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)(";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 3 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)(&";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 4 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)(|";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 5 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)(|)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 6 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)(&)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 7 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC)()";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 8 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 9 didn't fail - " + invalid_verifier);

            invalid_verifier = "$KYC";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 10 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC/SUB";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 11 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC$UNIQUE";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 12 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC~MSGCHANNEL";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 13 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC._";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 14 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC.|";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 15 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC &";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 16 didn't fail - " + invalid_verifier);

            invalid_verifier = "&";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 17 didn't fail - " + invalid_verifier);

            invalid_verifier = "(!)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 18 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC81";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 19 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC && TEST)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 20 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC || TEST)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 21 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC |& TEST)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 22 didn't fail - " + invalid_verifier);

            invalid_verifier = "(KYC &| TEST)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 23 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC & | TEST";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 24 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC () TEST";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 25 didn't fail - " + invalid_verifier);

            invalid_verifier = "KYC ( ) TEST";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 26 didn't fail - " + invalid_verifier);

            invalid_verifier = "(true)";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 27 didn't fail - " + invalid_verifier);

            invalid_verifier = "!@#$%^&*()";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 28 didn't fail - " + invalid_verifier);

            invalid_verifier = "()";
            BOOST_CHECK_MESSAGE(!CheckVerifierString(invalid_verifier, setFoundQualifiers, error), "Invalid Verifier String Test 29 didn't fail - " + invalid_verifier);

        }

        /// Check valid verifier strings ///
        {
            setFoundQualifiers.clear();
            std::string verifier = "true";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String failed when it was set to true");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.empty(), "Qualifier set was not empty");

            setFoundQualifiers.clear();
            verifier = "(KYC & !TEST & YMC) | (TAG & SKIP)";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 1 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 5);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KYC"), "TEST 1 set didn't have #KYC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TEST"), "TEST 1 set didn't have #TEST");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("YMC"), "TEST 1 set didn't have #YMC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TAG"), "TEST 1 set didn't have #TAG");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("SKIP"), "TEST 1 set didn't have #SKIP");

            setFoundQualifiers.clear();
            verifier = "KYC & !TEST & YMC | TAG & SKIP";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 2 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 5);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KYC"), "TEST 2 set didn't have #KYC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TEST"), "TEST 2 set didn't have #TEST");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("YMC"), "TEST 2 set didn't have #YMC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TAG"), "TEST 2 set didn't have #TAG");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("SKIP"), "TEST 2 set didn't have #SKIP");

            setFoundQualifiers.clear();
            verifier = "(KYC & !TEST & YMC | TAG & SKIP) | (TAG & KYC & TEST)";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 3 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 5);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KYC"), "TEST 3 set didn't have #KYC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TEST"), "TEST 3 set didn't have #TEST");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("YMC"), "TEST 3 set didn't have #YMC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TAG"), "TEST 3 set didn't have #TAG");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("SKIP"), "TEST 3 set didn't have #SKIP");



            setFoundQualifiers.clear();
            verifier = "(KYC&!TEST&YMC|TAG&SKIP)|(TAG&KYC&TEST)";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 4 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 5);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KYC"), "TEST 4 set didn't have #KYC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TEST"), "TEST 4 set didn't have #TEST");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("YMC"), "TEST 4 set didn't have #YMC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TAG"), "TEST 4 set didn't have #TAG");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("SKIP"), "TEST 4 set didn't have #SKIP");


            // 80 character limit on stripped strings
            setFoundQualifiers.clear();
            verifier = "KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KYC&KY80";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 5 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 2);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KYC"), "TEST 5 set didn't have #KYC");
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("KY80"), "TEST 5 set didn't have #KY80");


            setFoundQualifiers.clear();
            verifier = "((((((((((((((((((((((((((((((((((((TTT))))))))))))))))))))))))))))))))))))";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 6 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 1);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TTT"), "TEST 6 set didn't have #TTT");

            setFoundQualifiers.clear();
            verifier = "TEST&!TEST";
            BOOST_CHECK_MESSAGE(CheckVerifierString(verifier, setFoundQualifiers, error), "Verifier String Test 7 failed - " + verifier + " - " + error);
            BOOST_CHECK(setFoundQualifiers.size() == 1);
            BOOST_CHECK_MESSAGE(setFoundQualifiers.count("TEST"), "TEST 7 set didn't have #TEST");
        }
    }


BOOST_AUTO_TEST_SUITE_END()
