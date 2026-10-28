// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypter.h"

#include "crypto/aes.h"
#include "crypto/sha512.h"
#include "hash.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include "utilstrencodings.h"
#include "wallet/bip39.h"

#include <limits>
#include <string>
#include <vector>

int CCrypter::BytesToKeySHA512AES(const std::vector<unsigned char>& chSalt, const SecureString& strKeyData, int count, unsigned char *key,unsigned char *iv) const
{
    // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
    // cipher and sha512 message digest. Because sha512's output size (64b) is
    // greater than the aes256 block size (16b) + aes256 key size (32b),
    // there's no need to process more than once (D_0).

    if(!count || !key || !iv)
        return 0;

    unsigned char buf[CSHA512::OUTPUT_SIZE];
    CSHA512 di;

    di.Write((const unsigned char*)strKeyData.c_str(), strKeyData.size());
    di.Write(chSalt.data(), chSalt.size());
    di.Finalize(buf);

    for(int i = 0; i != count - 1; i++)
        di.Reset().Write(buf, sizeof(buf)).Finalize(buf);

    memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(buf, sizeof(buf));
    return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = BytesToKeySHA512AES(chSalt, strKeyData, nRounds, vchKey.data(), vchIV.data());

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_IV_SIZE)
        return false;

    memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
    memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const
{
    vchCiphertext.clear();
    if (!fKeySet || vchPlaintext.empty() ||
        vchPlaintext.size() > static_cast<size_t>(std::numeric_limits<int>::max() - AES_BLOCKSIZE))
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCKSIZE bytes
    vchCiphertext.resize(vchPlaintext.size() + AES_BLOCKSIZE);

    AES256CBCEncrypt enc(vchKey.data(), vchIV.data(), true);
    int nLen = enc.Encrypt(vchPlaintext.data(), static_cast<int>(vchPlaintext.size()), vchCiphertext.data());
    if (nLen <= static_cast<int>(vchPlaintext.size())) {
        vchCiphertext.clear();
        return false;
    }
    vchCiphertext.resize(nLen);

    return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext) const
{
    CKeyingMaterial().swap(vchPlaintext);
    if (!fKeySet || vchCiphertext.size() < AES_BLOCKSIZE ||
        vchCiphertext.size() % AES_BLOCKSIZE != 0 ||
        vchCiphertext.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    vchPlaintext.resize(vchCiphertext.size());

    AES256CBCDecrypt dec(vchKey.data(), vchIV.data(), true);
    int nLen = dec.Decrypt(vchCiphertext.data(), static_cast<int>(vchCiphertext.size()), vchPlaintext.data());
    if (nLen == 0) {
        CKeyingMaterial().swap(vchPlaintext);
        return false;
    }

    vchPlaintext.resize(nLen);
    return true;
}

static bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(chIV.data(), &nIV, WALLET_CRYPTO_IV_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

static bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(chIV.data(), &nIV, WALLET_CRYPTO_IV_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;

    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

static bool DecryptKey(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
    if (fUseCrypto)
        return true;
    if (!mapKeys.empty() || !mapPQKeys.empty())
        return false;
    fUseCrypto = true;
    return true;
}

void CCryptoKeyStore::ResetCryptedOnAddFailure()
{
    LOCK(cs_KeyStore);
    if (mapCryptedKeys.empty() && mapCryptedPQKeys.empty()) {
        CKeyingMaterial().swap(vMasterKey);
        fUseCrypto = false;
        fDecryptionThoroughlyChecked = false;
    }
}

bool CCryptoKeyStore::LockKeyStore()
{
    if (!SetCrypted())
        return false;

    {
        LOCK(cs_KeyStore);
        CKeyingMaterial().swap(vMasterKey);
        SecureVector().swap(vchWords);
        SecureVector().swap(vchPassphrase);
        SecureVector().swap(g_vchSeed);
    }

    return true;
}

bool CCryptoKeyStore::Lock()
{
    if (!LockKeyStore())
        return false;

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi)
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        // RIP-25: validate decrypted PQ secret/public-key pairs as well.
        // A successful AES decrypt alone is not sufficient: the persisted ML-DSA
        // secret must cryptographically match the public key/witness program.
        if (!keyFail) {
            CryptedPQKeyMap::const_iterator pqi = mapCryptedPQKeys.begin();
            for (; pqi != mapCryptedPQKeys.end(); ++pqi)
            {
                const CPQPubKey &pqPubKey = (*pqi).second.first;
                const std::vector<unsigned char> &vchCryptedSecret = (*pqi).second.second;
                CKeyingMaterial vchSecret;
                if (!DecryptSecret(vMasterKeyIn, vchCryptedSecret, pqPubKey.GetWitnessProgram(), vchSecret))
                {
                    keyFail = true;
                    break;
                }
                CPQKey pqKey;
                if (!pqKey.SetKeyData(vchSecret, pqPubKey))
                {
                    keyFail = true;
                    break;
                }
                keyPass = true;
                if (fDecryptionThoroughlyChecked)
                    break;
            }
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            return false;
        }
        if (keyFail || !keyPass)
            return false;

        CKeyingMaterial validatedMasterKey(vMasterKeyIn);
        if (vchCryptedBip39Words.size() || vchCryptedBip39Passphrase.size() || vchCryptedBip39VchSeed.size()) {
            if (!DecryptBip39(vMasterKeyIn)) {
                LogPrintf("Failed to decrypt or validate BIP39 data\n");
                return false;
            }
        }
        vMasterKey.swap(validatedMasterKey);
        fDecryptionThoroughlyChecked = true;
    }
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::AddKeyPubKey(key, pubkey);

        if (IsLocked())
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.begin(), key.end());
        if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
            return false;

        if (!AddCryptedKey(pubkey, vchCryptedSecret))
            return false;
    }
    return true;
}


bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::AddPQKeyPubKey(const CPQKey &key, const CPQPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
        if (!key.IsValid() || !pubkey.IsValid() || !key.MatchesPubKey(pubkey))
            return false;

        if (!IsCrypted())
            return CBasicKeyStore::AddPQKeyPubKey(key, pubkey);

        if (IsLocked())
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.GetKeyData().begin(), key.GetKeyData().end());
        uint256 witnessProgram = pubkey.GetWitnessProgram();
        if (!EncryptSecret(vMasterKey, vchSecret, witnessProgram, vchCryptedSecret))
            return false;

        if (!AddCryptedPQKey(pubkey, vchCryptedSecret))
            return false;
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedPQKey(const CPQPubKey &pqPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!pqPubKey.IsValid() || !SetCrypted())
            return false;

        mapCryptedPQKeys[pqPubKey.GetWitnessProgram()] = make_pair(pqPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::GetPQKey(const uint256 &witnessProgram, CPQKey &keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPQKey(witnessProgram, keyOut);

        CryptedPQKeyMap::const_iterator mi = mapCryptedPQKeys.find(witnessProgram);
        if (mi != mapCryptedPQKeys.end())
        {
            const CPQPubKey &pqPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKeyingMaterial vchSecret;
            if (!DecryptSecret(vMasterKey, vchCryptedSecret, pqPubKey.GetWitnessProgram(), vchSecret))
                return false;
            return keyOut.SetKeyData(vchSecret, pqPubKey);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetPQPubKey(const uint256 &witnessProgram, CPQPubKey &pubkeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPQPubKey(witnessProgram, pubkeyOut);

        CryptedPQKeyMap::const_iterator mi = mapCryptedPQKeys.find(witnessProgram);
        if (mi != mapCryptedPQKeys.end())
        {
            pubkeyOut = (*mi).second.first;
            return true;
        }
    }
    return false;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted()) {
            return CBasicKeyStore::GetKey(address, keyOut);
        }

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);

        if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
        }

    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            return true;
        }
        // Check for watch-only pubkeys
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
    }
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!mapCryptedKeys.empty() || !mapCryptedPQKeys.empty() || IsCrypted())
            return false;

        // Build every ciphertext before changing keystore mode. Persistence is
        // performed through the virtual AddCrypted* methods below; if any of
        // those writes fails, restore the original plaintext maps so callers
        // can abort their database transaction without leaving a half-crypted
        // in-memory wallet.
        CryptedKeyMap cryptedKeys;
        CryptedPQKeyMap cryptedPQKeys;
        for (const KeyMap::value_type& mKey : mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            std::vector<unsigned char> vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
                return false;
            cryptedKeys[vchPubKey.GetID()] = std::make_pair(vchPubKey, std::move(vchCryptedSecret));
        }

        for (const PQKeyMap::value_type& mKey : mapPQKeys)
        {
            const CPQKey &key = mKey.second;
            CPQPubKey pqPubKey = key.GetPubKey();
            if (!key.IsValid() || !pqPubKey.IsValid() || !key.MatchesPubKey(pqPubKey))
                return false;
            CKeyingMaterial vchSecret(key.GetKeyData().begin(), key.GetKeyData().end());
            std::vector<unsigned char> vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, pqPubKey.GetWitnessProgram(), vchCryptedSecret))
                return false;
            cryptedPQKeys[pqPubKey.GetWitnessProgram()] = std::make_pair(pqPubKey, std::move(vchCryptedSecret));
        }

        KeyMap plaintextKeys;
        PQKeyMap plaintextPQKeys;
        plaintextKeys.swap(mapKeys);
        plaintextPQKeys.swap(mapPQKeys);
        fUseCrypto = true;

        bool success = true;
        try {
            for (const CryptedKeyMap::value_type& entry : cryptedKeys) {
                if (!AddCryptedKey(entry.second.first, entry.second.second)) {
                    success = false;
                    break;
                }
            }
            if (success) {
                for (const CryptedPQKeyMap::value_type& entry : cryptedPQKeys) {
                    if (!AddCryptedPQKey(entry.second.first, entry.second.second)) {
                        success = false;
                        break;
                    }
                }
            }
        } catch (...) {
            success = false;
        }

        if (!success) {
            mapCryptedKeys.clear();
            mapCryptedPQKeys.clear();
            mapKeys.swap(plaintextKeys);
            mapPQKeys.swap(plaintextPQKeys);
            fUseCrypto = false;
            return false;
        }
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedWords(const uint256& hash, const std::vector<unsigned char> &vchCryptedWords)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        nWordHash = hash;
        vchCryptedBip39Words = vchCryptedWords;
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedPassphrase(const std::vector<unsigned char> &vchCryptedPassphrase)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        vchCryptedBip39Passphrase = vchCryptedPassphrase;
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedVchSeed(const std::vector<unsigned char> &vchCryptedVchSeed)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        vchCryptedBip39VchSeed = vchCryptedVchSeed;
    }
    return true;
}

bool CCryptoKeyStore::EncryptBip39(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);

        CKeyingMaterial vchSecretWords(vchWords.begin(), vchWords.end());
        if (!EncryptSecret(vMasterKeyIn, vchSecretWords, nWordHash, vchCryptedBip39Words))
            return false;

        CKeyingMaterial vchSecretVchSeed(g_vchSeed.begin(), g_vchSeed.end());
        if (!EncryptSecret(vMasterKeyIn, vchSecretVchSeed, nWordHash, vchCryptedBip39VchSeed))
            return false;

        CKeyingMaterial vchDecryptedVchSeed;
        if (!DecryptSecret(vMasterKeyIn, vchCryptedBip39VchSeed, nWordHash, vchDecryptedVchSeed)) {
            return false;
        }

        if (!vchPassphrase.empty()) {
            CKeyingMaterial vchSecretPassphrase(vchPassphrase.begin(), vchPassphrase.end());
            if (!EncryptSecret(vMasterKeyIn, vchSecretPassphrase, nWordHash, vchCryptedBip39Passphrase))
                return false;

            CKeyingMaterial vchDecryptedPassphrase;
            if (!DecryptSecret(vMasterKeyIn, vchCryptedBip39Passphrase, nWordHash, vchDecryptedPassphrase)) {
                return false;
            }
        }
    }

    return true;
}

bool CCryptoKeyStore::DecryptBip39(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (vchCryptedBip39Words.size() < AES_BLOCKSIZE ||
            vchCryptedBip39Words.size() % AES_BLOCKSIZE != 0 ||
            vchCryptedBip39VchSeed.size() != BIP39_CRYPTED_SEED_SIZE ||
            (!vchCryptedBip39Passphrase.empty() &&
             (vchCryptedBip39Passphrase.size() < AES_BLOCKSIZE ||
              vchCryptedBip39Passphrase.size() % AES_BLOCKSIZE != 0))) {
            return false;
        }

        CKeyingMaterial vchDecryptedWords;
        if (!DecryptSecret(vMasterKeyIn, vchCryptedBip39Words, nWordHash, vchDecryptedWords)) {
            return false;
        }
        if (Hash(vchDecryptedWords.begin(), vchDecryptedWords.end()) != nWordHash) {
            return false;
        }
        SecureString words;
        words.reserve(vchDecryptedWords.size() > 64 ? vchDecryptedWords.size() : 64);
        words.assign(vchDecryptedWords.begin(), vchDecryptedWords.end());
        if (!CMnemonic::Check(words)) {
            return false;
        }

        CKeyingMaterial vchDecryptedVchSeed;
        if (!DecryptSecret(vMasterKeyIn, vchCryptedBip39VchSeed, nWordHash, vchDecryptedVchSeed) ||
            vchDecryptedVchSeed.size() != BIP39_SEED_SIZE) {
            return false;
        }

        CKeyingMaterial vchDecryptedPassphrase;
        if (!vchCryptedBip39Passphrase.empty()) {
            if (!DecryptSecret(vMasterKeyIn, vchCryptedBip39Passphrase, nWordHash, vchDecryptedPassphrase)) {
                return false;
            }
        }

        SecureString passphrase;
        passphrase.reserve(vchDecryptedPassphrase.size() > 64 ? vchDecryptedPassphrase.size() : 64);
        passphrase.assign(vchDecryptedPassphrase.begin(), vchDecryptedPassphrase.end());
        SecureVector derivedSeed;
        if (!CMnemonic::ToSeed(words, passphrase, derivedSeed) ||
            derivedSeed.size() != BIP39_SEED_SIZE ||
            !TimingResistantEqual(derivedSeed, vchDecryptedVchSeed)) {
            return false;
        }

        vchWords.swap(vchDecryptedWords);
        vchPassphrase.swap(vchDecryptedPassphrase);
        g_vchSeed.swap(vchDecryptedVchSeed);
    }

    return true;
}
