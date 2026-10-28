// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/walletdb.h"

#include "base58.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "fs.h"
#include "hash.h"
#include "protocol.h"
#include "serialize.h"
#include "sync.h"
#include "util.h"
#include "utiltime.h"
#include "wallet/bip39.h"
#include "wallet/wallet.h"

#include <atomic>

#include <boost/thread.hpp>

//
// CWalletDB
//

bool CWalletDB::WriteName(const std::string& strAddress, const std::string& strName)
{
    return WriteIC(std::make_pair(std::string("name"), strAddress), strName);
}

bool CWalletDB::EraseName(const std::string& strAddress)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    return EraseIC(std::make_pair(std::string("name"), strAddress));
}

bool CWalletDB::WritePurpose(const std::string& strAddress, const std::string& strPurpose)
{
    return WriteIC(std::make_pair(std::string("purpose"), strAddress), strPurpose);
}

bool CWalletDB::ErasePurpose(const std::string& strAddress)
{
    return EraseIC(std::make_pair(std::string("purpose"), strAddress));
}

bool CWalletDB::WriteTx(const CWalletTx& wtx)
{
    return WriteIC(std::make_pair(std::string("tx"), wtx.GetHash()), wtx);
}

bool CWalletDB::EraseTx(uint256 hash)
{
    return EraseIC(std::make_pair(std::string("tx"), hash));
}

bool CWalletDB::WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta)
{
    if (!WriteIC(std::make_pair(std::string("keymeta"), vchPubKey), keyMeta, false)) {
        return false;
    }

    // hash pubkey/privkey to accelerate wallet load
    std::vector<unsigned char> vchKey;
    vchKey.reserve(vchPubKey.size() + vchPrivKey.size());
    vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
    vchKey.insert(vchKey.end(), vchPrivKey.begin(), vchPrivKey.end());

    return WriteIC(std::make_pair(std::string("key"), vchPubKey), std::make_pair(vchPrivKey, Hash(vchKey.begin(), vchKey.end())), false);
}

bool CWalletDB::WriteKeyMetadata(const CPubKey& vchPubKey, const CKeyMetadata& keyMeta)
{
    return WriteIC(std::make_pair(std::string("keymeta"), vchPubKey), keyMeta);
}

bool CWalletDB::WriteCryptedKey(const CPubKey& vchPubKey,
                                const std::vector<unsigned char>& vchCryptedSecret,
                                const CKeyMetadata &keyMeta)
{
    const auto cryptedKey = std::make_pair(std::string("ckey"), vchPubKey);
    if (!WriteIC(std::make_pair(std::string("keymeta"), vchPubKey), keyMeta)) {
        return false;
    }

    if (!WriteIC(cryptedKey, vchCryptedSecret, false)) {
        return false;
    }
    if (!EraseIC(std::make_pair(std::string("key"), vchPubKey)) ||
        !EraseIC(std::make_pair(std::string("wkey"), vchPubKey))) {
        // EncryptWallet wraps conversion in a transaction and will abort it.
        // For a standalone encrypted-key write, avoid deliberately retaining
        // a new ciphertext record beside plaintext after an erase error.
        EraseIC(cryptedKey);
        return false;
    }
    return true;
}

bool CWalletDB::WritePQKey(const uint256& witnessProgram, const CPQPubKey& pqPubKey, const CPQKey::KeyData& pqKeyData)
{
    // hash pubkey/keydata to accelerate wallet load
    const uint256 hash = Hash(pqPubKey.begin(), pqPubKey.end(),
                              pqKeyData.begin(), pqKeyData.end());

    return WriteIC(std::make_pair(std::string("pqkey"), witnessProgram),
                   std::make_pair(std::make_pair(pqPubKey, pqKeyData), hash), false);
}

bool CWalletDB::WriteCryptedPQKey(const uint256& witnessProgram, const CPQPubKey& pqPubKey, const std::vector<unsigned char>& vchCryptedSecret)
{
    const auto cryptedKey = std::make_pair(std::string("cpqkey"), witnessProgram);
    if (!WriteIC(cryptedKey, std::make_pair(pqPubKey, vchCryptedSecret), false)) {
        return false;
    }
    if (!EraseIC(std::make_pair(std::string("pqkey"), witnessProgram))) {
        // The wallet-encryption path wraps this operation in a transaction and
        // will abort it. For standalone encrypted-key additions, make a
        // best-effort rollback so a failed erase does not deliberately leave a
        // new mixed plaintext/ciphertext record pair behind.
        EraseIC(cryptedKey);
        return false;
    }
    return true;
}

bool CWalletDB::HasPlaintextKeys(bool& hasPlaintext)
{
    hasPlaintext = false;
    Dbc* pcursor = batch.GetCursor();
    if (!pcursor)
        return false;

    while (true) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        const int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND)
            break;
        if (ret != 0) {
            pcursor->close();
            return false;
        }

        try {
            std::string strType;
            ssKey >> strType;
            if (strType == "key" || strType == "wkey") {
                hasPlaintext = true;
                break;
            }
        } catch (...) {
            pcursor->close();
            return false;
        }
    }

    return pcursor->close() == 0;
}

bool CWalletDB::HasPlaintextPQKeys(bool& hasPlaintext)
{
    hasPlaintext = false;
    Dbc* pcursor = batch.GetCursor();
    if (!pcursor)
        return false;

    while (true) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        const int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND)
            break;
        if (ret != 0) {
            pcursor->close();
            return false;
        }

        try {
            std::string strType;
            ssKey >> strType;
            if (strType == "pqkey") {
                hasPlaintext = true;
                break;
            }
        } catch (...) {
            pcursor->close();
            return false;
        }
    }

    return pcursor->close() == 0;
}

bool CWalletDB::HasPlaintextBip39(bool& hasPlaintext)
{
    hasPlaintext = false;
    Dbc* pcursor = batch.GetCursor();
    if (!pcursor)
        return false;

    while (true) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        const int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND)
            break;
        if (ret != 0) {
            pcursor->close();
            return false;
        }

        try {
            std::string strType;
            ssKey >> strType;
            if (strType == "bip39words" || strType == "bip39passphrase" ||
                strType == "bip39vchseed") {
                hasPlaintext = true;
                break;
            }
        } catch (...) {
            pcursor->close();
            return false;
        }
    }

    return pcursor->close() == 0;
}

bool CWalletDB::WriteEncryptionRewritePending(int previousMinVersion)
{
    return WriteIC(std::string("encryption_rewrite_pending"),
                   std::make_pair(WALLET_ENCRYPTION_REWRITE_MARKER_VERSION,
                                  previousMinVersion));
}

bool CWalletDB::EraseEncryptionRewritePending()
{
    return EraseIC(std::string("encryption_rewrite_pending"));
}

bool CWalletDB::ReadEncryptionRewritePending(bool& pending, int& previousMinVersion)
{
    pending = false;
    previousMinVersion = 0;
    const std::string markerKey = "encryption_rewrite_pending";
    const std::string minVersionKey = "minversion";
    const int markerExistsResult = batch.ExistsStatus(markerKey);
    const int minVersionExistsResult = batch.ExistsStatus(minVersionKey);
    if ((markerExistsResult != 0 && markerExistsResult != DB_NOTFOUND) ||
        (minVersionExistsResult != 0 && minVersionExistsResult != DB_NOTFOUND))
        return false;

    int storedMinVersion = 0;
    const bool hasMinVersion = minVersionExistsResult == 0;
    if (hasMinVersion && !batch.Read(minVersionKey, storedMinVersion))
        return false;

    if (markerExistsResult == DB_NOTFOUND)
        return !hasMinVersion ||
               storedMinVersion != WALLET_ENCRYPTION_REWRITE_MIN_VERSION;

    std::pair<uint32_t, int> marker;
    if (!batch.Read(markerKey, marker) ||
        marker.first != WALLET_ENCRYPTION_REWRITE_MARKER_VERSION ||
        marker.second < FEATURE_WALLETCRYPT || marker.second > CLIENT_VERSION ||
        !hasMinVersion ||
        storedMinVersion != WALLET_ENCRYPTION_REWRITE_MIN_VERSION)
        return false;

    pending = true;
    previousMinVersion = marker.second;
    return true;
}

bool CWalletDB::WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey)
{
    return WriteIC(std::make_pair(std::string("mkey"), nID), kMasterKey, true);
}

bool CWalletDB::WriteCScript(const uint160& hash, const CScript& redeemScript)
{
    return WriteIC(std::make_pair(std::string("cscript"), hash), redeemScript, false);
}

bool CWalletDB::WriteWatchOnly(const CScript &dest, const CKeyMetadata& keyMeta)
{
    if (!WriteIC(std::make_pair(std::string("watchmeta"), dest), keyMeta)) {
        return false;
    }
    return WriteIC(std::make_pair(std::string("watchs"), dest), '1');
}

bool CWalletDB::EraseWatchOnly(const CScript &dest)
{
    if (!EraseIC(std::make_pair(std::string("watchmeta"), dest))) {
        return false;
    }
    return EraseIC(std::make_pair(std::string("watchs"), dest));
}

bool CWalletDB::WriteBestBlock(const CBlockLocator& locator)
{
    WriteIC(std::string("bestblock"), CBlockLocator()); // Write empty block locator so versions that require a merkle branch automatically rescan
    return WriteIC(std::string("bestblock_nomerkle"), locator);
}

bool CWalletDB::ReadBestBlock(CBlockLocator& locator)
{
    if (batch.Read(std::string("bestblock"), locator) && !locator.vHave.empty()) return true;
    return batch.Read(std::string("bestblock_nomerkle"), locator);
}

bool CWalletDB::WriteOrderPosNext(int64_t nOrderPosNext)
{
    return WriteIC(std::string("orderposnext"), nOrderPosNext);
}

bool CWalletDB::ReadPool(int64_t nPool, CKeyPool& keypool)
{
    return batch.Read(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::WritePool(int64_t nPool, const CKeyPool& keypool)
{
    return WriteIC(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::ErasePool(int64_t nPool)
{
    return EraseIC(std::make_pair(std::string("pool"), nPool));
}

bool CWalletDB::WriteMinVersion(int nVersion)
{
    return WriteIC(std::string("minversion"), nVersion);
}

bool CWalletDB::ReadAccount(const std::string& strAccount, CAccount& account)
{
    account.SetNull();
    return batch.Read(std::make_pair(std::string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccount(const std::string& strAccount, const CAccount& account)
{
    return WriteIC(std::make_pair(std::string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry)
{
    return WriteIC(std::make_pair(std::string("acentry"), std::make_pair(acentry.strAccount, nAccEntryNum)), acentry);
}

CAmount CWalletDB::GetAccountCreditDebit(const std::string& strAccount)
{
    std::list<CAccountingEntry> entries;
    ListAccountCreditDebit(strAccount, entries);

    CAmount nCreditDebit = 0;
    for (const CAccountingEntry& entry : entries)
        nCreditDebit += entry.nCreditDebit;

    return nCreditDebit;
}

void CWalletDB::ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& entries)
{
    bool fAllAccounts = (strAccount == "*");

    Dbc* pcursor = batch.GetCursor();
    if (!pcursor)
        throw std::runtime_error(std::string(__func__) + ": cannot create DB cursor");
    bool setRange = true;
    while (true)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (setRange)
            ssKey << std::make_pair(std::string("acentry"), std::make_pair((fAllAccounts ? std::string("") : strAccount), uint64_t(0)));
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue, setRange);
        setRange = false;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
        {
            pcursor->close();
            throw std::runtime_error(std::string(__func__) + ": error scanning DB");
        }

        // Unserialize
        std::string strType;
        ssKey >> strType;
        if (strType != "acentry")
            break;
        CAccountingEntry acentry;
        ssKey >> acentry.strAccount;
        if (!fAllAccounts && acentry.strAccount != strAccount)
            break;

        ssValue >> acentry;
        ssKey >> acentry.nEntryNo;
        entries.push_back(acentry);
    }

    pcursor->close();
}

class CWalletScanState {
public:
    unsigned int nKeys;
    unsigned int nCKeys;
    unsigned int nWatchKeys;
    unsigned int nKeyMeta;
    bool fIsEncrypted;
    bool fHasPlaintextKeys;
    bool fHasPlaintextPQKeys;
    bool fHasCryptedPQKeys;
    bool fHasPlaintextBip39Words;
    bool fHasPlaintextBip39Passphrase;
    bool fHasPlaintextBip39Seed;
    bool fHasCryptedBip39Words;
    bool fHasCryptedBip39Passphrase;
    bool fHasCryptedBip39Seed;
    uint256 plaintextBip39WordHash;
    SecureVector plaintextBip39Words;
    SecureVector plaintextBip39Passphrase;
    SecureVector plaintextBip39Seed;
    bool fHasHDChain;
    bool fHasPQHDChain;
    bool fEncryptionRewritePending;
    int nEncryptionRewritePreviousMinVersion;
    bool fAnyUnordered;
    int nFileVersion;
    std::vector<uint256> vWalletUpgrade;

    CWalletScanState() {
        nKeys = nCKeys = nWatchKeys = nKeyMeta = 0;
        fIsEncrypted = false;
        fHasPlaintextKeys = false;
        fHasPlaintextPQKeys = false;
        fHasCryptedPQKeys = false;
        fHasPlaintextBip39Words = false;
        fHasPlaintextBip39Passphrase = false;
        fHasPlaintextBip39Seed = false;
        fHasCryptedBip39Words = false;
        fHasCryptedBip39Passphrase = false;
        fHasCryptedBip39Seed = false;
        fHasHDChain = false;
        fHasPQHDChain = false;
        fEncryptionRewritePending = false;
        nEncryptionRewritePreviousMinVersion = 0;
        fAnyUnordered = false;
        nFileVersion = 0;
    }
};

bool ReadKeyValue(CWallet* pwallet, CDataStream& ssKey, CDataStream& ssValue,
             CWalletScanState &wss, std::string& strType, std::string& strErr)
{
    try {
        // Unserialize
        // Taking advantage of the fact that pair serialization
        // is just the two items serialized one after the other
        ssKey >> strType;
        if (strType == "name")
        {
            std::string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].name;
        }
        else if (strType == "purpose")
        {
            std::string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].purpose;
        }
        else if (strType == "tx")
        {
            uint256 hash;
            ssKey >> hash;
            CWalletTx wtx;
            ssValue >> wtx;
            CValidationState state;
            if (!(CheckTransaction(wtx, state) && (wtx.GetHash() == hash) && state.IsValid())) {
                // If a client has a wallet.dat that contains asset transactions, but we are syncing the chain.
                // we want to make sure that we don't fail to load this wallet transaction just because it is an asset transaction
                // before asset are active
                if (state.GetRejectReason() != "bad-txns-is-asset-and-asset-not-active" && state.GetRejectReason() != "bad-txns-transfer-asset-bad-deserialize") {
                    strErr = state.GetRejectReason();
                    return false;
                }
            }

            // Undo serialize changes in 31600
            if (31404 <= wtx.fTimeReceivedIsTxTime && wtx.fTimeReceivedIsTxTime <= 31703)
            {
                if (!ssValue.empty())
                {
                    char fTmp;
                    char fUnused;
                    ssValue >> fTmp >> fUnused >> wtx.strFromAccount;
                    strErr = strprintf("LoadWallet() upgrading tx ver=%d %d '%s' %s",
                                       wtx.fTimeReceivedIsTxTime, fTmp, wtx.strFromAccount, hash.ToString());
                    wtx.fTimeReceivedIsTxTime = fTmp;
                }
                else
                {
                    strErr = strprintf("LoadWallet() repairing tx ver=%d %s", wtx.fTimeReceivedIsTxTime, hash.ToString());
                    wtx.fTimeReceivedIsTxTime = 0;
                }
                wss.vWalletUpgrade.push_back(hash);
            }

            if (wtx.nOrderPos == -1)
                wss.fAnyUnordered = true;

            pwallet->LoadToWallet(wtx);
        }
        else if (strType == "acentry")
        {
            std::string strAccount;
            ssKey >> strAccount;
            uint64_t nNumber;
            ssKey >> nNumber;
            if (nNumber > pwallet->nAccountingEntryNumber) {
                pwallet->nAccountingEntryNumber = nNumber;
            }

            if (!wss.fAnyUnordered)
            {
                CAccountingEntry acentry;
                ssValue >> acentry;
                if (acentry.nOrderPos == -1)
                    wss.fAnyUnordered = true;
            }
        }
        else if (strType == "watchs")
        {
            wss.nWatchKeys++;
            CScript script;
            ssKey >> script;
            char fYes;
            ssValue >> fYes;
            if (fYes == '1')
                pwallet->LoadWatchOnly(script);
        }
        else if (strType == "key" || strType == "wkey")
        {
            wss.fHasPlaintextKeys = true;
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            if (!vchPubKey.IsValid())
            {
                strErr = "Error reading wallet database: CPubKey corrupt";
                return false;
            }
            CKey key;
            CPrivKey pkey;
            uint256 hash;

            if (strType == "key")
            {
                wss.nKeys++;
                ssValue >> pkey;
            } else {
                CWalletKey wkey;
                ssValue >> wkey;
                pkey = wkey.vchPrivKey;
            }

            // Old wallets store keys as "key" [pubkey] => [privkey]
            // ... which was slow for wallets with lots of keys, because the public key is re-derived from the private key
            // using EC operations as a checksum.
            // Newer wallets store keys as "key"[pubkey] => [privkey][hash(pubkey,privkey)], which is much faster while
            // remaining backwards-compatible.
            try
            {
                ssValue >> hash;
            }
            catch (...) {}

            bool fSkipCheck = false;

            if (!hash.IsNull())
            {
                // hash pubkey/privkey to accelerate wallet load
                std::vector<unsigned char> vchKey;
                vchKey.reserve(vchPubKey.size() + pkey.size());
                vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
                vchKey.insert(vchKey.end(), pkey.begin(), pkey.end());

                if (Hash(vchKey.begin(), vchKey.end()) != hash)
                {
                    strErr = "Error reading wallet database: CPubKey/CPrivKey corrupt";
                    return false;
                }

                fSkipCheck = true;
            }

            if (!key.Load(pkey, vchPubKey, fSkipCheck))
            {
                strErr = "Error reading wallet database: CPrivKey corrupt";
                return false;
            }
            if (!pwallet->LoadKey(key, vchPubKey))
            {
                strErr = "Error reading wallet database: LoadKey failed";
                return false;
            }
        }
        else if (strType == "mkey")
        {
            unsigned int nID;
            ssKey >> nID;
            CMasterKey kMasterKey;
            ssValue >> kMasterKey;
            if(pwallet->mapMasterKeys.count(nID) != 0)
            {
                strErr = strprintf("Error reading wallet database: duplicate CMasterKey id %u", nID);
                return false;
            }
            pwallet->mapMasterKeys[nID] = kMasterKey;
            if (pwallet->nMasterKeyMaxID < nID)
                pwallet->nMasterKeyMaxID = nID;
        }
        else if (strType == "ckey")
        {
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            if (!vchPubKey.IsValid())
            {
                strErr = "Error reading wallet database: CPubKey corrupt";
                return false;
            }
            std::vector<unsigned char> vchPrivKey;
            ssValue >> vchPrivKey;
            wss.nCKeys++;

            if (!pwallet->LoadCryptedKey(vchPubKey, vchPrivKey))
            {
                strErr = "Error reading wallet database: LoadCryptedKey failed";
                return false;
            }
            wss.fIsEncrypted = true;
        }
        else if (strType == "pqkey")
        {
            wss.fHasPlaintextPQKeys = true;
            uint256 witnessProgram;
            ssKey >> witnessProgram;

            CPQPubKey pqPubKey;
            uint256 hash;
            ssValue >> pqPubKey;

            // The generic vector deserializer resizes before callers can
            // validate the length. Check the fixed ML-DSA-44 secret-key size
            // before allocating from the bounded locked-memory pool.
            const uint64_t pqKeySize = ReadCompactSize(ssValue);
            if (pqKeySize != mldsa::SECRETKEY_BYTES)
            {
                strErr = "Error reading wallet database: CPQKey size corrupt";
                return false;
            }
            CPQKey::KeyData pqKeyData(pqKeySize);
            ssValue.read(reinterpret_cast<char*>(pqKeyData.data()), pqKeyData.size());
            ssValue >> hash;

            if (!pqPubKey.IsValid())
            {
                strErr = "Error reading wallet database: CPQPubKey corrupt";
                return false;
            }

            // verify hash
            if (Hash(pqPubKey.begin(), pqPubKey.end(),
                     pqKeyData.begin(), pqKeyData.end()) != hash)
            {
                strErr = "Error reading wallet database: CPQPubKey/CPQKey corrupt";
                return false;
            }

            CPQKey pqKey;
            if (!pqKey.SetKeyData(pqKeyData))
            {
                strErr = "Error reading wallet database: CPQKey SetKeyData failed";
                return false;
            }
            if (!pwallet->LoadPQKey(pqKey, pqPubKey))
            {
                strErr = "Error reading wallet database: LoadPQKey failed";
                return false;
            }
            wss.nKeys++;
        }
        else if (strType == "cpqkey")
        {
            wss.fHasCryptedPQKeys = true;
            uint256 witnessProgram;
            ssKey >> witnessProgram;

            CPQPubKey pqPubKey;
            std::vector<unsigned char> vchCryptedSecret;
            ssValue >> pqPubKey;
            ssValue >> vchCryptedSecret;

            if (!pqPubKey.IsValid())
            {
                strErr = "Error reading wallet database: CPQPubKey corrupt";
                return false;
            }
            if (!pwallet->LoadCryptedPQKey(pqPubKey, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedPQKey failed";
                return false;
            }
            wss.nCKeys++;
            wss.fIsEncrypted = true;
        }
        else if (strType == "keymeta" || strType == "watchmeta")
        {
            CTxDestination keyID;
            if (strType == "keymeta")
            {
              CPubKey vchPubKey;
              ssKey >> vchPubKey;
              keyID = vchPubKey.GetID();
            }
            else if (strType == "watchmeta")
            {
              CScript script;
              ssKey >> script;
              keyID = CScriptID(script);
            }

            CKeyMetadata keyMeta;
            ssValue >> keyMeta;
            wss.nKeyMeta++;

            pwallet->LoadKeyMetadata(keyID, keyMeta);
        }
        else if (strType == "defaultkey")
        {
            // We don't want or need the default key, but if there is one set,
            // we want to make sure that it is valid so that we can detect corruption
            CPubKey vchPubKey;
            ssValue >> vchPubKey;
            if (!vchPubKey.IsValid()) {
                strErr = "Error reading wallet database: Default Key corrupt";
                return false;
            }
        }
        else if (strType == "pool")
        {
            int64_t nIndex;
            ssKey >> nIndex;
            CKeyPool keypool;
            ssValue >> keypool;

            pwallet->LoadKeyPool(nIndex, keypool);
        }
        else if (strType == "version")
        {
            ssValue >> wss.nFileVersion;
            if (wss.nFileVersion == 10300)
                wss.nFileVersion = 300;
        }
        else if (strType == "cscript")
        {
            uint160 hash;
            ssKey >> hash;
            CScript script;
            ssValue >> script;
            if (!pwallet->LoadCScript(script))
            {
                strErr = "Error reading wallet database: LoadCScript failed";
                return false;
            }
        }
        else if (strType == "orderposnext")
        {
            ssValue >> pwallet->nOrderPosNext;
        }
        else if (strType == "destdata")
        {
            std::string strAddress, strKey, strValue;
            ssKey >> strAddress;
            ssKey >> strKey;
            ssValue >> strValue;
            if (!pwallet->LoadDestData(DecodeDestination(strAddress), strKey, strValue))
            {
                strErr = "Error reading wallet database: LoadDestData failed";
                return false;
            }
        }
        else if (strType == "hdchain")
        {
            CHDChain chain(pwallet);
            ssValue >> chain;
            if (!pwallet->SetHDChain(chain, true))
            {
                strErr = "Error reading wallet database: SetHDChain failed";
                return false;
            }
            wss.fHasHDChain = true;
        }
        else if (strType == "pqhdchain")
        {
            CPQHDChain chain;
            ssValue >> chain;
            if (!ssKey.empty() || !ssValue.empty() || !chain.IsInitialized())
            {
                strErr = "Error reading wallet database: PQ HD chain corrupt";
                return false;
            }
            if (!pwallet->LoadPQHDChain(chain))
            {
                strErr = "Error reading wallet database: LoadPQHDChain failed";
                return false;
            }
            wss.fHasPQHDChain = true;
        }
        else if (strType == "cbip39words")
        {
            wss.fHasCryptedBip39Words = true;
            std::pair<uint256,std::vector<unsigned char> > valuePair;
            ssValue >> valuePair;
            if (!ssValue.empty())
            {
                strErr = "Error reading wallet database: encrypted BIP39 words trailing data";
                return false;
            }

            if (!pwallet->LoadCryptedWords(valuePair.first, valuePair.second))
            {
                strErr = "Error reading wallet database: LoadCryptedWords failed";
                return false;
            }
        }
        else if (strType == "cbip39passphrase")
        {
            wss.fHasCryptedBip39Passphrase = true;
            std::vector<unsigned char> vchPassphrase;
            ssValue >> vchPassphrase;
            if (!ssValue.empty())
            {
                strErr = "Error reading wallet database: encrypted BIP39 passphrase trailing data";
                return false;
            }

            if (!pwallet->LoadCryptedPassphrase(vchPassphrase))
            {
                strErr = "Error reading wallet database: LoadCryptedPassphrase failed";
                return false;
            }
        }
        else if (strType == "cbip39vchseed")
        {
            wss.fHasCryptedBip39Seed = true;
            const uint64_t seedSize = ReadCompactSize(ssValue);
            if (seedSize != BIP39_CRYPTED_SEED_SIZE)
            {
                strErr = "Error reading wallet database: encrypted BIP39 seed size corrupt";
                return false;
            }
            if (ssValue.size() != seedSize)
            {
                strErr = "Error reading wallet database: encrypted BIP39 seed trailing data";
                return false;
            }
            std::vector<unsigned char> vchSeed(seedSize);
            ssValue.read(reinterpret_cast<char*>(vchSeed.data()), vchSeed.size());

            if (!pwallet->LoadCryptedVchSeed(vchSeed))
            {
                strErr = "Error reading wallet database: LoadCryptedVchSeed failed";
                return false;
            }
        }
        else if (strType == "bip39words")
        {
            wss.fHasPlaintextBip39Words = true;
            std::pair<uint256, SecureVector> valuePair;
            ssValue >> valuePair;
            if (!ssValue.empty() || valuePair.second.size() > 4096)
            {
                strErr = "Error reading wallet database: BIP39 words corrupt";
                return false;
            }
            wss.plaintextBip39WordHash = valuePair.first;
            wss.plaintextBip39Words.swap(valuePair.second);
        }
        else if (strType == "bip39passphrase")
        {
            wss.fHasPlaintextBip39Passphrase = true;
            SecureVector vchPassphrase;
            ssValue >> vchPassphrase;
            if (!ssValue.empty())
            {
                strErr = "Error reading wallet database: BIP39 passphrase trailing data";
                return false;
            }
            wss.plaintextBip39Passphrase.swap(vchPassphrase);
        }
        else if (strType == "bip39vchseed")
        {
            wss.fHasPlaintextBip39Seed = true;
            const uint64_t seedSize = ReadCompactSize(ssValue);
            if (seedSize != BIP39_SEED_SIZE)
            {
                strErr = "Error reading wallet database: BIP39 seed size corrupt";
                return false;
            }
            if (ssValue.size() != seedSize)
            {
                strErr = "Error reading wallet database: BIP39 seed trailing data";
                return false;
            }
            SecureVector vchSeed(seedSize);
            ssValue.read(reinterpret_cast<char*>(vchSeed.data()), vchSeed.size());
            wss.plaintextBip39Seed.swap(vchSeed);
        }
        else if (strType == "encryption_rewrite_pending")
        {
            std::pair<uint32_t, int> marker;
            if (!ssKey.empty())
            {
                strErr = "Error reading wallet database: encryption rewrite marker key corrupt";
                return false;
            }
            ssValue >> marker;
            if (marker.first != WALLET_ENCRYPTION_REWRITE_MARKER_VERSION ||
                marker.second < FEATURE_WALLETCRYPT ||
                marker.second > CLIENT_VERSION || !ssValue.empty())
            {
                strErr = "Error reading wallet database: encryption rewrite marker corrupt";
                return false;
            }
            wss.fEncryptionRewritePending = true;
            wss.nEncryptionRewritePreviousMinVersion = marker.second;
        }
    } catch (...)
    {
        return false;
    }
    return true;
}

bool CWalletDB::IsKeyType(const std::string& strType)
{
    return (strType== "key" || strType == "wkey" ||
            strType == "mkey" || strType == "ckey" ||
            strType == "pqkey" || strType == "cpqkey" ||
            strType == "hdchain" || strType == "pqhdchain" ||
            strType == "bip39words" || strType == "bip39passphrase" ||
            strType == "bip39vchseed" || strType == "cbip39words" ||
            strType == "cbip39passphrase" || strType == "cbip39vchseed" ||
            strType == "encryption_rewrite_pending");
}

DBErrors CWalletDB::LoadWallet(CWallet* pwallet)
{
    CWalletScanState wss;
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;
    int nMinVersion = 0;
    bool hasMinVersion = false;
    bool rewritePending = false;
    int rewritePreviousMinVersion = 0;

    LOCK(pwallet->cs_wallet);
    try {
        hasMinVersion = batch.Read((std::string)"minversion", nMinVersion);
        if (hasMinVersion && nMinVersion == WALLET_ENCRYPTION_REWRITE_MIN_VERSION) {
            if (!ReadEncryptionRewritePending(
                    rewritePending, rewritePreviousMinVersion) || !rewritePending)
                return DB_CORRUPT;
            pwallet->LoadMinVersion(rewritePreviousMinVersion);
        } else if (hasMinVersion) {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
            pwallet->LoadMinVersion(nMinVersion);
        }

        // Get cursor
        Dbc* pcursor = batch.GetCursor();
        if (!pcursor)
        {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true)
        {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
            {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            // Try to be tolerant of single corrupt records:
            std::string strType, strErr;
            if (!ReadKeyValue(pwallet, ssKey, ssValue, wss, strType, strErr))
            {
                // losing keys is considered a catastrophic error, anything else
                // we assume the user can live with:
                if (IsKeyType(strType) || strType == "defaultkey")
                    result = DB_CORRUPT;
                else
                {
                    LogPrintf("DB failed to Read Key Value. Type: %s, Error: %s\n", strType, strErr);
                    // Leave other errors alone, if we try to fix them we might make things worse.
                    fNoncriticalErrors = true; // ... but do warn the user there is something wrong.
                    if (strType == "tx") {
                        // Rescan if there is a bad transaction record:
                        gArgs.SoftSetBoolArg("-rescan", true);
                    }
                }
            }
            if (!strErr.empty())
                LogPrintf("%s\n", strErr);
        }
        pcursor->close();
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (...) {
        result = DB_CORRUPT;
    }

    const bool hasPlaintextBip39 = wss.fHasPlaintextBip39Words ||
        wss.fHasPlaintextBip39Passphrase || wss.fHasPlaintextBip39Seed;
    const bool hasCryptedBip39 = wss.fHasCryptedBip39Words ||
        wss.fHasCryptedBip39Passphrase || wss.fHasCryptedBip39Seed;
    const bool hasEncryptionEvidence = wss.fHasCryptedPQKeys || wss.fIsEncrypted ||
        hasCryptedBip39 || !pwallet->mapMasterKeys.empty();

    if (wss.fHasPQHDChain &&
        (!wss.fHasHDChain || !pwallet->IsHDEnabled())) {
        LogPrintf("Error reading wallet database: PQ HD chain has no wallet HD chain\n");
        result = DB_CORRUPT;
    }

    if (wss.fEncryptionRewritePending) {
        pwallet->SetEncryptionRewritePending(true);
        if (!rewritePending || !hasMinVersion ||
            nMinVersion != WALLET_ENCRYPTION_REWRITE_MIN_VERSION ||
            wss.nEncryptionRewritePreviousMinVersion != rewritePreviousMinVersion ||
            !hasEncryptionEvidence || pwallet->mapMasterKeys.empty()) {
            LogPrintf("Error reading wallet database: encryption rewrite state is inconsistent\n");
            result = DB_CORRUPT;
        }
    } else if (rewritePending || nMinVersion == WALLET_ENCRYPTION_REWRITE_MIN_VERSION) {
        LogPrintf("Error reading wallet database: encryption rewrite marker is missing\n");
        result = DB_CORRUPT;
    }

    if ((wss.fHasPlaintextKeys || wss.fHasPlaintextPQKeys || hasPlaintextBip39) &&
        hasEncryptionEvidence) {
        LogPrintf("Error reading wallet database: encrypted wallet contains plaintext private keys\n");
        result = DB_CORRUPT;
    }

    if (pwallet->IsBip44Enabled()) {
        const bool completePlaintextBip39 =
            wss.fHasPlaintextBip39Words && wss.fHasPlaintextBip39Seed;
        const bool completeCryptedBip39 =
            wss.fHasCryptedBip39Words && wss.fHasCryptedBip39Seed;
        if (hasPlaintextBip39 == hasCryptedBip39 ||
            (hasPlaintextBip39 && !completePlaintextBip39) ||
            (hasCryptedBip39 && !completeCryptedBip39) ||
            (hasCryptedBip39 && pwallet->mapMasterKeys.empty())) {
            LogPrintf("Error reading wallet database: incomplete or mixed BIP39 key material\n");
            result = DB_CORRUPT;
        }
    } else if (hasPlaintextBip39 || hasCryptedBip39) {
        LogPrintf("Error reading wallet database: BIP39 key material has no BIP44 chain\n");
        result = DB_CORRUPT;
    }

    if (result == DB_LOAD_OK && pwallet->IsBip44Enabled() && hasPlaintextBip39) {
        SecureString words;
        words.reserve(wss.plaintextBip39Words.size() > 64 ?
                      wss.plaintextBip39Words.size() : 64);
        words.assign(wss.plaintextBip39Words.begin(),
                     wss.plaintextBip39Words.end());
        SecureString passphrase;
        passphrase.reserve(wss.plaintextBip39Passphrase.size() > 64 ?
                           wss.plaintextBip39Passphrase.size() : 64);
        passphrase.assign(wss.plaintextBip39Passphrase.begin(),
                          wss.plaintextBip39Passphrase.end());
        SecureVector derivedSeed;
        const bool validLineage =
            Hash(wss.plaintextBip39Words.begin(), wss.plaintextBip39Words.end()) ==
                wss.plaintextBip39WordHash &&
            CMnemonic::Check(words) &&
            CMnemonic::ToSeed(words, passphrase, derivedSeed) &&
            derivedSeed.size() == BIP39_SEED_SIZE &&
            TimingResistantEqual(derivedSeed, wss.plaintextBip39Seed);
        ClearSecureString(words);
        ClearSecureString(passphrase);
        if (!validLineage) {
            LogPrintf("Error reading wallet database: BIP39 plaintext lineage corrupt\n");
            result = DB_CORRUPT;
        } else if (!pwallet->LoadWords(wss.plaintextBip39WordHash,
                                      std::move(wss.plaintextBip39Words)) ||
                   !pwallet->LoadPassphrase(
                       std::move(wss.plaintextBip39Passphrase)) ||
                   !pwallet->LoadVchSeed(std::move(wss.plaintextBip39Seed))) {
            LogPrintf("Error reading wallet database: BIP39 plaintext install failed\n");
            result = DB_CORRUPT;
        }
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    // Any wallet corruption at all: skip any rewriting or
    // upgrading, we don't want to make it worse.
    if (result != DB_LOAD_OK) {
        if (wss.fEncryptionRewritePending && result == DB_NONCRITICAL_ERROR)
            return DB_NEED_REWRITE_ENCRYPTION_NONCRITICAL;
        return result;
    }

    LogPrintf("nFileVersion = %d\n", wss.nFileVersion);

    LogPrintf("Keys: %u plaintext, %u encrypted, %u w/ metadata, %u total\n",
           wss.nKeys, wss.nCKeys, wss.nKeyMeta, wss.nKeys + wss.nCKeys);

    // nTimeFirstKey is only reliable if all keys have metadata
    if ((wss.nKeys + wss.nCKeys + wss.nWatchKeys) != wss.nKeyMeta)
        pwallet->UpdateTimeFirstKey(1);

    for (uint256 hash : wss.vWalletUpgrade)
        WriteTx(pwallet->mapWallet[hash]);

    // Rewrite encrypted wallets of versions 0.4.0 and 0.5.0rc:
    if (!wss.fEncryptionRewritePending && wss.fIsEncrypted &&
        (wss.nFileVersion == 40000 || wss.nFileVersion == 50000))
        return DB_NEED_REWRITE;

    if (wss.nFileVersion < CLIENT_VERSION) // Update
        WriteVersion(CLIENT_VERSION);

    if (wss.fAnyUnordered)
        result = pwallet->ReorderTransactions();

    pwallet->laccentries.clear();
    ListAccountCreditDebit("*", pwallet->laccentries);
    for (CAccountingEntry& entry : pwallet->laccentries) {
        pwallet->wtxOrdered.insert(make_pair(entry.nOrderPos, CWallet::TxPair(nullptr, &entry)));
    }

    if (wss.fEncryptionRewritePending)
        return DB_NEED_REWRITE_ENCRYPTION;

    return result;
}

DBErrors CWalletDB::FindWalletTx(std::vector<uint256>& vTxHash, std::vector<CWalletTx>& vWtx)
{
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;

    try {
        int nMinVersion = 0;
        if (batch.Read((std::string)"minversion", nMinVersion))
        {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
        }

        // Get cursor
        Dbc* pcursor = batch.GetCursor();
        if (!pcursor)
        {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true)
        {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = batch.ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
            {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            std::string strType;
            ssKey >> strType;
            if (strType == "tx") {
                uint256 hash;
                ssKey >> hash;

                CWalletTx wtx;
                ssValue >> wtx;

                vTxHash.push_back(hash);
                vWtx.push_back(wtx);
            }
        }
        pcursor->close();
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (...) {
        result = DB_CORRUPT;
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    return result;
}

DBErrors CWalletDB::ZapSelectTx(std::vector<uint256>& vTxHashIn, std::vector<uint256>& vTxHashOut)
{
    // build list of wallet TXs and hashes
    std::vector<uint256> vTxHash;
    std::vector<CWalletTx> vWtx;
    DBErrors err = FindWalletTx(vTxHash, vWtx);
    if (err != DB_LOAD_OK) {
        return err;
    }

    std::sort(vTxHash.begin(), vTxHash.end());
    std::sort(vTxHashIn.begin(), vTxHashIn.end());

    // erase each matching wallet TX
    bool delerror = false;
    std::vector<uint256>::iterator it = vTxHashIn.begin();
    for (uint256 hash : vTxHash) {
        while (it < vTxHashIn.end() && (*it) < hash) {
            it++;
        }
        if (it == vTxHashIn.end()) {
            break;
        }
        else if ((*it) == hash) {
            if(!EraseTx(hash)) {
                LogPrint(BCLog::DB, "Transaction was found for deletion but returned database error: %s\n", hash.GetHex());
                delerror = true;
            }
            vTxHashOut.push_back(hash);
        }
    }

    if (delerror) {
        return DB_CORRUPT;
    }
    return DB_LOAD_OK;
}

DBErrors CWalletDB::ZapWalletTx(std::vector<CWalletTx>& vWtx)
{
    // build list of wallet TXs
    std::vector<uint256> vTxHash;
    DBErrors err = FindWalletTx(vTxHash, vWtx);
    if (err != DB_LOAD_OK)
        return err;

    // erase each wallet TX
    for (uint256& hash : vTxHash) {
        if (!EraseTx(hash))
            return DB_CORRUPT;
    }

    return DB_LOAD_OK;
}

void MaybeCompactWalletDB()
{
    static std::atomic<bool> fOneThread(false);
    if (fOneThread.exchange(true)) {
        return;
    }
    if (!gArgs.GetBoolArg("-flushwallet", DEFAULT_FLUSHWALLET)) {
        return;
    }

    for (CWalletRef pwallet : vpwallets) {
        CWalletDBWrapper& dbh = pwallet->GetDBHandle();

        unsigned int nUpdateCounter = dbh.nUpdateCounter;

        if (dbh.nLastSeen != nUpdateCounter) {
            dbh.nLastSeen = nUpdateCounter;
            dbh.nLastWalletUpdate = GetTime();
        }

        if (dbh.nLastFlushed != nUpdateCounter && GetTime() - dbh.nLastWalletUpdate >= 2) {
            if (CDB::PeriodicFlush(dbh)) {
                dbh.nLastFlushed = nUpdateCounter;
            }
        }
    }

    fOneThread = false;
}

//
// Try to (very carefully!) recover wallet file if there is a problem.
//
bool CWalletDB::Recover(const std::string& filename, void *callbackDataIn, bool (*recoverKVcallback)(void* callbackData, CDataStream ssKey, CDataStream ssValue), std::string& out_backup_filename)
{
    return CDB::Recover(filename, callbackDataIn, recoverKVcallback, out_backup_filename);
}

bool CWalletDB::Recover(const std::string& filename, std::string& out_backup_filename)
{
    // recover without a key filter callback
    // results in recovering all record types
    return CWalletDB::Recover(filename, nullptr, nullptr, out_backup_filename);
}

bool CWalletDB::RecoverKeysOnlyFilter(void *callbackData, CDataStream ssKey, CDataStream ssValue)
{
    CWallet *dummyWallet = reinterpret_cast<CWallet*>(callbackData);
    if (!dummyWallet)
        return false;

    // Classify from a copy before parsing the value. Recovery must not spend
    // resources on, or mutate the dummy wallet for, records it will discard.
    std::string strType;
    try {
        CDataStream ssType(ssKey);
        ssType >> strType;
    } catch (...) {
        return false;
    }
    // Key-only recovery creates a compact replacement database. Drop both the
    // pending marker and its minversion fence instead of copying only one half
    // of the recovery protocol.
    if (strType == "encryption_rewrite_pending")
        return false;
    if (strType == "destdata") {
        // Keep only the bounded, canonical PQ asset key association. Generic
        // address-book metadata does not belong in key-only recovery.
        if (ssKey.size() > 256 || ssValue.size() > 256)
            return false;
        try {
            std::string parsedType, address, key, descriptor;
            ssKey >> parsedType >> address >> key;
            ssValue >> descriptor;
            if (parsedType != "destdata" ||
                (key != "pqasset:destination:v1" &&
                 key != "pqasset:destination:v2") ||
                !ssKey.empty() || !ssValue.empty())
                return false;
            CKeyID classicalKey;
            uint256 pqProgram;
            return DecodePQAssetDestination(descriptor, classicalKey, pqProgram) &&
                address == EncodeDestination(classicalKey) &&
                descriptor == EncodePQAssetDestination(classicalKey, pqProgram) &&
                (key == "pqasset:destination:v1" ||
                 classicalKey == CKeyID(Hash160(pqProgram.begin(), pqProgram.end())));
        } catch (...) {
            return false;
        }
    }
    if (!IsKeyType(strType))
        return false;

    CWalletScanState dummyWss;
    std::string parsedType, strErr;
    bool fReadOK;
    {
        // Required in LoadKeyMetadata():
        LOCK(dummyWallet->cs_wallet);
        fReadOK = ReadKeyValue(dummyWallet, ssKey, ssValue,
                               dummyWss, parsedType, strErr);
    }
    if (!fReadOK || parsedType != strType)
    {
        LogPrintf("WARNING: CWalletDB::Recover skipping %s: %s\n", strType, strErr);
        return false;
    }

    return true;
}

bool CWalletDB::VerifyEnvironment(const std::string& walletFile, const fs::path& dataDir, std::string& errorStr)
{
    return CDB::VerifyEnvironment(walletFile, dataDir, errorStr);
}

bool CWalletDB::VerifyDatabaseFile(const std::string& walletFile, const fs::path& dataDir, std::string& warningStr, std::string& errorStr)
{
    return CDB::VerifyDatabaseFile(walletFile, dataDir, warningStr, errorStr, CWalletDB::Recover);
}

bool CWalletDB::WriteDestData(const std::string &address, const std::string &key, const std::string &value)
{
    return WriteIC(std::make_pair(std::string("destdata"), std::make_pair(address, key)), value);
}

bool CWalletDB::WriteBip39Words(const uint256& hash, const std::vector<unsigned char>& vchWords, bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39words");
    return WriteIC(key, std::make_pair(hash,vchWords), true);
}

bool CWalletDB::WriteBip39Words(const uint256& hash, const SecureVector& vchWords, bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39words");
    return WriteIC(key, std::make_pair(hash, vchWords), true);
}

bool CWalletDB::WriteBip39Passphrase(const std::vector<unsigned char>& vchPassphrase,  bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39passphrase");
    return WriteIC(key, vchPassphrase, true);
}

bool CWalletDB::WriteBip39Passphrase(const SecureVector& vchPassphrase, bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39passphrase");
    return WriteIC(key, vchPassphrase, true);
}

bool CWalletDB::WriteBip39VchSeed(const std::vector<unsigned char>& vchSeed,  bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39vchseed");
    return WriteIC(key, vchSeed, true);
}

bool CWalletDB::WriteBip39VchSeed(const SecureVector& vchSeed, bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39vchseed");
    return WriteIC(key, vchSeed, true);
}

bool CWalletDB::ReadBip39Words(uint256& hash, std::vector<unsigned char>& vchWords,  bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39words");
    std::pair<uint256, std::vector<unsigned char>> valuePair;
    bool ret = batch.Read(key, valuePair);
    if (ret) {
        hash = valuePair.first;
        vchWords = valuePair.second;
    }
    return ret;
}

bool CWalletDB::ReadBip39Passphrase(std::vector<unsigned char>& vchPassphrase,  bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39passphrase");
    return batch.Read(key, vchPassphrase);
}

bool CWalletDB::ReadBip39VchSeed(std::vector<unsigned char>& vchSeed,  bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39vchseed");
    return batch.Read(key, vchSeed);
}

bool CWalletDB::EraseBip39Words(bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39words");
    return EraseIC(key);
}

bool CWalletDB::EraseBip39Passphrase(bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39passphrase");
    return EraseIC(key);
}

bool CWalletDB::EraseBip39VchSeed(bool fEncrypted)
{
    std::string key = fEncrypted ? "c" : "";
    key.append("bip39vchseed");
    return EraseIC(key);
}

bool CWalletDB::EraseDestData(const std::string &address, const std::string &key)
{
    return EraseIC(std::make_pair(std::string("destdata"), std::make_pair(address, key)));
}

bool CWalletDB::WriteHDChain(const CHDChain& chain)
{
    return WriteIC(std::string("hdchain"), chain);
}

bool CWalletDB::WritePQHDChain(const CPQHDChain& chain)
{
    if (!chain.IsInitialized())
        return false;
    return WriteIC(std::string("pqhdchain"), chain);
}

bool CWalletDB::TxnBegin(int flags)
{
    return batch.TxnBegin(flags);
}

bool CWalletDB::TxnCommit(int flags)
{
    return batch.TxnCommit(flags);
}

bool CWalletDB::TxnAbort()
{
    return batch.TxnAbort();
}

bool CWalletDB::ReadVersion(int& nVersion)
{
    return batch.ReadVersion(nVersion);
}

bool CWalletDB::WriteVersion(int nVersion)
{
    return batch.WriteVersion(nVersion);
}

void CHDChain::SetSeedFromSeedId()
{
    // try to get the seed
	CKey seed;
	if (pwallet && !pwallet->IsLocked()) {
        if (!pwallet->GetKey(seed_id, seed)) {
            throw std::runtime_error(std::string(__func__) + ": seed not found");
        }
        vchSeed = SecureVector(seed.begin(), seed.end());
	}

	if (!pwallet)
        throw std::runtime_error(std::string(__func__) + ": wallet not found");
}

bool CHDChain::SetMnemonic(const SecureString& ssMnemonic, const SecureString& ssMnemonicPassphrase, SecureVector& vchSeed)
{
    SecureString ssMnemonicTmp = ssMnemonic;
    class ScopedMnemonicCleanser
    {
    private:
        SecureString& value;

    public:
        explicit ScopedMnemonicCleanser(SecureString& valueIn) : value(valueIn) {}
        ~ScopedMnemonicCleanser() { ClearSecureString(value); }
    } cleanseMnemonic(ssMnemonicTmp);

    // can't (re)set mnemonic if seed was already set
    if (!IsNull())
        return false;

    // empty mnemonic i.e. "generate a new one"
    if (ssMnemonic.empty()) {
        ssMnemonicTmp = CMnemonic::Generate(bUse_bip44 ? 128 : 256);
    }
    // NOTE: default mnemonic passphrase is an empty string
    if (!CMnemonic::Check(ssMnemonicTmp)) {
        throw std::runtime_error("SetMnemonic: invalid mnemonic");
    }

    if (!CMnemonic::ToSeed(ssMnemonicTmp, ssMnemonicPassphrase, vchSeed))
        return false;

    vchMnemonic = SecureVector(ssMnemonicTmp.begin(), ssMnemonicTmp.end());
    vchMnemonicPassphrase = SecureVector(ssMnemonicPassphrase.begin(), ssMnemonicPassphrase.end());

    return true;
}
