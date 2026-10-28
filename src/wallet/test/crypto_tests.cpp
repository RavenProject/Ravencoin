// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_raven.h"
#include "hash.h"
#include "utilstrencodings.h"
#include "wallet/bip39.h"
#include "wallet/crypter.h"

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(wallet_crypto, BasicTestingSetup)

    class TestKeyStore : public CCryptoKeyStore
    {
    public:
        bool PrepareUnlockedSecrets(CKeyingMaterial& masterKey)
        {
            CKey key;
            key.MakeNewKey(true);
            const std::string mnemonic =
                "abandon abandon abandon abandon abandon abandon abandon "
                "abandon abandon abandon abandon about";
            const std::string password = "TREZOR";
            const std::vector<unsigned char> words(mnemonic.begin(), mnemonic.end());
            const std::vector<unsigned char> passphrase(password.begin(), password.end());
            const std::vector<unsigned char> seed = ParseHex(
                "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e5349553"
                "1f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04");

            if (!AddKeyPubKey(key, key.GetPubKey()) ||
                !AddWords(Hash(words.begin(), words.end()), words) ||
                !AddPassphrase(passphrase) || !AddVchSeed(seed) ||
                !EncryptKeys(masterKey) || !EncryptBip39(masterKey) ||
                !Lock() || !PlaintextSecretStorageReleased()) {
                return false;
            }

            return Unlock(masterKey);
        }

        bool HasAllocatedPlaintextSecrets() const
        {
            LOCK(cs_KeyStore);
            return !vMasterKey.empty() && vMasterKey.capacity() > 0 &&
                   !vchWords.empty() && vchWords.capacity() > 0 &&
                   !vchPassphrase.empty() && vchPassphrase.capacity() > 0 &&
                   !g_vchSeed.empty() && g_vchSeed.capacity() > 0;
        }

        bool PlaintextSecretStorageReleased() const
        {
            LOCK(cs_KeyStore);
            return vMasterKey.empty() && vMasterKey.capacity() == 0 &&
                   vchWords.empty() && vchWords.capacity() == 0 &&
                   vchPassphrase.empty() && vchPassphrase.capacity() == 0 &&
                   g_vchSeed.empty() && g_vchSeed.capacity() == 0;
        }

        bool UnlockForTest(const CKeyingMaterial& masterKey)
        {
            return Unlock(masterKey);
        }

        void TruncateEncryptedWords()
        {
            LOCK(cs_KeyStore);
            vchCryptedBip39Words.pop_back();
        }

        void TruncateEncryptedPassphrase()
        {
            LOCK(cs_KeyStore);
            vchCryptedBip39Passphrase.pop_back();
        }

        void TruncateEncryptedSeed()
        {
            LOCK(cs_KeyStore);
            vchCryptedBip39VchSeed.pop_back();
        }

        void TruncateEncryptedClassicalKey()
        {
            LOCK(cs_KeyStore);
            mapCryptedKeys.begin()->second.second.pop_back();
        }

        size_t EncryptedBip39BlockCount(int record)
        {
            LOCK(cs_KeyStore);
            const std::vector<unsigned char>& crypted = record == 0
                ? vchCryptedBip39Words
                : (record == 1 ? vchCryptedBip39Passphrase : vchCryptedBip39VchSeed);
            return crypted.size() / WALLET_CRYPTO_IV_SIZE;
        }

        void CorruptEncryptedBip39Block(int record, size_t block)
        {
            LOCK(cs_KeyStore);
            std::vector<unsigned char>& crypted = record == 0
                ? vchCryptedBip39Words
                : (record == 1 ? vchCryptedBip39Passphrase : vchCryptedBip39VchSeed);
            crypted[block * WALLET_CRYPTO_IV_SIZE] ^= 1;
        }

        bool ReencryptWithWrongSeed(CKeyingMaterial& masterKey)
        {
            LOCK(cs_KeyStore);
            g_vchSeed[0] ^= 1;
            return EncryptBip39(masterKey);
        }

        bool ReencryptWithWrongWordHash(CKeyingMaterial& masterKey)
        {
            LOCK(cs_KeyStore);
            nWordHash.begin()[0] ^= 1;
            return EncryptBip39(masterKey);
        }

        bool ReencryptWithInvalidWords(CKeyingMaterial& masterKey)
        {
            LOCK(cs_KeyStore);
            vchWords.back() = 'x';
            nWordHash = Hash(vchWords.begin(), vchWords.end());
            return EncryptBip39(masterKey);
        }
    };

    class TestCrypter
    {
    public:
        static void TestPassphraseSingle(const std::vector<unsigned char> &vchSalt, const SecureString &passphrase, uint32_t rounds,
                                         const std::vector<unsigned char> &correctKey = std::vector<unsigned char>(),
                                         const std::vector<unsigned char> &correctIV = std::vector<unsigned char>())
        {
            CCrypter crypt;
            crypt.SetKeyFromPassphrase(passphrase, vchSalt, rounds, 0);

            if (!correctKey.empty())
                BOOST_CHECK_MESSAGE(memcmp(crypt.vchKey.data(), correctKey.data(), crypt.vchKey.size()) == 0, \
            HexStr(crypt.vchKey.begin(), crypt.vchKey.end()) + std::string(" != ") + HexStr(correctKey.begin(), correctKey.end()));
            if (!correctIV.empty())
                BOOST_CHECK_MESSAGE(memcmp(crypt.vchIV.data(), correctIV.data(), crypt.vchIV.size()) == 0,
                                    HexStr(crypt.vchIV.begin(), crypt.vchIV.end()) + std::string(" != ") + HexStr(correctIV.begin(), correctIV.end()));
        }

        static void TestPassphrase(const std::vector<unsigned char> &vchSalt, const SecureString &passphrase, uint32_t rounds,
                                   const std::vector<unsigned char> &correctKey = std::vector<unsigned char>(),
                                   const std::vector<unsigned char> &correctIV = std::vector<unsigned char>())
        {
            TestPassphraseSingle(vchSalt, passphrase, rounds, correctKey, correctIV);
            for (SecureString::const_iterator i(passphrase.begin()); i != passphrase.end(); ++i)
                TestPassphraseSingle(vchSalt, SecureString(i, passphrase.end()), rounds);
        }

        static void TestDecrypt(const CCrypter &crypt, const std::vector<unsigned char> &vchCiphertext, \
                        const std::vector<unsigned char> &vchPlaintext = std::vector<unsigned char>())
        {
            CKeyingMaterial vchDecrypted;
            crypt.Decrypt(vchCiphertext, vchDecrypted);
            if (vchPlaintext.size())
                BOOST_CHECK(CKeyingMaterial(vchPlaintext.begin(), vchPlaintext.end()) == vchDecrypted);
        }

        static void TestEncryptSingle(const CCrypter &crypt, const CKeyingMaterial &vchPlaintext,
                                      const std::vector<unsigned char> &vchCiphertextCorrect = std::vector<unsigned char>())
        {
            std::vector<unsigned char> vchCiphertext;
            crypt.Encrypt(vchPlaintext, vchCiphertext);

            if (!vchCiphertextCorrect.empty())
                BOOST_CHECK(vchCiphertext == vchCiphertextCorrect);

            const std::vector<unsigned char> vchPlaintext2(vchPlaintext.begin(), vchPlaintext.end());
            TestDecrypt(crypt, vchCiphertext, vchPlaintext2);
        }

        static void TestEncrypt(const CCrypter &crypt, const std::vector<unsigned char> &vchPlaintextIn, \
                       const std::vector<unsigned char> &vchCiphertextCorrect = std::vector<unsigned char>())
        {
            TestEncryptSingle(crypt, CKeyingMaterial(vchPlaintextIn.begin(), vchPlaintextIn.end()), vchCiphertextCorrect);
            for (std::vector<unsigned char>::const_iterator i(vchPlaintextIn.begin()); i != vchPlaintextIn.end(); ++i)
                TestEncryptSingle(crypt, CKeyingMaterial(i, vchPlaintextIn.end()));
        }

    };

    BOOST_AUTO_TEST_CASE(passphrase_test)
    {
        BOOST_TEST_MESSAGE("Running PassPhrase Test");

        // These are expensive.
        TestCrypter::TestPassphrase(ParseHex("0000deadbeef0000"), "test", 25000, \
                                ParseHex("fc7aba077ad5f4c3a0988d8daa4810d0d4a0e3bcb53af662998898f33df0556a"), \
                                ParseHex("cf2f2691526dd1aa220896fb8bf7c369"));

        std::string hash(GetRandHash().ToString());
        std::vector<unsigned char> vchSalt(8);
        GetRandBytes(vchSalt.data(), vchSalt.size());
        uint32_t rounds = InsecureRand32();
        if (rounds > 30000)
            rounds = 30000;
        TestCrypter::TestPassphrase(vchSalt, SecureString(hash.begin(), hash.end()), rounds);
    }

    BOOST_AUTO_TEST_CASE(encrypt_test)
    {
        BOOST_TEST_MESSAGE("Running Encrypt Test");

        std::vector<unsigned char> vchSalt = ParseHex("0000deadbeef0000");
        BOOST_CHECK(vchSalt.size() == WALLET_CRYPTO_SALT_SIZE);
        CCrypter crypt;
        crypt.SetKeyFromPassphrase("passphrase", vchSalt, 25000, 0);
        TestCrypter::TestEncrypt(crypt, ParseHex("22bcade09ac03ff6386914359cfe885cfeb5f77ff0d670f102f619687453b29d"));

        for (int i = 0; i != 100; i++)
        {
            uint256 hash(GetRandHash());
            TestCrypter::TestEncrypt(crypt, std::vector<unsigned char>(hash.begin(), hash.end()));
        }

    }

    BOOST_AUTO_TEST_CASE(decrypt_test)
    {
        BOOST_TEST_MESSAGE("Running Decrypt Test");

        std::vector<unsigned char> vchSalt = ParseHex("0000deadbeef0000");
        BOOST_CHECK(vchSalt.size() == WALLET_CRYPTO_SALT_SIZE);
        CCrypter crypt;
        crypt.SetKeyFromPassphrase("passphrase", vchSalt, 25000, 0);

        // Some corner cases the came up while testing
        TestCrypter::TestDecrypt(crypt, ParseHex("795643ce39d736088367822cdc50535ec6f103715e3e48f4f3b1a60a08ef59ca"));
        TestCrypter::TestDecrypt(crypt, ParseHex("de096f4a8f9bd97db012aa9d90d74de8cdea779c3ee8bc7633d8b5d6da703486"));
        TestCrypter::TestDecrypt(crypt, ParseHex("32d0a8974e3afd9c6c3ebf4d66aa4e6419f8c173de25947f98cf8b7ace49449c"));
        TestCrypter::TestDecrypt(crypt, ParseHex("e7c055cca2faa78cb9ac22c9357a90b4778ded9b2cc220a14cea49f931e596ea"));
        TestCrypter::TestDecrypt(crypt, ParseHex("b88efddd668a6801d19516d6830da4ae9811988ccbaf40df8fbb72f3f4d335fd"));
        TestCrypter::TestDecrypt(crypt, ParseHex("8cae76aa6a43694e961ebcb28c8ca8f8540b84153d72865e8561ddd93fa7bfa9"));

        for (int i = 0; i != 100; i++)
        {
            uint256 hash(GetRandHash());
            TestCrypter::TestDecrypt(crypt, std::vector<unsigned char>(hash.begin(), hash.end()));
        }
    }

    BOOST_AUTO_TEST_CASE(malformed_cbc_input_releases_output)
    {
        CCrypter crypt;
        const CKeyingMaterial key(WALLET_CRYPTO_KEY_SIZE, 0x11);
        const std::vector<unsigned char> iv(WALLET_CRYPTO_IV_SIZE, 0x22);
        BOOST_REQUIRE(crypt.SetKey(key, iv));

        const CKeyingMaterial expected{0x31, 0x32};
        std::vector<unsigned char> ciphertext;
        BOOST_REQUIRE(crypt.Encrypt(expected, ciphertext));
        CKeyingMaterial plaintext;
        BOOST_REQUIRE(crypt.Decrypt(ciphertext, plaintext));
        BOOST_CHECK(plaintext == expected);

        for (size_t size : {size_t(0), size_t(1), size_t(15), size_t(16), size_t(17)}) {
            plaintext.assign(32, 0xa5);
            BOOST_CHECK(!crypt.Decrypt(std::vector<unsigned char>(size, 0x42), plaintext));
            BOOST_CHECK(plaintext.empty());
        }

        ciphertext.assign(16, 0x42);
        BOOST_CHECK(!crypt.Encrypt(CKeyingMaterial(), ciphertext));
        BOOST_CHECK(ciphertext.empty());
    }

    BOOST_AUTO_TEST_CASE(lock_cleanses_and_releases_plaintext_secret_storage)
    {
        TestKeyStore keystore;
        CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);

        BOOST_REQUIRE(keystore.PrepareUnlockedSecrets(masterKey));
        BOOST_REQUIRE(keystore.HasAllocatedPlaintextSecrets());
        BOOST_REQUIRE(keystore.Lock());
        BOOST_CHECK(keystore.IsLocked());
        BOOST_CHECK(keystore.PlaintextSecretStorageReleased());
    }

    BOOST_AUTO_TEST_CASE(corrupt_bip39_unlock_is_atomic)
    {
        CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);
        for (int mode = 0; mode < 6; ++mode) {
            TestKeyStore keystore;
            BOOST_REQUIRE(keystore.PrepareUnlockedSecrets(masterKey));
            if (mode == 0) keystore.TruncateEncryptedWords();
            if (mode == 1) keystore.TruncateEncryptedPassphrase();
            if (mode == 2) keystore.TruncateEncryptedSeed();
            if (mode == 3) BOOST_REQUIRE(keystore.ReencryptWithWrongSeed(masterKey));
            if (mode == 4) BOOST_REQUIRE(keystore.ReencryptWithWrongWordHash(masterKey));
            if (mode == 5) BOOST_REQUIRE(keystore.ReencryptWithInvalidWords(masterKey));
            BOOST_REQUIRE(keystore.Lock());
            BOOST_CHECK(!keystore.UnlockForTest(masterKey));
            BOOST_CHECK(keystore.IsLocked());
            BOOST_CHECK(keystore.PlaintextSecretStorageReleased());
        }
    }

    BOOST_AUTO_TEST_CASE(corrupt_classical_key_cannot_publish_bip39_plaintext)
    {
        TestKeyStore keystore;
        CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);
        BOOST_REQUIRE(keystore.PrepareUnlockedSecrets(masterKey));
        BOOST_REQUIRE(keystore.Lock());
        keystore.TruncateEncryptedClassicalKey();
        BOOST_CHECK(!keystore.UnlockForTest(masterKey));
        BOOST_CHECK(keystore.IsLocked());
        BOOST_CHECK(keystore.PlaintextSecretStorageReleased());
    }

    BOOST_AUTO_TEST_CASE(bip39_cbc_mutation_in_every_block_rejects)
    {
        CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);
        for (int record = 0; record < 3; ++record) {
            TestKeyStore countStore;
            BOOST_REQUIRE(countStore.PrepareUnlockedSecrets(masterKey));
            const size_t blockCount = countStore.EncryptedBip39BlockCount(record);
            BOOST_REQUIRE(blockCount > 0);
            for (size_t block = 0; block < blockCount; ++block) {
                TestKeyStore keystore;
                BOOST_REQUIRE(keystore.PrepareUnlockedSecrets(masterKey));
                BOOST_REQUIRE(keystore.Lock());
                keystore.CorruptEncryptedBip39Block(record, block);
                BOOST_CHECK(!keystore.UnlockForTest(masterKey));
                BOOST_CHECK(keystore.IsLocked());
                BOOST_CHECK(keystore.PlaintextSecretStorageReleased());
            }
        }
    }

BOOST_AUTO_TEST_SUITE_END()
