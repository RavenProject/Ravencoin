// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chainparams.h"
#include "chainparamsbase.h"
#include "consensus/validation.h"
#include "fs.h"
#include "hash.h"
#include "pqkey.h"
#include "support/allocators/zeroafterfree.h"
#include "test/test_raven.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "wallet/bip39.h"
#include "wallet/db.h"
#include "wallet/pqderivation.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace wallet_db {

class RecoveryTestAccess
{
public:
    enum class Fault {
        NONE,
        NULL_WRITE_TRANSACTION,
        WRITE_COMMIT,
        TEMP_CLOSE,
        SECOND_RENAME,
        INSTALL_COMMIT,
    };

    static bool Recover(const std::string& filename,
                        std::string& backupFilename,
                        Fault fault = Fault::NONE,
                        bool duplicateFirstRow = false,
                        bool forcePartialSalvage = false,
                        const std::string& tempFilename = std::string(),
                        const std::string& requestedBackupFilename = std::string(),
                        void* callbackData = nullptr,
                        bool (*callback)(void*, CDataStream, CDataStream) = nullptr)
    {
        CDB::RecoveryTestOptions options;
        switch (fault) {
        case Fault::NONE:
            options.fault = CDB::RecoveryFault::NONE;
            break;
        case Fault::NULL_WRITE_TRANSACTION:
            options.fault = CDB::RecoveryFault::NULL_WRITE_TRANSACTION;
            break;
        case Fault::WRITE_COMMIT:
            options.fault = CDB::RecoveryFault::WRITE_COMMIT;
            break;
        case Fault::TEMP_CLOSE:
            options.fault = CDB::RecoveryFault::TEMP_CLOSE;
            break;
        case Fault::SECOND_RENAME:
            options.fault = CDB::RecoveryFault::SECOND_RENAME;
            break;
        case Fault::INSTALL_COMMIT:
            options.fault = CDB::RecoveryFault::INSTALL_COMMIT;
            break;
        }
        options.duplicate_first_row = duplicateFirstRow;
        options.force_partial_salvage = forcePartialSalvage;
        options.temp_filename = tempFilename;
        options.backup_filename = requestedBackupFilename;
        return CDB::RecoverInternal(filename, callbackData, callback,
                                    backupFilename, &options);
    }

    static bool WriteRaw(const std::string& filename,
                         const std::vector<unsigned char>& key,
                         const std::vector<unsigned char>& value)
    {
        CWalletDBWrapper dbw(&bitdb, filename);
        CDB db(dbw, "c+");
        Dbt dbKey(key.empty() ? nullptr : const_cast<unsigned char*>(key.data()),
                  static_cast<u_int32_t>(key.size()));
        Dbt dbValue(value.empty() ? nullptr : const_cast<unsigned char*>(value.data()),
                    static_cast<u_int32_t>(value.size()));
        const int result = db.pdb->put(nullptr, &dbKey, &dbValue, 0);
        db.Close();
        bitdb.Flush(false);
        return result == 0;
    }

    static bool HasRaw(const std::string& filename,
                       const std::vector<unsigned char>& key,
                       size_t expectedValueSize)
    {
        CWalletDBWrapper dbw(&bitdb, filename);
        CDB db(dbw, "r");
        Dbt dbKey(key.empty() ? nullptr : const_cast<unsigned char*>(key.data()),
                  static_cast<u_int32_t>(key.size()));
        Dbt dbValue;
        dbValue.set_flags(DB_DBT_MALLOC);
        const int result = db.pdb->get(nullptr, &dbKey, &dbValue, 0);
        const bool matches = result == 0 && dbValue.get_size() == expectedValueSize;
        if (dbValue.get_data()) {
            memory_cleanse(dbValue.get_data(), dbValue.get_size());
            free(dbValue.get_data());
        }
        db.Close();
        bitdb.Flush(false);
        return matches;
    }
};

} // namespace wallet_db

namespace {

using PlainPQValue = std::pair<std::pair<CPQPubKey, std::vector<unsigned char>>, uint256>;
using CryptedPQValue = std::pair<CPQPubKey, std::vector<unsigned char>>;

const std::string BIP39_TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
const std::string BIP39_TEST_PASSPHRASE = "TREZOR";

std::vector<unsigned char> Bip39TestWords()
{
    return std::vector<unsigned char>(BIP39_TEST_MNEMONIC.begin(), BIP39_TEST_MNEMONIC.end());
}

std::vector<unsigned char> Bip39TestPassphrase()
{
    return std::vector<unsigned char>(BIP39_TEST_PASSPHRASE.begin(), BIP39_TEST_PASSPHRASE.end());
}

std::vector<unsigned char> Bip39TestSeed()
{
    // Trezor's independent BIP39 vector for the mnemonic and passphrase above.
    return ParseHex(
        "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e5349553"
        "1f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04");
}

template <typename Container>
uint256 PQLineageId(const Container& seed, uint8_t seedSource,
                    uint32_t coinType)
{
    uint256 lineageId;
    if (!pqderivation::GetLineageId(seed.data(), seed.size(), seedSource,
                                    coinType, lineageId)) {
        throw std::runtime_error("failed to derive PQ test lineage");
    }
    return lineageId;
}

class ChainParamsRestorer
{
private:
    const std::string original;

public:
    ChainParamsRestorer() : original(GetParams().NetworkIDString()) {}
    ~ChainParamsRestorer() { SelectParams(original); }
};

CHDChain Bip44TestChain(CWallet* wallet)
{
    CKey marker;
    marker.MakeNewKey(true);
    CHDChain chain(wallet);
    chain.UseBip44(true);
    chain.seed_id = marker.GetPubKey().GetID();
    return chain;
}

bool InitializeBip39Wallet(CWallet& wallet)
{
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> passphrase = Bip39TestPassphrase();
    const SecureString secureWords(words.begin(), words.end());
    const SecureString securePassphrase(passphrase.begin(), passphrase.end());
    SecureVector derivedSeed;
    if (!CMnemonic::ToSeed(secureWords, securePassphrase, derivedSeed))
        return false;
    const std::vector<unsigned char> expectedSeed = Bip39TestSeed();
    if (derivedSeed.size() != expectedSeed.size() ||
        !std::equal(derivedSeed.begin(), derivedSeed.end(), expectedSeed.begin())) {
        return false;
    }
    const std::vector<unsigned char> seed(derivedSeed.begin(), derivedSeed.end());
    const uint256 wordHash = Hash(words.begin(), words.end());
    CHDChain chain(&wallet);
    chain.UseBip44(true);
    chain.seed_id = CPubKey(seed.begin(), seed.end()).GetID();
    if (!wallet.SetHDChain(chain, false) ||
        !wallet.LoadWords(wordHash, words) ||
        !wallet.LoadPassphrase(passphrase) ||
        !wallet.LoadVchSeed(seed)) {
        return false;
    }

    CWalletDB walletdb(wallet.GetDBHandle());
    return walletdb.WriteBip39Words(wordHash, words, false) &&
           walletdb.WriteBip39Passphrase(passphrase, false) &&
           walletdb.WriteBip39VchSeed(seed, false);
}

bool InitializeLegacyHDWallet(CWallet& wallet,
                              const std::vector<unsigned char>& seedBytes)
{
    CKey seed;
    seed.Set(seedBytes.begin(), seedBytes.end(), true);
    if (!seed.IsValid())
        return false;
    {
        LOCK(wallet.cs_wallet);
        if (!wallet.AddKeyPubKey(seed, seed.GetPubKey()))
            return false;
    }
    CHDChain chain(&wallet);
    chain.UseBip44(false);
    chain.seed_id = seed.GetPubKey().GetID();
    return wallet.SetHDChain(chain, false);
}

std::vector<unsigned char> RawSecret(const CPQKey& key)
{
    return std::vector<unsigned char>(key.GetKeyData().begin(), key.GetKeyData().end());
}

PlainPQValue PlainPQRecord(const CPQPubKey& pubkey, const std::vector<unsigned char>& secret)
{
    std::vector<unsigned char> keyMaterial;
    keyMaterial.reserve(pubkey.size() + secret.size());
    keyMaterial.insert(keyMaterial.end(), pubkey.begin(), pubkey.end());
    keyMaterial.insert(keyMaterial.end(), secret.begin(), secret.end());
    return std::make_pair(std::make_pair(pubkey, secret), Hash(keyMaterial.begin(), keyMaterial.end()));
}

bool FileContainsSecret(const fs::path& path, const std::vector<unsigned char>& secret)
{
    std::ifstream file(path.string(), std::ios::binary);
    if (!file)
        throw std::runtime_error("failed to read PQ wallet test file");
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string needle(secret.begin(), secret.end());
    return contents.find(needle) != std::string::npos;
}

std::string ReadFileBytes(const fs::path& path)
{
    std::ifstream file(path.string(), std::ios::binary);
    if (!file)
        throw std::runtime_error("failed to read wallet recovery fixture");
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

void WriteRecoveryFixture(const std::string& filename, const std::string& value)
{
    CWalletDBWrapper dbw(&bitdb, filename);
    CDB db(dbw, "c+");
    if (!db.Write(std::string("recovery-fixture"), value))
        throw std::runtime_error("failed to write wallet recovery fixture");
    db.Close();
    bitdb.Flush(false);
}

bool ReadRecoveryFixture(const std::string& filename, std::string& value)
{
    CWalletDBWrapper dbw(&bitdb, filename);
    CDB db(dbw, "r");
    const bool result = db.Read(std::string("recovery-fixture"), value);
    db.Close();
    bitdb.Flush(false);
    return result;
}

bool ThrowingRecoveryFilter(void*, CDataStream, CDataStream)
{
    throw std::runtime_error("injected recovery callback exception");
}

class FailingPQPersistenceKeyStore : public CCryptoKeyStore
{
public:
    bool EncryptForTest(CKeyingMaterial& masterKey)
    {
        return EncryptKeys(masterKey);
    }

    bool AddCryptedPQKey(const CPQPubKey&, const std::vector<unsigned char>&) override
    {
        return false;
    }
};

class ThrowingPQPersistenceKeyStore : public FailingPQPersistenceKeyStore
{
public:
    bool AddCryptedPQKey(const CPQPubKey&, const std::vector<unsigned char>&) override
    {
        throw std::runtime_error("injected PQ persistence exception");
    }
};

class InspectableCryptoKeyStore : public CCryptoKeyStore
{
public:
    bool EncryptForTest(CKeyingMaterial& masterKey)
    {
        return EncryptKeys(masterKey);
    }

    bool GetCryptedKeyForTest(const CKeyID& keyID, std::vector<unsigned char>& cryptedSecret)
    {
        LOCK(cs_KeyStore);
        const auto it = mapCryptedKeys.find(keyID);
        if (it == mapCryptedKeys.end())
            return false;
        cryptedSecret = it->second.second;
        return true;
    }
};

class ScopedDBLockTimeout
{
private:
    DbEnv* env;
    db_timeout_t previousLockTimeout;
    db_timeout_t previousTxnTimeout;
    u_int32_t previousDeadlockPolicy;

public:
    explicit ScopedDBLockTimeout(DbEnv* envIn, db_timeout_t timeout)
        : env(envIn), previousLockTimeout(0), previousTxnTimeout(0), previousDeadlockPolicy(0)
    {
        if (!env ||
            env->get_timeout(&previousLockTimeout, DB_SET_LOCK_TIMEOUT) != 0 ||
            env->get_timeout(&previousTxnTimeout, DB_SET_TXN_TIMEOUT) != 0 ||
            env->get_lk_detect(&previousDeadlockPolicy) != 0) {
            throw std::runtime_error("failed to set Berkeley DB lock timeout");
        }

        bool lockTimeoutChanged = false;
        bool txnTimeoutChanged = false;
        if (env->set_timeout(timeout, DB_SET_LOCK_TIMEOUT) == 0) {
            lockTimeoutChanged = true;
            if (env->set_timeout(timeout, DB_SET_TXN_TIMEOUT) == 0) {
                txnTimeoutChanged = true;
                if (env->set_lk_detect(DB_LOCK_YOUNGEST) == 0)
                    return;
            }
        }

        if (txnTimeoutChanged)
            env->set_timeout(previousTxnTimeout, DB_SET_TXN_TIMEOUT);
        if (lockTimeoutChanged)
            env->set_timeout(previousLockTimeout, DB_SET_LOCK_TIMEOUT);
        env->set_lk_detect(previousDeadlockPolicy);
        throw std::runtime_error("failed to set Berkeley DB lock timeout");
    }

    ~ScopedDBLockTimeout()
    {
        if (env) {
            env->set_timeout(previousLockTimeout, DB_SET_LOCK_TIMEOUT);
            env->set_timeout(previousTxnTimeout, DB_SET_TXN_TIMEOUT);
            env->set_lk_detect(previousDeadlockPolicy);
        }
    }
};

class ScopedDBExpiredLockTimeout
{
private:
    DbEnv* env;
    db_timeout_t previousLockTimeout;
    db_timeout_t previousTxnTimeout;
    bool previousTimeNotGranted;

public:
    explicit ScopedDBExpiredLockTimeout(DbEnv* envIn, db_timeout_t timeout)
        : env(envIn), previousLockTimeout(0), previousTxnTimeout(0),
          previousTimeNotGranted(false)
    {
        u_int32_t previousFlags = 0;
        if (!env ||
            env->get_timeout(&previousLockTimeout, DB_SET_LOCK_TIMEOUT) != 0 ||
            env->get_timeout(&previousTxnTimeout, DB_SET_TXN_TIMEOUT) != 0 ||
            env->get_flags(&previousFlags) != 0) {
            throw std::runtime_error("failed to inspect Berkeley DB timeout state");
        }
        previousTimeNotGranted = (previousFlags & DB_TIME_NOTGRANTED) != 0;

        bool lockTimeoutChanged = false;
        bool txnTimeoutChanged = false;
        if (env->set_timeout(timeout, DB_SET_LOCK_TIMEOUT) == 0) {
            lockTimeoutChanged = true;
            if (env->set_timeout(0, DB_SET_TXN_TIMEOUT) == 0) {
                txnTimeoutChanged = true;
                if (env->set_flags(DB_TIME_NOTGRANTED, 1) == 0)
                    return;
            }
        }

        if (txnTimeoutChanged)
            env->set_timeout(previousTxnTimeout, DB_SET_TXN_TIMEOUT);
        if (lockTimeoutChanged)
            env->set_timeout(previousLockTimeout, DB_SET_LOCK_TIMEOUT);
        env->set_flags(DB_TIME_NOTGRANTED, previousTimeNotGranted ? 1 : 0);
        throw std::runtime_error("failed to configure Berkeley DB lock expiration");
    }

    ~ScopedDBExpiredLockTimeout()
    {
        if (env) {
            env->set_timeout(previousLockTimeout, DB_SET_LOCK_TIMEOUT);
            env->set_timeout(previousTxnTimeout, DB_SET_TXN_TIMEOUT);
            env->set_flags(DB_TIME_NOTGRANTED, previousTimeNotGranted ? 1 : 0);
        }
    }
};

class ScopedArgState
{
private:
    std::string name;
    bool wasSet;
    std::string value;

public:
    explicit ScopedArgState(const std::string& nameIn)
        : name(nameIn), wasSet(gArgs.IsArgSet(nameIn)),
          value(gArgs.GetArg(nameIn, std::string()))
    {
    }

    ~ScopedArgState()
    {
        if (wasSet)
            gArgs.ForceSetArg(name, value);
        else
            gArgs.ClearArg(name);
    }
};

class ScopedMnemonicInput
{
private:
    SecureString words;
    SecureString passphrase;
    bool hadInput;

public:
    ScopedMnemonicInput()
        : hadInput(TakePendingMnemonicInput(words, passphrase))
    {
    }

    ~ScopedMnemonicInput()
    {
        ClearPendingMnemonicInput();
        if (hadInput)
            SetPendingMnemonicInput(std::move(words), std::move(passphrase));
    }
};

class ScopedThreadCancellation
{
private:
    std::thread& thread;
    std::atomic<bool>& firstStop;
    std::atomic<bool>& secondStop;

public:
    ScopedThreadCancellation(
        std::thread& threadIn,
        std::atomic<bool>& firstStopIn,
        std::atomic<bool>& secondStopIn)
        : thread(threadIn), firstStop(firstStopIn), secondStop(secondStopIn)
    {
    }

    ~ScopedThreadCancellation()
    {
        firstStop = true;
        secondStop = true;
        if (thread.joinable())
            thread.join();
    }
};

bool WalletContainsAnyRecordType(
    const std::string& filename,
    const std::vector<std::string>& recordTypes)
{
    CWalletDBWrapper dbw(&bitdb, filename);
    CDB db(dbw, "r");
    Dbc* cursor = db.GetCursor();
    if (!cursor)
        throw std::runtime_error("failed to open wallet record cursor");

    while (true) {
        CDataStream key(SER_DISK, CLIENT_VERSION);
        CDataStream value(SER_DISK, CLIENT_VERSION);
        const int result = db.ReadAtCursor(cursor, key, value);
        if (result == DB_NOTFOUND)
            break;
        if (result != 0) {
            cursor->close();
            throw std::runtime_error("failed to read wallet record cursor");
        }

        std::string type;
        key >> type;
        for (const std::string& expected : recordTypes) {
            if (type == expected) {
                cursor->close();
                return true;
            }
        }
    }

    if (cursor->close() != 0)
        throw std::runtime_error("failed to close wallet record cursor");
    return false;
}

std::unique_ptr<CWallet> LoadPQWallet(const std::string& filename)
{
    std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
    bool firstRun = false;
    if (wallet->LoadWallet(firstRun) != DB_LOAD_OK)
        throw std::runtime_error("failed to load PQ wallet test database");
    return wallet;
}

struct PQWalletDatabaseTestingSetup : public TestingSetup
{
    int64_t oldKeypoolSize;

    PQWalletDatabaseTestingSetup()
        : TestingSetup(CBaseChainParams::REGTEST),
          oldKeypoolSize(gArgs.GetArg("-keypool", DEFAULT_KEYPOOL_SIZE))
    {
        // WalletTestingSetup uses a mock database. These tests intentionally
        // exercise the actual Berkeley DB persistence, rewrite, and backup paths.
        bitdb.Close();
        bitdb.Reset();
        gArgs.ForceSetArg("-keypool", 1);
    }

    ~PQWalletDatabaseTestingSetup()
    {
        bitdb.Flush(true);
        bitdb.Reset();
        gArgs.ForceSetArg("-keypool", oldKeypoolSize);
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(pq_wallet_tests, PQWalletDatabaseTestingSetup)

BOOST_AUTO_TEST_CASE(protected_asset_destination_pair_survives_reload)
{
    const std::string filename = "pq-asset-destination-pair-wallet.dat";
    CKey classicalKey;
    CKey otherClassicalKey;
    classicalKey.MakeNewKey(true);
    otherClassicalKey.MakeNewKey(true);
    CPQKey pqKey;
    CPQKey otherPQKey;
    pqKey.MakeNewKey();
    otherPQKey.MakeNewKey();
    BOOST_REQUIRE(pqKey.IsValid());
    BOOST_REQUIRE(otherPQKey.IsValid());
    const uint256 pqProgram = pqKey.GetPubKey().GetWitnessProgram();
    const uint256 otherProgram = otherPQKey.GetPubKey().GetWitnessProgram();
    const std::string descriptor = EncodePQAssetDestination(classicalKey.GetPubKey().GetID(), pqProgram);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddKeyPubKey(classicalKey, classicalKey.GetPubKey()));
        BOOST_REQUIRE(wallet->AddKeyPubKey(otherClassicalKey, otherClassicalKey.GetPubKey()));
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(pqKey, pqKey.GetPubKey()));
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(otherPQKey, otherPQKey.GetPubKey()));
        BOOST_REQUIRE(wallet->StoreOwnedPQAssetDestination(classicalKey.GetPubKey().GetID(), pqProgram));
        BOOST_CHECK(wallet->StoreOwnedPQAssetDestination(classicalKey.GetPubKey().GetID(), pqProgram));
        BOOST_CHECK(!wallet->StoreOwnedPQAssetDestination(classicalKey.GetPubKey().GetID(), otherProgram));
        BOOST_CHECK(!wallet->StoreOwnedPQAssetDestination(otherClassicalKey.GetPubKey().GetID(), pqProgram));
        BOOST_CHECK(wallet->GetOwnedPQAssetDestinations() == std::vector<std::string>{descriptor});
    }
    bitdb.Flush(false);

    std::unique_ptr<CWallet> reloaded = LoadPQWallet(filename);
    BOOST_CHECK(reloaded->GetOwnedPQAssetDestinations() == std::vector<std::string>{descriptor});
    BOOST_REQUIRE(reloaded->LoadDestData(
        otherClassicalKey.GetPubKey().GetID(), "pqasset:destination:v1", descriptor));
    BOOST_CHECK(reloaded->GetOwnedPQAssetDestinations() == std::vector<std::string>{descriptor});

    CWallet unrelatedWallet;
    BOOST_REQUIRE(unrelatedWallet.LoadDestData(
        classicalKey.GetPubKey().GetID(), "pqasset:destination:v1", descriptor));
    BOOST_CHECK(unrelatedWallet.GetOwnedPQAssetDestinations().empty());
}

BOOST_AUTO_TEST_CASE(pq_only_asset_destination_needs_no_classical_key)
{
    const std::string filename = "pq-only-asset-destination-wallet.dat";
    CPQKey pqKey;
    pqKey.MakeNewKey();
    BOOST_REQUIRE(pqKey.IsValid());
    const uint256 program = pqKey.GetPubKey().GetWitnessProgram();
    const CKeyID pseudoAddress(Hash160(program.begin(), program.end()));
    const std::string descriptor = EncodePQAssetDestination(pseudoAddress, program);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(pqKey, pqKey.GetPubKey()));
        BOOST_CHECK(!wallet->HaveKey(pseudoAddress));
        BOOST_REQUIRE(wallet->StoreOwnedPQAssetDestination(pseudoAddress, program));
        BOOST_CHECK(wallet->IsPQOnlyAssetID(pseudoAddress));
        BOOST_CHECK(wallet->GetOwnedPQAssetDestinations() ==
                    std::vector<std::string>{descriptor});
    }
    bitdb.Flush(false);

    std::unique_ptr<CWallet> reloaded = LoadPQWallet(filename);
    BOOST_CHECK(!reloaded->HaveKey(pseudoAddress));
    BOOST_CHECK(reloaded->IsPQOnlyAssetID(pseudoAddress));
    BOOST_CHECK(reloaded->GetOwnedPQAssetDestinations() ==
                std::vector<std::string>{descriptor});

    CWallet dummyWallet;
    std::string backupFilename;
    BOOST_REQUIRE(CWalletDB::Recover(
        filename, &dummyWallet, CWalletDB::RecoverKeysOnlyFilter, backupFilename));
    bitdb.Flush(false);
    std::unique_ptr<CWallet> recovered = LoadPQWallet(filename);
    BOOST_CHECK(recovered->GetOwnedPQAssetDestinations() ==
                std::vector<std::string>{descriptor});
    BOOST_CHECK(recovered->IsPQOnlyAssetID(pseudoAddress));
    BOOST_CHECK_EQUAL(recovered->mapAddressBook[pseudoAddress].purpose, "pqasset");

    auto retainedPair = [&](const CKeyID& address, const std::string& key) {
        CWallet filterWallet;
        CDataStream dbKey(SER_DISK, CLIENT_VERSION);
        CDataStream dbValue(SER_DISK, CLIENT_VERSION);
        dbKey << std::string("destdata") << EncodeDestination(address) << key;
        dbValue << descriptor;
        return CWalletDB::RecoverKeysOnlyFilter(
            &filterWallet, std::move(dbKey), std::move(dbValue));
    };
    BOOST_CHECK(retainedPair(pseudoAddress, "pqasset:destination:v2"));
    CKey unrelatedKey;
    unrelatedKey.MakeNewKey(true);
    BOOST_CHECK(!retainedPair(unrelatedKey.GetPubKey().GetID(),
                              "pqasset:destination:v2"));
}

BOOST_AUTO_TEST_CASE(protected_asset_destination_pair_survives_key_only_recovery)
{
    const std::string filename = "pq-asset-pair-salvage-wallet.dat";
    CKey classicalKey;
    classicalKey.MakeNewKey(true);
    CPQKey pqKey;
    pqKey.MakeNewKey();
    BOOST_REQUIRE(pqKey.IsValid());
    const CKeyID classicalId = classicalKey.GetPubKey().GetID();
    const uint256 pqProgram = pqKey.GetPubKey().GetWitnessProgram();
    const std::string descriptor = EncodePQAssetDestination(classicalId, pqProgram);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddKeyPubKey(classicalKey, classicalKey.GetPubKey()));
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(pqKey, pqKey.GetPubKey()));
        BOOST_REQUIRE(wallet->StoreOwnedPQAssetDestination(classicalId, pqProgram));
    }
    bitdb.Flush(false);

    CWallet dummyWallet;
    std::string backupFilename;
    BOOST_REQUIRE(CWalletDB::Recover(
        filename, &dummyWallet, CWalletDB::RecoverKeysOnlyFilter, backupFilename));
    bitdb.Flush(false);

    std::unique_ptr<CWallet> recovered = LoadPQWallet(filename);
    BOOST_CHECK(recovered->GetOwnedPQAssetDestinations() ==
                std::vector<std::string>{descriptor});

    auto retainedDestData = [&](const std::string& address, const std::string& key,
                                const std::string& value, bool trailing) {
        CWallet filterWallet;
        CDataStream dbKey(SER_DISK, CLIENT_VERSION);
        CDataStream dbValue(SER_DISK, CLIENT_VERSION);
        dbKey << std::string("destdata") << address << key;
        dbValue << value;
        if (trailing)
            dbValue << uint8_t{1};
        return CWalletDB::RecoverKeysOnlyFilter(
            &filterWallet, std::move(dbKey), std::move(dbValue));
    };
    BOOST_CHECK(retainedDestData(EncodeDestination(classicalId),
                                 "pqasset:destination:v1", descriptor, false));
    BOOST_CHECK(!retainedDestData(EncodeDestination(classicalId),
                                  "pqasset:destination:v1", "invalid", false));
    CKey otherClassicalKey;
    otherClassicalKey.MakeNewKey(true);
    BOOST_CHECK(!retainedDestData(
        EncodeDestination(otherClassicalKey.GetPubKey().GetID()),
        "pqasset:destination:v1", descriptor, false));
    BOOST_CHECK(!retainedDestData(EncodeDestination(classicalId),
                                  "pqasset:destination:v1", descriptor, true));
    BOOST_CHECK(!retainedDestData(EncodeDestination(classicalId),
                                  "pqasset:destination:v1", std::string(300, 'x'), false));
    BOOST_CHECK(!retainedDestData(EncodeDestination(classicalId),
                                  "unrelated:metadata", descriptor, false));
}

BOOST_AUTO_TEST_CASE(pending_mnemonic_input_is_single_consumption)
{
    ScopedMnemonicInput restoreInput;
    ClearPendingMnemonicInput();

    SetPendingMnemonicInput(
        SecureString(BIP39_TEST_MNEMONIC.begin(), BIP39_TEST_MNEMONIC.end()),
        SecureString(BIP39_TEST_PASSPHRASE.begin(), BIP39_TEST_PASSPHRASE.end()));
    BOOST_CHECK(HasPendingMnemonicInput());

    SecureString words;
    SecureString passphrase;
    BOOST_REQUIRE(TakePendingMnemonicInput(words, passphrase));
    BOOST_CHECK_EQUAL(
        std::string(words.begin(), words.end()), BIP39_TEST_MNEMONIC);
    BOOST_CHECK_EQUAL(
        std::string(passphrase.begin(), passphrase.end()), BIP39_TEST_PASSPHRASE);
    BOOST_CHECK_GE(words.capacity(), 64U);
    BOOST_CHECK_GE(passphrase.capacity(), 64U);
    BOOST_CHECK(!HasPendingMnemonicInput());

    words.assign(32, 'w');
    passphrase.assign(32, 'p');
    BOOST_CHECK(!TakePendingMnemonicInput(words, passphrase));
    BOOST_CHECK(words.empty());
    BOOST_CHECK(passphrase.empty());
    BOOST_CHECK_EQUAL(words.capacity(), SecureString().capacity());
    BOOST_CHECK_EQUAL(passphrase.capacity(), SecureString().capacity());
}

BOOST_AUTO_TEST_CASE(mnemonic_arguments_are_consumed_on_success_and_failure)
{
    ScopedArgState mnemonicArg("-mnemonic");
    ScopedArgState passphraseArg("-mnemonicpassphrase");
    ScopedMnemonicInput restoreInput;
    ClearPendingMnemonicInput();

    const std::string filename = "secure-mnemonic-arguments-wallet.dat";
    std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
    wallet->UseBip44(true);
    gArgs.ForceSetArg("-mnemonic", BIP39_TEST_MNEMONIC);
    gArgs.ForceSetArg("-mnemonicpassphrase", BIP39_TEST_PASSPHRASE);
    BOOST_CHECK_NO_THROW((void)wallet->GenerateNewSeed());
    BOOST_CHECK(!gArgs.IsArgSet("-mnemonic"));
    BOOST_CHECK(!gArgs.IsArgSet("-mnemonicpassphrase"));
    BOOST_CHECK(gArgs.GetArgs("-mnemonic").empty());
    BOOST_CHECK(gArgs.GetArgs("-mnemonicpassphrase").empty());

    gArgs.ForceSetArg("-mnemonic", "not a valid mnemonic");
    gArgs.ForceSetArg("-mnemonicpassphrase", "failure-path-secret");
    BOOST_CHECK_THROW(wallet->GenerateNewSeed(), std::runtime_error);
    BOOST_CHECK(!gArgs.IsArgSet("-mnemonic"));
    BOOST_CHECK(!gArgs.IsArgSet("-mnemonicpassphrase"));
    BOOST_CHECK(gArgs.GetArgs("-mnemonic").empty());
    BOOST_CHECK(gArgs.GetArgs("-mnemonicpassphrase").empty());
}

BOOST_AUTO_TEST_CASE(invalid_mnemonic_error_never_discloses_phrase)
{
    CHDChain chain(nullptr);
    chain.UseBip44(true);
    const SecureString invalid("sentinel-private-recovery-words");
    const SecureString passphrase;
    SecureVector seed;

    try {
        chain.SetMnemonic(invalid, passphrase, seed);
        BOOST_FAIL("invalid mnemonic was accepted");
    } catch (const std::runtime_error& error) {
        const std::string message(error.what());
        BOOST_CHECK(message.find("sentinel-private-recovery-words") ==
                    std::string::npos);
        BOOST_CHECK(message.find("invalid mnemonic") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(cancelled_mnemonic_prompt_cleans_pending_secrets)
{
    ScopedArgState mnemonicArg("-mnemonic");
    ScopedArgState passphraseArg("-mnemonicpassphrase");
    ScopedMnemonicInput restoreInput;
    gArgs.ClearArg("-mnemonic");
    gArgs.ClearArg("-mnemonicpassphrase");
    ClearPendingMnemonicInput();

    boost::signals2::scoped_connection mnemonicConnection(
        uiInterface.ShowMnemonic.connect([&](int) {
            SetPendingMnemonicInput(
                SecureString(BIP39_TEST_MNEMONIC.begin(),
                             BIP39_TEST_MNEMONIC.end()),
                SecureString(BIP39_TEST_PASSPHRASE.begin(),
                             BIP39_TEST_PASSPHRASE.end()));
            throw std::runtime_error("mnemonic prompt cancelled");
        }));

    BOOST_CHECK_THROW(
        CWallet::CreateWalletFromFile("cancelled-mnemonic-prompt-wallet.dat"),
        std::runtime_error);
    BOOST_CHECK(!HasPendingMnemonicInput());
    mnemonicConnection.disconnect();
}

BOOST_AUTO_TEST_CASE(dismissed_mnemonic_prompt_does_not_create_wallet)
{
    const std::string filename = "dismissed-mnemonic-prompt-wallet.dat";
    ScopedArgState mnemonicArg("-mnemonic");
    ScopedArgState passphraseArg("-mnemonicpassphrase");
    ScopedArgState bip44Arg("-bip44");
    ScopedMnemonicInput restoreInput;
    gArgs.ClearArg("-mnemonic");
    gArgs.ClearArg("-mnemonicpassphrase");
    gArgs.ForceSetArg("-bip44", "1");
    ClearPendingMnemonicInput();

    unsigned int promptCount = 0;
    unsigned int loadNotifications = 0;
    boost::signals2::scoped_connection mnemonicConnection(
        uiInterface.ShowMnemonic.connect([&](int) {
            ++promptCount;
            // A dismissed modal dialog supplies no mnemonic.
        }));
    boost::signals2::scoped_connection loadConnection(
        uiInterface.LoadWallet.connect([&](CWallet*) {
            ++loadNotifications;
        }));

    CWallet* unexpectedWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        unexpectedWallet = CWallet::CreateWalletFromFile(filename));
    if (unexpectedWallet) {
        UnregisterValidationInterface(unexpectedWallet);
        delete unexpectedWallet;
    }
    BOOST_CHECK(unexpectedWallet == nullptr);
    BOOST_CHECK_EQUAL(promptCount, 1U);
    BOOST_CHECK_EQUAL(loadNotifications, 0U);
    BOOST_CHECK(!HasPendingMnemonicInput());
    BOOST_CHECK(!WalletContainsAnyRecordType(
        filename, {"hdchain", "bip39words", "bip39passphrase", "bip39vchseed",
                   "key", "wkey", "ckey", "keymeta", "pool"}));

    {
        std::unique_ptr<CWalletDBWrapper> probeDbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet probe(std::move(probeDbw));
        bool firstRun = false;
        BOOST_REQUIRE_EQUAL(probe.LoadWallet(firstRun, false), DB_LOAD_OK);
        BOOST_CHECK(firstRun);
    }

    mnemonicConnection.disconnect();
    boost::signals2::scoped_connection recoveryConnection(
        uiInterface.ShowMnemonic.connect([&](int) {
            ++promptCount;
            SetPendingMnemonicInput(
                SecureString(BIP39_TEST_MNEMONIC.begin(),
                             BIP39_TEST_MNEMONIC.end()),
                SecureString(BIP39_TEST_PASSPHRASE.begin(),
                             BIP39_TEST_PASSPHRASE.end()));
        }));
    CWallet* recoveredWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        recoveredWallet = CWallet::CreateWalletFromFile(filename));
    BOOST_REQUIRE(recoveredWallet != nullptr);
    BOOST_CHECK_EQUAL(promptCount, 2U);
    BOOST_CHECK_EQUAL(loadNotifications, 1U);
    BOOST_CHECK(!HasPendingMnemonicInput());

    uint256 wordHash;
    std::vector<unsigned char> words;
    std::vector<unsigned char> passphrase;
    std::vector<unsigned char> seed;
    recoveredWallet->GetBip39Data(wordHash, words, passphrase, seed);
    BOOST_CHECK(words == Bip39TestWords());
    BOOST_CHECK(passphrase == Bip39TestPassphrase());
    BOOST_CHECK(seed == Bip39TestSeed());
    UnregisterValidationInterface(recoveredWallet);
    delete recoveredWallet;
}

BOOST_AUTO_TEST_CASE(deterministic_pq_generation_requires_hd_root)
{
    const std::string filename = "pq-hd-root-required-wallet.dat";
    std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
    CPQPubKey rejected;
    uint32_t index = 99;
    BOOST_CHECK(!wallet->GenerateNewPQKey(rejected, &index));
    BOOST_CHECK(!rejected.IsValid());
    BOOST_CHECK_EQUAL(index, 99U);
    CWalletDBWrapper rawDbw(&bitdb, filename);
    CDB rawDb(rawDbw, "r");
    BOOST_CHECK(!rawDb.Exists(std::string("pqhdchain")));
}

BOOST_AUTO_TEST_CASE(pq_hd_derivation_kats_are_byte_exact)
{
    const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
    struct Vector {
        uint32_t coinType;
        uint32_t index;
        const char* expected;
    };
    const Vector bip39Vectors[] = {
        {175, 0, "5312ca47967e38c2c45a56837491a4b4a627bc697c4f247a7a090a854d798222"},
        {175, 1, "65e9c6a22716d7e17d016662c4e86a7002962f3f2813fbdd78e6effda879bebc"},
        {1, 0, "e0f3d1cfb06da142ccdbdc54aed4e131c9ab16403d97a33964bad3dc99f45e2d"},
        {1, 1, "14269645b7522fdc5274d7ae574302a30745fc9614f7308cae54f12856141db4"},
    };
    for (const Vector& vector : bip39Vectors) {
        SecureVector derived;
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            bip39Seed.data(), bip39Seed.size(), vector.coinType,
            vector.index, derived));
        BOOST_CHECK_EQUAL(HexStr(derived.begin(), derived.end()), vector.expected);
    }

    const std::vector<unsigned char> legacySeed = ParseHex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f");
    const Vector legacyVectors[] = {
        {175, 0, "60ace9551ccdc2f6b3872df764898bfee33689b6fe5bb02215e0bb93ed1639ee"},
        {175, 1, "693092e2a91919547ac036129a2f9bdd3025da51fa73f78edc12f54f851f0275"},
        {1, 0, "59537f793a61662cc2ec3d577583e75383b89d0e45c4b16e161845e056a21016"},
        {1, 1, "f5c4e8b65088739f73599932b3ec9fd11d6fcb69a6d07438653de7474c79cde9"},
    };
    for (const Vector& vector : legacyVectors) {
        SecureVector derived;
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            legacySeed.data(), legacySeed.size(), vector.coinType,
            vector.index, derived));
        BOOST_CHECK_EQUAL(HexStr(derived.begin(), derived.end()), vector.expected);
    }

    uint256 lineageId;
    BOOST_REQUIRE(pqderivation::GetLineageId(
        bip39Seed.data(), bip39Seed.size(), pqderivation::SEED_SOURCE_BIP39,
        175, lineageId));
    BOOST_CHECK_EQUAL(
        HexStr(lineageId.begin(), lineageId.end()),
        "ea1634102982fea3e1b48a882a0ae86f124efdf1bf87276045ae7054e55d0443");
    BOOST_REQUIRE(pqderivation::GetLineageId(
        legacySeed.data(), legacySeed.size(),
        pqderivation::SEED_SOURCE_LEGACY_HD, 1, lineageId));
    BOOST_CHECK_EQUAL(
        HexStr(lineageId.begin(), lineageId.end()),
        "59243d9ad7de94feee944dd1a1a17b0f93d645a1fa2b28f35a2b0e6440b17553");
    BOOST_CHECK(!pqderivation::GetLineageId(
        bip39Seed.data(), bip39Seed.size(),
        pqderivation::SEED_SOURCE_LEGACY_HD, 1, lineageId));
    BOOST_CHECK(lineageId.IsNull());
    BOOST_CHECK(!pqderivation::GetLineageId(
        bip39Seed.data(), bip39Seed.size(), pqderivation::SEED_SOURCE_BIP39,
        pqderivation::HARDENED_LIMIT, lineageId));

    SecureVector output(32, 0x7f);
    BOOST_CHECK(!pqderivation::DeriveSeed(nullptr, 64, 1, 0, output));
    BOOST_CHECK(output.empty());
    BOOST_CHECK(!pqderivation::DeriveSeed(
        legacySeed.data(), legacySeed.size() - 1, 1, 0, output));
    BOOST_CHECK(!pqderivation::DeriveSeed(
        legacySeed.data(), legacySeed.size(), pqderivation::HARDENED_LIMIT, 0,
        output));
    BOOST_CHECK(!pqderivation::DeriveSeed(
        legacySeed.data(), legacySeed.size(), 1,
        pqderivation::HARDENED_LIMIT, output));
    BOOST_CHECK_EQUAL(pqderivation::GetKeypath(175, 7), "m/25'/175'/0'/0'/7'");
    BOOST_CHECK(pqderivation::GetKeypath(1, pqderivation::HARDENED_LIMIT).empty());
}

BOOST_AUTO_TEST_CASE(pq_hd_chain_record_is_key_critical_and_strict)
{
    BOOST_CHECK(CWalletDB::IsKeyType("pqhdchain"));

    CPQHDChain fieldChecks;
    BOOST_CHECK(fieldChecks.IsValid());
    BOOST_CHECK(!fieldChecks.IsInitialized());
    fieldChecks.SetLineage(CPQHDChain::SEED_SOURCE_BIP39, 1,
                           uint256S("01"));
    BOOST_CHECK(fieldChecks.IsInitialized());
    fieldChecks.nSeedSource = 3;
    BOOST_CHECK(!fieldChecks.IsValid());
    fieldChecks.SetLineage(CPQHDChain::SEED_SOURCE_BIP39,
                           CPQHDChain::MAX_COUNTER, uint256S("01"));
    BOOST_CHECK(!fieldChecks.IsValid());
    fieldChecks.SetLineage(CPQHDChain::SEED_SOURCE_BIP39, 1, uint256());
    BOOST_CHECK(!fieldChecks.IsValid());
    fieldChecks.SetLineage(CPQHDChain::SEED_SOURCE_BIP39, 1,
                           uint256S("01"));
    fieldChecks.nExternalChainCounter = CPQHDChain::MAX_COUNTER;
    BOOST_CHECK(fieldChecks.IsValid());
    fieldChecks.nExternalChainCounter = UINT32_MAX;
    BOOST_CHECK(!fieldChecks.IsValid());

    CPQHDChain layout;
    layout.SetLineage(CPQHDChain::SEED_SOURCE_BIP39, 0x01020304U,
                      uint256S("01"));
    layout.nExternalChainCounter = 0x11223344U;
    CDataStream serializedLayout(SER_DISK, CLIENT_VERSION);
    serializedLayout << layout;
    BOOST_CHECK_EQUAL(serializedLayout.size(), 45U);
    BOOST_CHECK_EQUAL(
        HexStr(serializedLayout.begin(), serializedLayout.end()),
        "010000004433221102040302010100000000000000000000000000000000000000"
        "000000000000000000000000");

    const std::string validFilename = "pq-hd-chain-valid-wallet.dat";
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(validFilename);
        CHDChain hd(wallet.get());
        CKey marker;
        marker.MakeNewKey(true);
        hd.seed_id = marker.GetPubKey().GetID();
        BOOST_REQUIRE(wallet->SetHDChain(hd, false));
        CPQHDChain pq;
        pq.SetLineage(CPQHDChain::SEED_SOURCE_LEGACY_HD, 175,
                      uint256S("01"));
        pq.nExternalChainCounter = 7;
        CWalletDB walletdb(wallet->GetDBHandle());
        BOOST_REQUIRE(walletdb.WritePQHDChain(pq));
    }
    bitdb.Flush(false);
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(validFilename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nVersion, CPQHDChain::CURRENT_VERSION);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 7U);
    }

    const std::string standaloneFilename = "pq-hd-chain-standalone-wallet.dat";
    {
        CWalletDBWrapper dbw(&bitdb, standaloneFilename);
        CWalletDB walletdb(dbw, "c+");
        CKey marker;
        marker.MakeNewKey(true);
        CPQHDChain pq;
        pq.SetLineage(CPQHDChain::SEED_SOURCE_LEGACY_HD, 175,
                      uint256S("02"));
        BOOST_REQUIRE(walletdb.WritePQHDChain(pq));
    }
    bitdb.Flush(false);
    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, standaloneFilename));
        CWallet wallet(std::move(dbw));
        bool firstRun = true;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_CORRUPT);
    }

    const std::string malformedFilename = "pq-hd-chain-malformed-wallet.dat";
    {
        CWalletDBWrapper dbw(&bitdb, malformedFilename);
        CWalletDB walletdb(dbw, "c+");
        CKey marker;
        marker.MakeNewKey(true);
        CHDChain hd(nullptr);
        hd.seed_id = marker.GetPubKey().GetID();
        BOOST_REQUIRE(walletdb.WriteHDChain(hd));
        CDB raw(dbw, "r+");
        CPQHDChain malformed;
        malformed.SetLineage(CPQHDChain::SEED_SOURCE_LEGACY_HD, 175,
                             uint256S("03"));
        malformed.nVersion = CPQHDChain::CURRENT_VERSION + 1;
        BOOST_REQUIRE(raw.Write(std::string("pqhdchain"), malformed));
    }
    bitdb.Flush(false);
    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, malformedFilename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_CORRUPT);
    }

    const std::string trailingFilename = "pq-hd-chain-trailing-wallet.dat";
    {
        CWalletDBWrapper dbw(&bitdb, trailingFilename);
        CWalletDB walletdb(dbw, "c+");
        CKey marker;
        marker.MakeNewKey(true);
        CHDChain hd(nullptr);
        hd.seed_id = marker.GetPubKey().GetID();
        BOOST_REQUIRE(walletdb.WriteHDChain(hd));
    }
    CDataStream rawKey(SER_DISK, CLIENT_VERSION);
    CDataStream rawValue(SER_DISK, CLIENT_VERSION);
    rawKey << std::string("pqhdchain");
    CPQHDChain trailingChain;
    trailingChain.SetLineage(CPQHDChain::SEED_SOURCE_LEGACY_HD, 175,
                             uint256S("04"));
    rawValue << trailingChain;
    rawValue << uint8_t{0x42};
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::WriteRaw(
        trailingFilename,
        std::vector<unsigned char>(rawKey.begin(), rawKey.end()),
        std::vector<unsigned char>(rawValue.begin(), rawValue.end())));
    bitdb.Flush(false);
    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, trailingFilename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_CORRUPT);
    }
}

BOOST_AUTO_TEST_CASE(deterministic_pq_wallet_derivation_recovers_and_advances)
{
    const std::string firstFilename = "pq-hd-first-wallet.dat";
    const std::string backupFilename = "pq-hd-first-wallet-backup.dat";
    const std::string restoredFilename = "pq-hd-restored-wallet.dat";
    CPQPubKey firstIndex0;
    CPQPubKey firstIndex1;

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(firstFilename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(firstIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_REQUIRE(wallet->GenerateNewPQKey(firstIndex1, &index));
        BOOST_CHECK_EQUAL(index, 1U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 2U);
        BOOST_CHECK_EQUAL(
            firstIndex0.GetWitnessProgram().GetHex(),
            "3b2b571eb1bf9f935a19f2acbe99ce27fb7d3519a54e4b2f6017f5f3a876c9ad");
        BOOST_CHECK_EQUAL(
            EncodeDestination(WitnessV2PQDestination(
                firstIndex0.GetWitnessProgram())),
            "rcrt1z4hyhd28n75tkqt6tf6j3jdtalvnuaxd74nepjk5nn7lmz8jh9vasg4ztx8");
        BOOST_REQUIRE(wallet->BackupWallet(
            (GetDataDir() / backupFilename).string()));
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(backupFilename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 2U);
        CPQKey loaded;
        BOOST_REQUIRE(wallet->GetPQKey(firstIndex0.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(firstIndex0));
        BOOST_REQUIRE(wallet->GetPQKey(firstIndex1.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(firstIndex1));
    }
    bitdb.Flush(false);

    CPQPubKey restoredIndex0;
    CPQPubKey restoredIndex1;
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(restoredFilename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(restoredIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_REQUIRE(wallet->GenerateNewPQKey(restoredIndex1, &index));
        BOOST_CHECK_EQUAL(index, 1U);
    }

    BOOST_CHECK(restoredIndex0 == firstIndex0);
    BOOST_CHECK(restoredIndex1 == firstIndex1);

    for (uint32_t index = 0; index < 2; ++index) {
        SecureVector seed;
        const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            bip39Seed.data(), bip39Seed.size(), 1, index, seed));
        CPQKey expected;
        BOOST_REQUIRE(expected.SetSeed(seed.data()));
        BOOST_CHECK(expected.GetPubKey() == (index == 0 ? firstIndex0 : firstIndex1));
    }

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(firstFilename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 2U);
        CPQKey loaded;
        BOOST_REQUIRE(wallet->GetPQKey(firstIndex0.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(firstIndex0));
        BOOST_REQUIRE(wallet->GetPQKey(firstIndex1.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(firstIndex1));

        CPQPubKey index2PubKey;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(index2PubKey, &index));
        BOOST_CHECK_EQUAL(index, 2U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 3U);

        SecureVector seed;
        const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            bip39Seed.data(), bip39Seed.size(), 1, 2, seed));
        CPQKey expected;
        BOOST_REQUIRE(expected.SetSeed(seed.data()));
        BOOST_CHECK(expected.GetPubKey() == index2PubKey);
    }
}

BOOST_AUTO_TEST_CASE(encrypted_deterministic_pq_generation_is_ciphertext_only)
{
    const std::string filename = "pq-hd-encrypted-wallet.dat";
    const SecureString passphrase("pq-hd-encrypted-passphrase");
    CPQPubKey generated;

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        CKey classicalKey;
        classicalKey.MakeNewKey(true);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(
                classicalKey, classicalKey.GetPubKey()));
        }
        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
        CPQPubKey lockedAttempt;
        uint32_t lockedIndex = 99;
        BOOST_CHECK(!wallet->GenerateNewPQKey(lockedAttempt, &lockedIndex));
        BOOST_CHECK(!lockedAttempt.IsValid());
        BOOST_CHECK_EQUAL(lockedIndex, 99U);
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(generated, &index));
        BOOST_CHECK_EQUAL(index, 0U);

        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        const uint256 witnessProgram = generated.GetWitnessProgram();
        BOOST_CHECK(rawDb.Exists(
            std::make_pair(std::string("cpqkey"), witnessProgram)));
        BOOST_CHECK(!rawDb.Exists(
            std::make_pair(std::string("pqkey"), witnessProgram)));
        CPQHDChain stored;
        BOOST_REQUIRE(rawDb.Read(std::string("pqhdchain"), stored));
        BOOST_CHECK_EQUAL(stored.nExternalChainCounter, 1U);
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK(wallet->IsCrypted());
        BOOST_CHECK(wallet->IsLocked());
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 1U);
        CPQKey loaded;
        BOOST_CHECK(!wallet->GetPQKey(generated.GetWitnessProgram(), loaded));
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        BOOST_REQUIRE(wallet->GetPQKey(generated.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(generated));

        CPQPubKey second;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(second, &index));
        BOOST_CHECK_EQUAL(index, 1U);
        BOOST_CHECK(second != generated);
    }
}

BOOST_AUTO_TEST_CASE(encrypted_pq_index_recovery_is_idempotent_and_ciphertext_only)
{
    const std::string filename = "pq-hd-encrypted-index-recovery-wallet.dat";
    const SecureString passphrase("pq-hd-index-recovery-passphrase");
    SecureVector seed;
    const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
    BOOST_REQUIRE(pqderivation::DeriveSeed(
        bip39Seed.data(), bip39Seed.size(), 1, 2, seed));
    CPQKey expected;
    BOOST_REQUIRE(expected.SetSeed(seed.data()));
    const uint256 recoveredProgram = expected.GetPubKey().GetWitnessProgram();

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        CKey classicalKey;
        classicalKey.MakeNewKey(true);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(
                classicalKey, classicalKey.GetPubKey()));
        }
        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));

        bool added = true;
        BOOST_CHECK(!wallet->RecoverPQKey(2, added));
        BOOST_CHECK(!added);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 0U);

        BOOST_REQUIRE(wallet->Unlock(passphrase));
        BOOST_REQUIRE(wallet->RecoverPQKey(2, added));
        BOOST_CHECK(added);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 3U);
        BOOST_CHECK(wallet->HavePQKey(recoveredProgram));

        BOOST_REQUIRE(wallet->RecoverPQKey(2, added));
        BOOST_CHECK(!added);
        BOOST_REQUIRE(wallet->RecoverPQKey(0, added));
        BOOST_CHECK(added);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 3U);

        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        BOOST_CHECK(rawDb.Exists(
            std::make_pair(std::string("cpqkey"), recoveredProgram)));
        BOOST_CHECK(!rawDb.Exists(
            std::make_pair(std::string("pqkey"), recoveredProgram)));
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 3U);
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        CPQKey loaded;
        BOOST_REQUIRE(wallet->GetPQKey(recoveredProgram, loaded));
        BOOST_CHECK(loaded.MatchesPubKey(expected.GetPubKey()));
        CPQPubKey next;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(next, &index));
        BOOST_CHECK_EQUAL(index, 3U);
    }
}

BOOST_AUTO_TEST_CASE(encrypted_legacy_hd_pq_derivation_recovers_and_advances)
{
    const std::string filename = "pq-hd-encrypted-legacy-wallet.dat";
    const SecureString passphrase("pq-hd-encrypted-legacy-passphrase");
    const std::vector<unsigned char> legacySeed = ParseHex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f");
    CPQPubKey beforeRotation;
    CPQPubKey firstAfterRotation;
    SecureVector rotatedSeed;

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeLegacyHDWallet(*wallet, legacySeed));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(beforeRotation, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        const uint256 originalLineage = wallet->GetPQHDChain().lineage_id;

        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        CKey currentHDSeed;
        BOOST_REQUIRE(wallet->GetKey(
            wallet->GetHDChain().seed_id, currentHDSeed));
        rotatedSeed.assign(currentHDSeed.begin(), currentHDSeed.end());
        BOOST_REQUIRE_EQUAL(rotatedSeed.size(), pqderivation::LEGACY_SEED_BYTES);

        BOOST_REQUIRE(wallet->GenerateNewPQKey(firstAfterRotation, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nSeedSource,
                          CPQHDChain::SEED_SOURCE_LEGACY_HD);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nCoinType, 1U);
        BOOST_CHECK(wallet->GetPQHDChain().lineage_id == PQLineageId(
            rotatedSeed, pqderivation::SEED_SOURCE_LEGACY_HD, 1));
        BOOST_CHECK(wallet->GetPQHDChain().lineage_id != originalLineage);

        SecureVector expectedSeed;
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            rotatedSeed.data(), rotatedSeed.size(), 1, 0, expectedSeed));
        CPQKey expectedKey;
        BOOST_REQUIRE(expectedKey.SetSeed(expectedSeed.data()));
        BOOST_CHECK(expectedKey.GetPubKey() == firstAfterRotation);

        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        BOOST_CHECK(rawDb.Exists(std::make_pair(
            std::string("cpqkey"), beforeRotation.GetWitnessProgram())));
        BOOST_CHECK(!rawDb.Exists(std::make_pair(
            std::string("pqkey"), beforeRotation.GetWitnessProgram())));
        BOOST_CHECK(rawDb.Exists(std::make_pair(
            std::string("cpqkey"), firstAfterRotation.GetWitnessProgram())));
        BOOST_CHECK(!rawDb.Exists(std::make_pair(
            std::string("pqkey"), firstAfterRotation.GetWitnessProgram())));
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK(wallet->IsLocked());
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 1U);
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        CPQPubKey second;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(second, &index));
        BOOST_CHECK_EQUAL(index, 1U);
        BOOST_CHECK(second != firstAfterRotation);

        SecureVector expectedSeed;
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            rotatedSeed.data(), rotatedSeed.size(), 1, 1, expectedSeed));
        CPQKey expectedKey;
        BOOST_REQUIRE(expectedKey.SetSeed(expectedSeed.data()));
        BOOST_CHECK(expectedKey.GetPubKey() == second);
    }
}

BOOST_AUTO_TEST_CASE(deterministic_pq_hd_lineage_change_resets_counter)
{
    const std::string filename = "pq-hd-lineage-reset-wallet.dat";
    CPQPubKey replacementIndex0;
    const std::vector<unsigned char> replacementPassphrase = {
        'r', 'e', 'p', 'l', 'a', 'c', 'e', 'm', 'e', 'n', 't'};
    const std::vector<unsigned char> replacementSeed = ParseHex(
        "d4338fb97a1582023d772880df61e318575000f71d50f687e9da8335891f282b"
        "300fa3c37afdeabe3d388377c71f0d748f97bb7007570c97c100442e129db174");
    const uint256 replacementLineage = PQLineageId(
        replacementSeed, pqderivation::SEED_SOURCE_BIP39, 1);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        CPQPubKey ignored;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(ignored, &index));
        BOOST_REQUIRE(wallet->GenerateNewPQKey(ignored, &index));
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 2U);

        CHDChain replacement(wallet.get());
        replacement.UseBip44(true);
        replacement.seed_id = CPubKey(
            replacementSeed.begin(), replacementSeed.end()).GetID();
        BOOST_REQUIRE(wallet->SetHDChain(replacement, false));
        BOOST_REQUIRE(wallet->LoadPassphrase(replacementPassphrase));
        BOOST_REQUIRE(wallet->LoadVchSeed(replacementSeed));
        CWalletDB walletdb(wallet->GetDBHandle());
        BOOST_REQUIRE(walletdb.WriteBip39Passphrase(
            replacementPassphrase, false));
        BOOST_REQUIRE(walletdb.WriteBip39VchSeed(replacementSeed, false));

        BOOST_REQUIRE(wallet->GenerateNewPQKey(replacementIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 1U);
        BOOST_CHECK(wallet->GetPQHDChain().lineage_id == replacementLineage);

        SecureVector expectedSeed;
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            replacementSeed.data(), replacementSeed.size(), 1, 0,
            expectedSeed));
        CPQKey expectedKey;
        BOOST_REQUIRE(expectedKey.SetSeed(expectedSeed.data()));
        BOOST_CHECK(expectedKey.GetPubKey() == replacementIndex0);
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 1U);
        BOOST_CHECK(wallet->GetPQHDChain().lineage_id == replacementLineage);
        CPQKey loaded;
        BOOST_REQUIRE(wallet->GetPQKey(
            replacementIndex0.GetWitnessProgram(), loaded));
        BOOST_CHECK(loaded.MatchesPubKey(replacementIndex0));
    }
}

BOOST_AUTO_TEST_CASE(deterministic_pq_coin_type_change_uses_own_branch)
{
    ChainParamsRestorer restoreParams;
    const std::string filename = "pq-hd-network-lineage-wallet.dat";
    CPQPubKey regtestIndex0;
    CPQPubKey regtestIndex1;
    CPQPubKey mainIndex0;

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(regtestIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_REQUIRE(wallet->GenerateNewPQKey(regtestIndex1, &index));
        BOOST_CHECK_EQUAL(index, 1U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nCoinType, 1U);

        SelectParams(CBaseChainParams::MAIN);
        BOOST_REQUIRE(wallet->GenerateNewPQKey(mainIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nCoinType, 175U);
        BOOST_CHECK(mainIndex0 != regtestIndex0);

        SecureVector expectedSeed;
        const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
        BOOST_REQUIRE(pqderivation::DeriveSeed(
            bip39Seed.data(), bip39Seed.size(), 175, 0, expectedSeed));
        CPQKey expectedKey;
        BOOST_REQUIRE(expectedKey.SetSeed(expectedSeed.data()));
        BOOST_CHECK(expectedKey.GetPubKey() == mainIndex0);
    }
    bitdb.Flush(false);

    SelectParams(CBaseChainParams::REGTEST);
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nCoinType, 175U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 1U);

        CPQPubKey regtestIndex2;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(regtestIndex2, &index));
        BOOST_CHECK_EQUAL(index, 2U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nCoinType, 1U);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 3U);
        BOOST_CHECK(regtestIndex2 != regtestIndex0);
        BOOST_CHECK(regtestIndex2 != regtestIndex1);
    }
}

BOOST_AUTO_TEST_CASE(deterministic_pq_counter_exhaustion_is_persistent)
{
    const std::string filename = "pq-hd-counter-exhaustion-wallet.dat";
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
        CPQHDChain nearExhaustion;
        nearExhaustion.SetLineage(
            CPQHDChain::SEED_SOURCE_BIP39, 1,
            PQLineageId(bip39Seed, pqderivation::SEED_SOURCE_BIP39, 1));
        nearExhaustion.nExternalChainCounter =
            pqderivation::HARDENED_LIMIT - 1;
        CWalletDB walletdb(wallet->GetDBHandle());
        BOOST_REQUIRE(walletdb.WritePQHDChain(nearExhaustion));
        BOOST_REQUIRE(wallet->LoadPQHDChain(nearExhaustion));

        CPQPubKey finalKey;
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(finalKey, &index));
        BOOST_CHECK_EQUAL(index, pqderivation::HARDENED_LIMIT - 1);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter,
                          pqderivation::HARDENED_LIMIT);

        CPQPubKey rejected;
        index = 99;
        BOOST_CHECK(!wallet->GenerateNewPQKey(rejected, &index));
        BOOST_CHECK(!rejected.IsValid());
        BOOST_CHECK_EQUAL(index, 99U);
    }
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter,
                          pqderivation::HARDENED_LIMIT);
        CPQPubKey rejected;
        uint32_t index = 99;
        BOOST_CHECK(!wallet->GenerateNewPQKey(rejected, &index));
        BOOST_CHECK_EQUAL(index, 99U);
    }
}

BOOST_AUTO_TEST_CASE(deterministic_pq_counter_and_key_commit_atomically)
{
    const std::string filename = "pq-hd-atomic-wallet.dat";
    std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
    BOOST_REQUIRE(InitializeBip39Wallet(*wallet));

    SecureVector expectedSeed;
    const std::vector<unsigned char> bip39Seed = Bip39TestSeed();
    BOOST_REQUIRE(pqderivation::DeriveSeed(
        bip39Seed.data(), bip39Seed.size(), 1, 0, expectedSeed));
    CPQKey expectedKey;
    BOOST_REQUIRE(expectedKey.SetSeed(expectedSeed.data()));
    const uint256 expectedProgram = expectedKey.GetPubKey().GetWitnessProgram();

    ScopedDBExpiredLockTimeout timeout(bitdb.dbenv, 100000);
    CWalletDB blocker(wallet->GetDBHandle());
    BOOST_REQUIRE(blocker.TxnBegin());
    CPQHDChain blockedChain;
    blockedChain.SetLineage(CPQHDChain::SEED_SOURCE_BIP39, 1,
                            PQLineageId(
                                bip39Seed,
                                pqderivation::SEED_SOURCE_BIP39, 1));
    blockedChain.nExternalChainCounter = 77;
    BOOST_REQUIRE(blocker.WritePQHDChain(blockedChain));

    std::atomic<bool> generationComplete{false};
    std::atomic<bool> detectorFailed{false};
    std::atomic<bool> timeoutObserved{false};
    std::thread detector([&] {
        for (int attempt = 0; attempt < 1000 && !generationComplete; ++attempt) {
            int rejected = 0;
            if (bitdb.dbenv->lock_detect(0, DB_LOCK_EXPIRE, &rejected) != 0) {
                detectorFailed = true;
                return;
            }
            if (rejected > 0) {
                timeoutObserved = true;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    CPQPubKey generated;
    uint32_t index = 99;
    const bool generatedSuccessfully = wallet->GenerateNewPQKey(generated, &index);
    generationComplete = true;
    detector.join();
    BOOST_REQUIRE(blocker.TxnAbort());

    BOOST_CHECK(!detectorFailed);
    BOOST_CHECK(timeoutObserved);
    BOOST_CHECK(!generatedSuccessfully);
    BOOST_CHECK(!generated.IsValid());
    BOOST_CHECK_EQUAL(index, 99U);
    BOOST_CHECK_EQUAL(wallet->GetPQHDChain().nExternalChainCounter, 0U);
    BOOST_CHECK(!wallet->HavePQKey(expectedProgram));

    CWalletDBWrapper rawDbw(&bitdb, filename);
    CDB rawDb(rawDbw, "r");
    BOOST_CHECK(!rawDb.Exists(std::string("pqhdchain")));
    BOOST_CHECK(!rawDb.Exists(
        std::make_pair(std::string("pqkey"), expectedProgram)));
    BOOST_CHECK(!rawDb.Exists(
        std::make_pair(std::string("cpqkey"), expectedProgram)));
}

BOOST_AUTO_TEST_CASE(bip39_records_are_key_critical)
{
    for (const std::string& type : {
             "hdchain", "pqhdchain",
             "bip39words", "bip39passphrase", "bip39vchseed",
             "cbip39words", "cbip39passphrase", "cbip39vchseed"}) {
        BOOST_CHECK_MESSAGE(CWalletDB::IsKeyType(type), type);
    }

    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> passphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const std::vector<unsigned char> cryptedSeed(BIP39_CRYPTED_SEED_SIZE, 0x5a);
    const uint256 wordHash = Hash(words.begin(), words.end());
    auto retainedByKeyOnlyRecovery = [](const std::string& type, const auto& value) {
        CWallet dummyWallet;
        CDataStream key(SER_DISK, CLIENT_VERSION);
        CDataStream serializedValue(SER_DISK, CLIENT_VERSION);
        key << type;
        serializedValue << value;
        return CWalletDB::RecoverKeysOnlyFilter(
            &dummyWallet, std::move(key), std::move(serializedValue));
    };
    BOOST_CHECK(retainedByKeyOnlyRecovery(
        "bip39words", std::make_pair(wordHash, words)));
    BOOST_CHECK(retainedByKeyOnlyRecovery("bip39passphrase", passphrase));
    BOOST_CHECK(retainedByKeyOnlyRecovery("bip39vchseed", seed));
    BOOST_CHECK(retainedByKeyOnlyRecovery(
        "cbip39words", std::make_pair(wordHash, std::vector<unsigned char>(96, 0x31))));
    BOOST_CHECK(retainedByKeyOnlyRecovery(
        "cbip39passphrase", std::vector<unsigned char>(32, 0x42)));
    BOOST_CHECK(retainedByKeyOnlyRecovery("cbip39vchseed", cryptedSeed));
}

BOOST_AUTO_TEST_CASE(salvage_rows_use_secure_storage)
{
    using CleansingBytes =
        std::vector<unsigned char, zero_after_free_allocator<unsigned char>>;
    BOOST_CHECK((std::is_same<CDBEnv::KeyValPair::first_type, CleansingBytes>::value));
    BOOST_CHECK((std::is_same<CDBEnv::KeyValPair::second_type, CleansingBytes>::value));
}

BOOST_AUTO_TEST_CASE(recovery_faults_preserve_original_database)
{
    using Fault = wallet_db::RecoveryTestAccess::Fault;
    struct FaultCase {
        const char* name;
        Fault fault;
        bool duplicateFirstRow;
        bool throwingCallback;
    };
    const FaultCase cases[] = {
        {"duplicate-put", Fault::NONE, true, false},
        {"callback-exception", Fault::NONE, false, true},
        {"null-write-transaction", Fault::NULL_WRITE_TRANSACTION, false, false},
        {"write-commit", Fault::WRITE_COMMIT, false, false},
        {"temporary-close", Fault::TEMP_CLOSE, false, false},
        {"second-rename", Fault::SECOND_RENAME, false, false},
        {"install-commit", Fault::INSTALL_COMMIT, false, false},
    };

    for (const FaultCase& faultCase : cases) {
        const std::string filename =
            strprintf("recovery-preserve-%s-wallet.dat", faultCase.name);
        const std::string tempFilename = filename + ".recover.test";
        const std::string requestedBackup = filename + ".backup.test";
        const std::string expectedValue =
            strprintf("original-value-%s", faultCase.name);
        WriteRecoveryFixture(filename, expectedValue);
        const std::string originalBytes = ReadFileBytes(GetDataDir() / filename);

        std::string publishedBackup = "must-be-cleared";
        const bool recovered = wallet_db::RecoveryTestAccess::Recover(
            filename,
            publishedBackup,
            faultCase.fault,
            faultCase.duplicateFirstRow,
            false,
            tempFilename,
            requestedBackup,
            nullptr,
            faultCase.throwingCallback ? ThrowingRecoveryFilter : nullptr);

        BOOST_CHECK_MESSAGE(!recovered, faultCase.name);
        BOOST_CHECK_MESSAGE(publishedBackup.empty(), faultCase.name);
        BOOST_REQUIRE_MESSAGE(fs::is_regular_file(GetDataDir() / filename),
                              faultCase.name);
        BOOST_CHECK_MESSAGE(ReadFileBytes(GetDataDir() / filename) == originalBytes,
                            faultCase.name);
        BOOST_CHECK_MESSAGE(!fs::exists(GetDataDir() / requestedBackup),
                            faultCase.name);
        BOOST_CHECK_MESSAGE(!fs::exists(GetDataDir() / tempFilename),
                            faultCase.name);

        std::string actualValue;
        BOOST_REQUIRE_MESSAGE(ReadRecoveryFixture(filename, actualValue),
                              faultCase.name);
        BOOST_CHECK_EQUAL(actualValue, expectedValue);
    }
}

BOOST_AUTO_TEST_CASE(recovery_exclusive_temp_open_failure_preserves_source)
{
    const std::string filename = "recovery-blocked-temp-wallet.dat";
    const std::string tempFilename = filename + ".recover.blocked";
    const std::string requestedBackup = filename + ".backup.test";
    const fs::path tempPath = GetDataDir() / tempFilename;
    WriteRecoveryFixture(filename, "original-blocked-temp-value");
    const std::string originalBytes = ReadFileBytes(GetDataDir() / filename);
    BOOST_REQUIRE(fs::create_directory(tempPath));

    std::string publishedBackup = "must-be-cleared";
    BOOST_CHECK(!wallet_db::RecoveryTestAccess::Recover(
        filename,
        publishedBackup,
        wallet_db::RecoveryTestAccess::Fault::NONE,
        false,
        false,
        tempFilename,
        requestedBackup));
    BOOST_CHECK(publishedBackup.empty());
    BOOST_REQUIRE(fs::is_regular_file(GetDataDir() / filename));
    BOOST_CHECK_EQUAL(ReadFileBytes(GetDataDir() / filename), originalBytes);
    BOOST_CHECK(!fs::exists(GetDataDir() / requestedBackup));
    BOOST_CHECK(fs::is_directory(tempPath));
    BOOST_REQUIRE(fs::remove(tempPath));
}

BOOST_AUTO_TEST_CASE(partial_recovery_installs_atomically_and_preserves_backup)
{
    const std::string filename = "partial-recovery-wallet.dat";
    const std::string tempFilename = filename + ".recover.test";
    const std::string requestedBackup = filename + ".backup.test";
    const std::string expectedValue = "partial-recovery-original-value";
    WriteRecoveryFixture(filename, expectedValue);
    const std::string originalBytes = ReadFileBytes(GetDataDir() / filename);

    std::string publishedBackup;
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::Recover(
        filename,
        publishedBackup,
        wallet_db::RecoveryTestAccess::Fault::NONE,
        false,
        true,
        tempFilename,
        requestedBackup));
    BOOST_CHECK_EQUAL(publishedBackup, requestedBackup);
    BOOST_REQUIRE(fs::is_regular_file(GetDataDir() / filename));
    BOOST_REQUIRE(fs::is_regular_file(GetDataDir() / requestedBackup));
    BOOST_CHECK_EQUAL(ReadFileBytes(GetDataDir() / requestedBackup), originalBytes);
    BOOST_CHECK(!fs::exists(GetDataDir() / tempFilename));

    std::string actualValue;
    BOOST_REQUIRE(ReadRecoveryFixture(filename, actualValue));
    BOOST_CHECK_EQUAL(actualValue, expectedValue);
}

BOOST_AUTO_TEST_CASE(recovery_handles_zero_length_raw_rows)
{
    const std::string filename = "zero-length-recovery-wallet.dat";
    const std::string tempFilename = filename + ".recover.test";
    const std::string requestedBackup = filename + ".backup.test";
    const std::vector<unsigned char> empty;
    const std::vector<unsigned char> nonemptyKey{0x42};
    const std::vector<unsigned char> nonemptyValue{0x51};

    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::WriteRaw(
        filename, empty, nonemptyValue));
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::WriteRaw(
        filename, nonemptyKey, empty));

    std::string publishedBackup;
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::Recover(
        filename,
        publishedBackup,
        wallet_db::RecoveryTestAccess::Fault::NONE,
        false,
        false,
        tempFilename,
        requestedBackup));
    BOOST_CHECK_EQUAL(publishedBackup, requestedBackup);
    BOOST_CHECK(wallet_db::RecoveryTestAccess::HasRaw(
        filename, empty, nonemptyValue.size()));
    BOOST_CHECK(wallet_db::RecoveryTestAccess::HasRaw(
        filename, nonemptyKey, empty.size()));
}

BOOST_AUTO_TEST_CASE(recovery_handles_dump_larger_than_locked_pool_limit)
{
    const std::string filename = "large-recovery-wallet.dat";
    const std::vector<unsigned char> key{0x42};
    const std::vector<unsigned char> value(300000, 0x5a);
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::WriteRaw(filename, key, value));

    std::string backupFilename;
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::Recover(filename, backupFilename));
    BOOST_CHECK(!backupFilename.empty());
    BOOST_CHECK(wallet_db::RecoveryTestAccess::HasRaw(filename, key, value.size()));
}

BOOST_AUTO_TEST_CASE(bip44_key_only_recovery_preserves_derivation_lineage)
{
    const std::string filename = "bip44-key-only-recovery-wallet.dat";
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> passphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const uint256 wordHash = Hash(words.begin(), words.end());
    CPQPubKey recoveredPQIndex0;

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(InitializeBip39Wallet(*wallet));
        uint32_t index = 99;
        BOOST_REQUIRE(wallet->GenerateNewPQKey(recoveredPQIndex0, &index));
        BOOST_CHECK_EQUAL(index, 0U);
    }
    bitdb.Flush(false);

    CWallet dummyWallet;
    std::string backupFilename;
    BOOST_REQUIRE(CWalletDB::Recover(
        filename, &dummyWallet, CWalletDB::RecoverKeysOnlyFilter, backupFilename));
    bitdb.Flush(false);

    std::unique_ptr<CWalletDBWrapper> recoveredDbw(new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> recovered(new CWallet(std::move(recoveredDbw)));
    bool firstRun = true;
    BOOST_REQUIRE_EQUAL(recovered->LoadWallet(firstRun), DB_LOAD_OK);
    BOOST_CHECK(!firstRun);
    uint256 recoveredHash;
    std::vector<unsigned char> recoveredWords;
    std::vector<unsigned char> recoveredPassphrase;
    std::vector<unsigned char> recoveredSeed;
    recovered->GetBip39Data(
        recoveredHash, recoveredWords, recoveredPassphrase, recoveredSeed);
    BOOST_CHECK(recoveredHash == wordHash);
    BOOST_CHECK(recoveredWords == words);
    BOOST_CHECK(recoveredPassphrase == passphrase);
    BOOST_CHECK(recoveredSeed == seed);
    BOOST_CHECK_EQUAL(recovered->GetPQHDChain().nExternalChainCounter, 1U);
    CPQKey recoveredPQKey;
    BOOST_REQUIRE(recovered->GetPQKey(
        recoveredPQIndex0.GetWitnessProgram(), recoveredPQKey));
    BOOST_CHECK(recoveredPQKey.MatchesPubKey(recoveredPQIndex0));

    CPQPubKey recoveredPQIndex1;
    uint32_t pqIndex = 99;
    BOOST_REQUIRE(recovered->GenerateNewPQKey(recoveredPQIndex1, &pqIndex));
    BOOST_CHECK_EQUAL(pqIndex, 1U);
    SecureVector expectedPQSeed;
    BOOST_REQUIRE(pqderivation::DeriveSeed(
        seed.data(), seed.size(), 1, 1, expectedPQSeed));
    CPQKey expectedPQKey;
    BOOST_REQUIRE(expectedPQKey.SetSeed(expectedPQSeed.data()));
    BOOST_CHECK(expectedPQKey.GetPubKey() == recoveredPQIndex1);

    // Independent expected value for regtest path m/44'/1'/0'/0/0.
    const std::vector<unsigned char> expectedBytes = ParseHex(
        "023765b56ecb006a47d775beee38c45a9fe5dbe11d100b2e2ea3c99196dc915a2d");
    const CPubKey expectedFirstExternal(expectedBytes.begin(), expectedBytes.end());
    BOOST_REQUIRE(expectedFirstExternal.IsValid());

    BOOST_REQUIRE(recovered->TopUpKeyPool(1));
    CPubKey recoveredFirstExternal;
    BOOST_REQUIRE(recovered->GetKeyFromPool(recoveredFirstExternal, false));
    BOOST_CHECK(recoveredFirstExternal == expectedFirstExternal);
}

BOOST_AUTO_TEST_CASE(first_run_detection_covers_hd_bip39_master_and_pq_state)
{
    {
        CWallet wallet;
        BOOST_CHECK(wallet.IsFirstRun());
    }
    {
        CWallet wallet;
        BOOST_REQUIRE(wallet.SetHDChain(Bip44TestChain(&wallet), true));
        BOOST_CHECK(!wallet.IsFirstRun());
    }
    {
        CWallet wallet;
        BOOST_REQUIRE(wallet.LoadVchSeed(Bip39TestSeed()));
        BOOST_CHECK(!wallet.IsFirstRun());
    }
    {
        const std::string filename = "first-run-plaintext-pq-wallet.dat";
        CPQKey key;
        key.MakeNewKey();
        BOOST_REQUIRE(key.IsValid());
        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, key.GetPubKey()));
        }
        bitdb.Flush(false);
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = true;
        BOOST_REQUIRE_EQUAL(wallet.LoadWallet(firstRun), DB_LOAD_OK);
        BOOST_CHECK(!firstRun);
    }
    {
        const std::string filename = "first-run-encrypted-pq-wallet.dat";
        const SecureString passphrase("first-run-encrypted-pq-passphrase");
        CPQKey key;
        key.MakeNewKey();
        BOOST_REQUIRE(key.IsValid());
        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            {
                LOCK(wallet->cs_wallet);
                BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, key.GetPubKey()));
            }
            BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
        }
        bitdb.Flush(false);
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = true;
        BOOST_REQUIRE_EQUAL(wallet.LoadWallet(firstRun), DB_LOAD_OK);
        BOOST_CHECK(wallet.IsCrypted());
        BOOST_CHECK(!firstRun);
    }
    {
        const std::string filename = "first-run-master-only-wallet.dat";
        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            CWalletDB walletdb(wallet->GetDBHandle());
            BOOST_REQUIRE(walletdb.WriteMasterKey(1U, CMasterKey()));
        }
        bitdb.Flush(false);
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = true;
        BOOST_REQUIRE_EQUAL(wallet.LoadWallet(firstRun), DB_LOAD_OK);
        BOOST_CHECK(!firstRun);
    }
    {
        CWallet wallet;
        BOOST_REQUIRE(wallet.LoadCryptedVchSeed(
            std::vector<unsigned char>(BIP39_CRYPTED_SEED_SIZE, 0x63)));
        BOOST_CHECK(!wallet.IsFirstRun());
    }
}

BOOST_AUTO_TEST_CASE(encrypted_bip44_key_only_recovery_preserves_derivation_lineage)
{
    const std::string filename = "bip44-encrypted-key-only-recovery-wallet.dat";
    const SecureString walletPassphrase("bip44-recovery-wallet-passphrase");
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> passphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const uint256 wordHash = Hash(words.begin(), words.end());

    CKey persistedKey;
    persistedKey.MakeNewKey(true);
    const CPubKey persistedPubKey = persistedKey.GetPubKey();
    BOOST_REQUIRE(persistedPubKey.IsValid());

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
        BOOST_REQUIRE(wallet->LoadWords(wordHash, words));
        BOOST_REQUIRE(wallet->LoadPassphrase(passphrase));
        BOOST_REQUIRE(wallet->LoadVchSeed(seed));
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(persistedKey, persistedPubKey));
        }
        {
            CWalletDB walletdb(wallet->GetDBHandle());
            BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
            BOOST_REQUIRE(walletdb.WriteBip39Passphrase(passphrase, false));
            BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
        }
        BOOST_REQUIRE(wallet->EncryptWallet(walletPassphrase));
    }
    bitdb.Flush(false);

    CWallet dummyWallet;
    std::string backupFilename;
    BOOST_REQUIRE(CWalletDB::Recover(
        filename, &dummyWallet, CWalletDB::RecoverKeysOnlyFilter, backupFilename));
    bitdb.Flush(false);

    std::unique_ptr<CWallet> recovered = LoadPQWallet(filename);
    BOOST_CHECK(recovered->IsCrypted());
    BOOST_CHECK(recovered->IsLocked());
    BOOST_REQUIRE(recovered->Unlock(walletPassphrase));

    CKey loadedKey;
    BOOST_REQUIRE(recovered->GetKey(persistedPubKey.GetID(), loadedKey));
    BOOST_CHECK(loadedKey.VerifyPubKey(persistedPubKey));

    const std::vector<unsigned char> expectedBytes = ParseHex(
        "023765b56ecb006a47d775beee38c45a9fe5dbe11d100b2e2ea3c99196dc915a2d");
    const CPubKey expectedFirstExternal(expectedBytes.begin(), expectedBytes.end());
    BOOST_REQUIRE(expectedFirstExternal.IsValid());
    BOOST_REQUIRE(recovered->TopUpKeyPool(1));
    CPubKey recoveredFirstExternal;
    BOOST_REQUIRE(recovered->GetKeyFromPool(recoveredFirstExternal, false));
    BOOST_CHECK(recoveredFirstExternal == expectedFirstExternal);
}

BOOST_AUTO_TEST_CASE(bip44_encryption_and_backup_are_ciphertext_only)
{
    const SecureString walletPassphrase("bip44-ciphertext-only-passphrase");
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> fullPassphrase = Bip39TestPassphrase();
    const uint256 wordHash = Hash(words.begin(), words.end());

    for (const bool withMnemonicPassphrase : {false, true}) {
        const std::string suffix = withMnemonicPassphrase ? "with-passphrase" : "without-passphrase";
        const std::string filename = "bip44-ciphertext-only-" + suffix + ".dat";
        const std::string backupFilename = "bip44-ciphertext-only-" + suffix + "-backup.dat";
        const std::vector<unsigned char> mnemonicPassphrase =
            withMnemonicPassphrase ? fullPassphrase : std::vector<unsigned char>();
        // Independent PBKDF2-HMAC-SHA512 BIP39 vector for the empty
        // passphrase; the TREZOR seed belongs only to the other case.
        const std::vector<unsigned char> seed = withMnemonicPassphrase
            ? Bip39TestSeed()
            : ParseHex("5eb00bbddcf069084889a8ab9155568165f5c453ccb85e70811aaed6f6da5fc19"
                       "a5ac40b389cd370d086206dec8aa6c43daea6690f20ad3d8d48b2d2ce9e38e4");

        CKey persistedKey;
        persistedKey.MakeNewKey(true);
        const CPubKey persistedPubKey = persistedKey.GetPubKey();
        BOOST_REQUIRE(persistedPubKey.IsValid());

        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
            BOOST_REQUIRE(wallet->LoadWords(wordHash, words));
            BOOST_REQUIRE(wallet->LoadPassphrase(mnemonicPassphrase));
            BOOST_REQUIRE(wallet->LoadVchSeed(seed));
            {
                LOCK(wallet->cs_wallet);
                BOOST_REQUIRE(wallet->AddKeyPubKey(persistedKey, persistedPubKey));
            }
            {
                CWalletDB walletdb(wallet->GetDBHandle());
                BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
                if (withMnemonicPassphrase)
                    BOOST_REQUIRE(walletdb.WriteBip39Passphrase(mnemonicPassphrase, false));
                BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
            }

            BOOST_REQUIRE(wallet->EncryptWallet(walletPassphrase));
            {
                CWalletDB walletdb(wallet->GetDBHandle(), "r");
                bool hasPlaintextBip39 = true;
                BOOST_REQUIRE(walletdb.HasPlaintextBip39(hasPlaintextBip39));
                BOOST_CHECK(!hasPlaintextBip39);
                std::vector<unsigned char> cryptedSeed;
                BOOST_CHECK(walletdb.ReadBip39VchSeed(cryptedSeed, true));
                BOOST_CHECK(!walletdb.ReadBip39VchSeed(cryptedSeed, false));
                std::vector<unsigned char> cryptedPassphrase;
                BOOST_CHECK_EQUAL(
                    walletdb.ReadBip39Passphrase(cryptedPassphrase, true),
                    withMnemonicPassphrase);
                BOOST_CHECK(!walletdb.ReadBip39Passphrase(cryptedPassphrase, false));
            }
            BOOST_REQUIRE(wallet->BackupWallet((GetDataDir() / backupFilename).string()));
        }
        bitdb.Flush(false);

        std::unique_ptr<CWallet> recovered = LoadPQWallet(backupFilename);
        BOOST_CHECK(recovered->IsCrypted());
        BOOST_CHECK(recovered->IsLocked());
        BOOST_REQUIRE(recovered->Unlock(walletPassphrase));
        uint256 recoveredHash;
        std::vector<unsigned char> recoveredWords;
        std::vector<unsigned char> recoveredPassphrase;
        std::vector<unsigned char> recoveredSeed;
        recovered->GetBip39Data(
            recoveredHash, recoveredWords, recoveredPassphrase, recoveredSeed);
        BOOST_CHECK(recoveredHash == wordHash);
        BOOST_CHECK(recoveredWords == words);
        BOOST_CHECK(recoveredPassphrase == mnemonicPassphrase);
        BOOST_CHECK(recoveredSeed == seed);
        recovered.reset();
        bitdb.Flush(false);
    }
}

BOOST_AUTO_TEST_CASE(mixed_plaintext_and_ciphertext_bip39_records_fail_load_and_backup)
{
    const SecureString walletPassphrase("bip44-mixed-record-passphrase");
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> mnemonicPassphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const uint256 wordHash = Hash(words.begin(), words.end());

    for (int record = 0; record < 3; ++record) {
        const std::string filename = strprintf("bip44-mixed-record-%d.dat", record);
        const std::string backupFilename = strprintf("bip44-mixed-record-%d-backup.dat", record);
        CKey persistedKey;
        persistedKey.MakeNewKey(true);

        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
            BOOST_REQUIRE(wallet->LoadWords(wordHash, words));
            BOOST_REQUIRE(wallet->LoadPassphrase(mnemonicPassphrase));
            BOOST_REQUIRE(wallet->LoadVchSeed(seed));
            {
                LOCK(wallet->cs_wallet);
                BOOST_REQUIRE(wallet->AddKeyPubKey(persistedKey, persistedKey.GetPubKey()));
            }
            {
                CWalletDB walletdb(wallet->GetDBHandle());
                BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
                BOOST_REQUIRE(walletdb.WriteBip39Passphrase(mnemonicPassphrase, false));
                BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
            }
            BOOST_REQUIRE(wallet->EncryptWallet(walletPassphrase));

            {
                CWalletDB walletdb(wallet->GetDBHandle());
                if (record == 0)
                    BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
                else if (record == 1)
                    BOOST_REQUIRE(walletdb.WriteBip39Passphrase(mnemonicPassphrase, false));
                else
                    BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));

                bool hasPlaintextBip39 = false;
                BOOST_REQUIRE(walletdb.HasPlaintextBip39(hasPlaintextBip39));
                BOOST_CHECK(hasPlaintextBip39);
            }
            BOOST_CHECK(!wallet->BackupWallet((GetDataDir() / backupFilename).string()));
            BOOST_CHECK(!fs::exists(GetDataDir() / backupFilename));
        }
        bitdb.Flush(false);

        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_CORRUPT);
        bitdb.Flush(false);
    }
}

BOOST_AUTO_TEST_CASE(bip39_plaintext_erase_errors_abort_encryption)
{
    const SecureString walletPassphrase("bip39-erase-failure-passphrase");
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> mnemonicPassphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const uint256 wordHash = Hash(words.begin(), words.end());

    for (int target = 0; target < 3; ++target) {
        const std::string filename = strprintf("bip39-erase-failure-%d.dat", target);
        const std::string targetType = target == 0 ? "bip39words" :
                                       target == 1 ? "bip39passphrase" :
                                                     "bip39vchseed";
        CKey persistedKey;
        persistedKey.MakeNewKey(true);
        const CPubKey persistedPubKey = persistedKey.GetPubKey();
        BOOST_REQUIRE(persistedPubKey.IsValid());

        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
            BOOST_REQUIRE(wallet->LoadWords(wordHash, words));
            BOOST_REQUIRE(wallet->LoadPassphrase(mnemonicPassphrase));
            BOOST_REQUIRE(wallet->LoadVchSeed(seed));
            {
                LOCK(wallet->cs_wallet);
                BOOST_REQUIRE(wallet->AddKeyPubKey(persistedKey, persistedPubKey));
            }
            {
                CWalletDB walletdb(wallet->GetDBHandle());
                // Earlier erases are deliberately DB_NOTFOUND in the later
                // cases, so the failing operation is unambiguous.
                if (target == 0)
                    BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
                if (target <= 1)
                    BOOST_REQUIRE(walletdb.WriteBip39Passphrase(mnemonicPassphrase, false));
                BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
            }
            {
                // Berkeley DB orders serialized string keys by their encoded
                // length first. Fill leaves on both sides of the target length
                // so mkey/ckey writes do not collide with the blocked page.
                CDB filler(wallet->GetDBHandle(), "r+");
                BOOST_REQUIRE(filler.TxnBegin());
                const std::vector<unsigned char> padding(256, 0x39);
                for (int i = 0; i < 512; ++i) {
                    const std::string suffix = strprintf("%04d", i);
                    std::string lower(targetType.size(), 'a');
                    std::string upper(targetType.size(), 'z');
                    lower.replace(lower.size() - suffix.size(), suffix.size(), suffix);
                    upper.replace(upper.size() - suffix.size(), suffix.size(), suffix);
                    BOOST_REQUIRE(filler.Write(lower, padding));
                    BOOST_REQUIRE(filler.Write(upper, padding));
                }
                BOOST_REQUIRE(filler.TxnCommit());
            }

            ScopedDBExpiredLockTimeout timeout(bitdb.dbenv, 100000);
            std::atomic<bool> blockerReady{false};
            std::atomic<bool> releaseBlocker{false};
            std::atomic<bool> blockerWriteSucceeded{false};
            std::atomic<bool> blockerAbortSucceeded{false};
            std::thread blockerThread([&] {
                try {
                    CWalletDB blocker(wallet->GetDBHandle());
                    if (blocker.TxnBegin()) {
                        bool wrote = false;
                        if (target == 0)
                            wrote = blocker.WriteBip39Words(
                                wordHash, std::vector<unsigned char>(words.size(), 0x71), false);
                        else if (target == 1)
                            wrote = blocker.WriteBip39Passphrase(
                                std::vector<unsigned char>(mnemonicPassphrase.size(), 0x72), false);
                        else
                            wrote = blocker.WriteBip39VchSeed(
                                std::vector<unsigned char>(seed.size(), 0x73), false);
                        blockerWriteSucceeded = wrote;
                        blockerReady = true;
                        for (int i = 0; i < 2000 && !releaseBlocker; ++i)
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        blockerAbortSucceeded = blocker.TxnAbort();
                        return;
                    }
                } catch (...) {
                }
                blockerReady = true;
            });

            for (int i = 0; i < 1000 && !blockerReady; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (!blockerReady || !blockerWriteSucceeded) {
                releaseBlocker = true;
                blockerThread.join();
                BOOST_FAIL("failed to establish Berkeley DB record blocker");
            }

            std::atomic<bool> timeoutObserved{false};
            std::atomic<bool> detectorFailed{false};
            std::thread detectorThread([&] {
                for (int i = 0; i < 1000; ++i) {
                    int rejected = 0;
                    if (bitdb.dbenv->lock_detect(0, DB_LOCK_EXPIRE, &rejected) != 0) {
                        detectorFailed = true;
                        break;
                    }
                    if (rejected > 0) {
                        timeoutObserved = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                releaseBlocker = true;
            });

            const bool encrypted = wallet->EncryptWallet(walletPassphrase);
            detectorThread.join();
            releaseBlocker = true;
            blockerThread.join();

            BOOST_CHECK(!detectorFailed);
            BOOST_CHECK(timeoutObserved);
            BOOST_CHECK(blockerAbortSucceeded);
            BOOST_CHECK(!encrypted);
            BOOST_CHECK(wallet->IsCrypted());
            BOOST_CHECK(wallet->IsLocked());
        }
        bitdb.Flush(false);

        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        BOOST_CHECK(rawDb.Exists(targetType));
        BOOST_CHECK(!rawDb.Exists(std::make_pair(std::string("mkey"), 1U)));
        BOOST_CHECK(!rawDb.Exists(std::make_pair(std::string("ckey"), persistedPubKey)));
        BOOST_CHECK(!rawDb.Exists(std::string("cbip39words")));
        BOOST_CHECK(!rawDb.Exists(std::string("cbip39passphrase")));
        BOOST_CHECK(!rawDb.Exists(std::string("cbip39vchseed")));
    }
}

BOOST_AUTO_TEST_CASE(bip44_incomplete_or_malformed_seed_fails_load)
{
    const std::vector<unsigned char> words = Bip39TestWords();
    const uint256 wordHash = Hash(words.begin(), words.end());
    const std::vector<unsigned char> validSeed = Bip39TestSeed();

    auto expectCorrupt = [&](const std::string& suffix, bool writeWords,
                             bool writeSeed, const std::vector<unsigned char>& seed) {
        const std::string filename = "bip44-incomplete-" + suffix + ".dat";
        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
            CWalletDB walletdb(wallet->GetDBHandle());
            if (writeWords)
                BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
            if (writeSeed)
                BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
        }
        bitdb.Flush(false);

        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet->LoadWallet(firstRun), DB_CORRUPT);
        wallet.reset();
        bitdb.Flush(false);
    };

    expectCorrupt("no-bip39", false, false, {});
    expectCorrupt("words-only", true, false, {});
    expectCorrupt("seed-only", false, true, validSeed);
    expectCorrupt("empty-seed", true, true, {});
    expectCorrupt("short-seed", true, true, std::vector<unsigned char>(63, 0x11));
    expectCorrupt("long-seed", true, true, std::vector<unsigned char>(65, 0x22));
}

BOOST_AUTO_TEST_CASE(bip44_plaintext_lineage_requires_semantic_integrity)
{
    const std::vector<unsigned char> validWords = Bip39TestWords();
    const std::vector<unsigned char> validPassphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> validSeed = Bip39TestSeed();
    const uint256 validHash = Hash(validWords.begin(), validWords.end());

    for (int fault = 0; fault < 5; ++fault) {
        const std::string filename = strprintf("bip44-lineage-integrity-%d.dat", fault);
        std::vector<unsigned char> words = validWords;
        std::vector<unsigned char> passphrase = validPassphrase;
        std::vector<unsigned char> seed = validSeed;
        uint256 wordHash = validHash;
        if (fault == 1)
            wordHash.begin()[0] ^= 1;
        else if (fault == 2) {
            const std::string invalidMnemonic =
                "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon";
            words.assign(invalidMnemonic.begin(), invalidMnemonic.end());
            wordHash = Hash(words.begin(), words.end());
            const SecureString invalidWords(words.begin(), words.end());
            const SecureString securePassphrase(passphrase.begin(), passphrase.end());
            SecureVector invalidSeed;
            BOOST_REQUIRE(CMnemonic::ToSeed(invalidWords, securePassphrase, invalidSeed));
            seed.assign(invalidSeed.begin(), invalidSeed.end());
        } else if (fault == 3)
            passphrase.push_back('!');
        else if (fault == 4)
            seed[0] ^= 1;

        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
            BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
            CWalletDB walletdb(wallet->GetDBHandle());
            BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
            BOOST_REQUIRE(walletdb.WriteBip39Passphrase(passphrase, false));
            BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
        }
        bitdb.Flush(false);

        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        const DBErrors loadResult = wallet.LoadWallet(firstRun);
        if (fault == 0) {
            BOOST_REQUIRE_EQUAL(loadResult, DB_LOAD_OK);
            const std::vector<unsigned char> expectedBytes = ParseHex(
                "023765b56ecb006a47d775beee38c45a9fe5dbe11d100b2e2ea3c99196dc915a2d");
            const CPubKey expectedFirstExternal(expectedBytes.begin(), expectedBytes.end());
            BOOST_REQUIRE(expectedFirstExternal.IsValid());
            BOOST_REQUIRE(wallet.TopUpKeyPool(1));
            CPubKey firstExternal;
            BOOST_REQUIRE(wallet.GetKeyFromPool(firstExternal, false));
            BOOST_CHECK(firstExternal == expectedFirstExternal);
        } else {
            BOOST_CHECK_EQUAL(loadResult, DB_CORRUPT);
            BOOST_CHECK(!wallet.HasValidBip39Seed());
            BOOST_CHECK_EQUAL(wallet.GetHDChain().nExternalChainCounter, 0U);
            BOOST_CHECK_EQUAL(wallet.GetHDChain().nInternalChainCounter, 0U);
            BOOST_CHECK(wallet.GetKeys().empty());
        }
        bitdb.Flush(false);
    }
}

BOOST_AUTO_TEST_CASE(bip44_derivation_refuses_missing_seed_without_advancing_counter)
{
    const std::string filename = "bip44-missing-seed-derivation-wallet.dat";
    std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
    BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
    BOOST_CHECK_EQUAL(wallet->GetHDChain().nExternalChainCounter, 0U);
    BOOST_CHECK_EQUAL(wallet->GetHDChain().nInternalChainCounter, 0U);

    BOOST_CHECK_THROW(wallet->TopUpKeyPool(1), std::runtime_error);
    BOOST_CHECK_EQUAL(wallet->GetHDChain().nExternalChainCounter, 0U);
    BOOST_CHECK_EQUAL(wallet->GetHDChain().nInternalChainCounter, 0U);
    BOOST_CHECK(wallet->GetKeys().empty());

    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    BOOST_REQUIRE(wallet->LoadWords(Hash(words.begin(), words.end()), words));
    BOOST_REQUIRE(wallet->LoadVchSeed(seed));
    BOOST_REQUIRE(wallet->TopUpKeyPool(1));
    CWalletDB walletdb(wallet->GetDBHandle());
    CKeyPool firstPoolEntry;
    CKeyPool internalPoolEntry;
    CKeyPool skippedPoolEntry;
    BOOST_CHECK(walletdb.ReadPool(1, firstPoolEntry));
    BOOST_CHECK(walletdb.ReadPool(2, internalPoolEntry));
    BOOST_CHECK(!walletdb.ReadPool(3, skippedPoolEntry));
}

BOOST_AUTO_TEST_CASE(crypted_ecdsa_write_reports_plaintext_erase_failure)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    // A dummy database accepts writes but cannot erase. The encrypted write
    // must not report success when plaintext deletion fails.
    CWalletDBWrapper dummyDbw;
    CWalletDB dummyDb(dummyDbw);
    BOOST_CHECK(!dummyDb.WriteCryptedKey(
        pubkey, std::vector<unsigned char>(48, 0x4d), CKeyMetadata()));
}

BOOST_AUTO_TEST_CASE(crypted_ecdsa_write_failure_rolls_back_wallet_memory)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    const CKeyID keyID = pubkey.GetID();
    CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x3a);

    InspectableCryptoKeyStore source;
    BOOST_REQUIRE(source.AddKeyPubKey(key, pubkey));
    BOOST_REQUIRE(source.EncryptForTest(masterKey));
    std::vector<unsigned char> oldCiphertext;
    BOOST_REQUIRE(source.GetCryptedKeyForTest(keyID, oldCiphertext));

    const SecureString passphrase("ecdsa-rollback-passphrase");
    CMasterKey encryptedMasterKey;
    encryptedMasterKey.vchSalt.assign(WALLET_CRYPTO_SALT_SIZE, 0x7c);
    encryptedMasterKey.nDeriveIterations = 25000;
    CCrypter crypter;
    BOOST_REQUIRE(crypter.SetKeyFromPassphrase(
        passphrase, encryptedMasterKey.vchSalt, encryptedMasterKey.nDeriveIterations,
        encryptedMasterKey.nDerivationMethod));
    BOOST_REQUIRE(crypter.Encrypt(masterKey, encryptedMasterKey.vchCryptedKey));

    CWallet wallet;
    wallet.mapMasterKeys[1] = encryptedMasterKey;
    BOOST_REQUIRE(wallet.LoadCryptedKey(pubkey, oldCiphertext));
    BOOST_CHECK(!wallet.AddCryptedKey(pubkey, std::vector<unsigned char>(48, 0x22)));
    BOOST_REQUIRE(wallet.Unlock(passphrase));
    CKey restored;
    BOOST_REQUIRE(wallet.GetKey(keyID, restored));
    BOOST_CHECK(restored.VerifyPubKey(pubkey));

    CWallet firstFailedAdd;
    BOOST_CHECK(!firstFailedAdd.AddCryptedKey(pubkey, oldCiphertext));
    BOOST_CHECK(!firstFailedAdd.IsCrypted());
}

BOOST_AUTO_TEST_CASE(crypted_pq_write_reports_plaintext_erase_failure)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();

    // A dummy database accepts writes but cannot erase. This isolates the
    // second half of the cpqkey-write/pqkey-erase operation.
    CWalletDBWrapper dummyDbw;
    CWalletDB dummyDb(dummyDbw);
    BOOST_CHECK(!dummyDb.WriteCryptedPQKey(
        pubkey.GetWitnessProgram(), pubkey, std::vector<unsigned char>(32, 0x5a)));
}

BOOST_AUTO_TEST_CASE(crypted_pq_write_failure_rolls_back_wallet_memory)
{
    CPQKey existingKey;
    CPQKey rejectedKey;
    existingKey.MakeNewKey();
    rejectedKey.MakeNewKey();
    BOOST_REQUIRE(existingKey.IsValid());
    BOOST_REQUIRE(rejectedKey.IsValid());
    const CPQPubKey existingPubKey = existingKey.GetPubKey();
    const CPQPubKey rejectedPubKey = rejectedKey.GetPubKey();

    // Seed encrypted mode without persistence, then exercise a normal write
    // through a dummy DB whose plaintext erase deterministically fails.
    CWallet wallet;
    BOOST_REQUIRE(wallet.LoadCryptedPQKey(existingPubKey, std::vector<unsigned char>(32, 0x11)));
    BOOST_CHECK(!wallet.AddCryptedPQKey(rejectedPubKey, std::vector<unsigned char>(32, 0x22)));
    BOOST_CHECK(wallet.HavePQKey(existingPubKey.GetWitnessProgram()));
    BOOST_CHECK(!wallet.HavePQKey(rejectedPubKey.GetWitnessProgram()));
}

BOOST_AUTO_TEST_CASE(resident_plaintext_pq_key_blocks_crypted_mode)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();

    CCryptoKeyStore keystore;
    BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));
    BOOST_CHECK(!keystore.AddCryptedPQKey(pubkey, std::vector<unsigned char>(32, 0xa5)));
    BOOST_CHECK(!keystore.IsCrypted());
    BOOST_CHECK(keystore.HavePQKey(pubkey.GetWitnessProgram()));
}

BOOST_AUTO_TEST_CASE(wallet_lock_releases_transient_hd_chain_secrets)
{
    CWallet wallet;
    CHDChain chain(&wallet);
    CKey marker;
    marker.MakeNewKey(true);
    chain.UseBip44(true);
    chain.seed_id = marker.GetPubKey().GetID();
    chain.vchMnemonic.assign(96, 0x41);
    chain.vchMnemonicPassphrase.assign(24, 0x42);
    chain.vchSeed.assign(BIP39_SEED_SIZE, 0x43);
    BOOST_REQUIRE(wallet.SetHDChain(chain, true));

    CKey key;
    key.MakeNewKey(true);
    BOOST_REQUIRE(wallet.LoadCryptedKey(
        key.GetPubKey(), std::vector<unsigned char>(48, 0x44)));

    bool notifiedLockedState = false;
    const auto statusConnection = wallet.NotifyStatusChanged.connect(
        [&wallet, &notifiedLockedState](CCryptoKeyStore*) {
            notifiedLockedState = true;
            const CHDChain& observedChain = wallet.GetHDChain();
            BOOST_CHECK(observedChain.vchMnemonic.empty());
            BOOST_CHECK_EQUAL(observedChain.vchMnemonic.capacity(), 0U);
            BOOST_CHECK(observedChain.vchMnemonicPassphrase.empty());
            BOOST_CHECK_EQUAL(observedChain.vchMnemonicPassphrase.capacity(), 0U);
            BOOST_CHECK(observedChain.vchSeed.empty());
            BOOST_CHECK_EQUAL(observedChain.vchSeed.capacity(), 0U);
        });
    BOOST_REQUIRE(statusConnection.connected());
    BOOST_REQUIRE(wallet.Lock());
    BOOST_CHECK(notifiedLockedState);

    const CHDChain& lockedChain = wallet.GetHDChain();
    BOOST_CHECK(lockedChain.vchMnemonic.empty());
    BOOST_CHECK_EQUAL(lockedChain.vchMnemonic.capacity(), 0U);
    BOOST_CHECK(lockedChain.vchMnemonicPassphrase.empty());
    BOOST_CHECK_EQUAL(lockedChain.vchMnemonicPassphrase.capacity(), 0U);
    BOOST_CHECK(lockedChain.vchSeed.empty());
    BOOST_CHECK_EQUAL(lockedChain.vchSeed.capacity(), 0U);
}

BOOST_AUTO_TEST_CASE(pq_persistence_failure_rolls_back_in_memory_encryption)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const std::vector<unsigned char> secret = RawSecret(key);

    FailingPQPersistenceKeyStore keystore;
    BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));
    CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);
    BOOST_CHECK(!keystore.EncryptForTest(masterKey));
    BOOST_CHECK(!keystore.IsCrypted());

    CPQKey restored;
    BOOST_REQUIRE(keystore.GetPQKey(pubkey.GetWitnessProgram(), restored));
    BOOST_CHECK(RawSecret(restored) == secret);
}

BOOST_AUTO_TEST_CASE(pq_persistence_exception_rolls_back_in_memory_encryption)
{
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const std::vector<unsigned char> secret = RawSecret(key);

    ThrowingPQPersistenceKeyStore keystore;
    BOOST_REQUIRE(keystore.AddPQKeyPubKey(key, pubkey));
    CKeyingMaterial masterKey(WALLET_CRYPTO_KEY_SIZE, 0x24);
    BOOST_CHECK(!keystore.EncryptForTest(masterKey));
    BOOST_CHECK(!keystore.IsCrypted());

    CPQKey restored;
    BOOST_REQUIRE(keystore.GetPQKey(pubkey.GetWitnessProgram(), restored));
    BOOST_CHECK(RawSecret(restored) == secret);
}

BOOST_AUTO_TEST_CASE(unencrypted_pq_key_persists_and_reloads)
{
    const std::string filename = "pq-plain-wallet.dat";
    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();
    const std::vector<unsigned char> secret = RawSecret(key);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
        }

        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        PlainPQValue plainRecord;
        BOOST_REQUIRE(rawDb.Read(std::make_pair(std::string("pqkey"), witnessProgram), plainRecord));
        BOOST_CHECK(plainRecord.first.first == pubkey);
        BOOST_CHECK(plainRecord.first.second == secret);
        BOOST_CHECK(plainRecord == PlainPQRecord(pubkey, secret));
        BOOST_CHECK(!rawDb.Exists(std::make_pair(std::string("cpqkey"), witnessProgram)));
    }

    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        CPQKey loaded;
        BOOST_REQUIRE(wallet->GetPQKey(witnessProgram, loaded));
        BOOST_CHECK(loaded.MatchesPubKey(pubkey));
        BOOST_CHECK(RawSecret(loaded) == secret);
    }
}

BOOST_AUTO_TEST_CASE(oversized_plaintext_pq_record_is_rejected_before_secure_allocation)
{
    const std::string filename = "pq-oversized-secret-wallet.dat";

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
    }
    bitdb.Flush(false);

    // LockedPool rejects a single allocation above its 256-KiB arena. A
    // corrupt record must therefore be rejected from its encoded length,
    // before the secure vector is constructed.
    const std::vector<unsigned char> oversizedSecret(300000, 0x7b);
    {
        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r+");
        BOOST_REQUIRE(rawDb.Write(
            std::make_pair(std::string("pqkey"), witnessProgram),
            PlainPQRecord(pubkey, oversizedSecret)));
    }
    bitdb.Flush(false);

    std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
    bool firstRun = false;
    BOOST_CHECK_EQUAL(wallet->LoadWallet(firstRun), DB_CORRUPT);
}

BOOST_AUTO_TEST_CASE(encrypted_pq_keys_are_ciphertext_only_after_reload_and_backup)
{
    const std::string filename = "pq-encrypted-wallet.dat";
    const std::string backupFilename = "pq-encrypted-wallet-backup.dat";
    const SecureString passphrase("pq-wallet-regression-passphrase");

    CKey migratedECDSAKey;
    migratedECDSAKey.MakeNewKey(true);
    const CPubKey migratedECDSAPubkey = migratedECDSAKey.GetPubKey();
    BOOST_REQUIRE(migratedECDSAPubkey.IsValid());

    CPQKey migratedKey;
    migratedKey.MakeNewKey();
    BOOST_REQUIRE(migratedKey.IsValid());
    const CPQPubKey migratedPubkey = migratedKey.GetPubKey();
    const uint256 migratedProgram = migratedPubkey.GetWitnessProgram();
    const std::vector<unsigned char> migratedSecret = RawSecret(migratedKey);

    CPQKey addedAfterEncryption;
    addedAfterEncryption.MakeNewKey();
    BOOST_REQUIRE(addedAfterEncryption.IsValid());
    const CPQPubKey addedPubkey = addedAfterEncryption.GetPubKey();
    const uint256 addedProgram = addedPubkey.GetWitnessProgram();
    const std::vector<unsigned char> addedSecret = RawSecret(addedAfterEncryption);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(migratedECDSAKey, migratedECDSAPubkey));
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(migratedKey, migratedPubkey));
        }

        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
        BOOST_REQUIRE(wallet->Unlock(passphrase));
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(addedAfterEncryption, addedPubkey));
        }

        {
            CWalletDBWrapper rawDbw(&bitdb, filename);
            CDB rawDb(rawDbw, "r");
            std::vector<unsigned char> cryptedECDSASecret;
            BOOST_REQUIRE(rawDb.Read(
                std::make_pair(std::string("ckey"), migratedECDSAPubkey),
                cryptedECDSASecret));
            BOOST_CHECK(!rawDb.Exists(
                std::make_pair(std::string("key"), migratedECDSAPubkey)));
            BOOST_CHECK(!rawDb.Exists(
                std::make_pair(std::string("wkey"), migratedECDSAPubkey)));

            for (const auto& expected : {
                    std::make_pair(migratedProgram, migratedSecret),
                    std::make_pair(addedProgram, addedSecret)}) {
                CryptedPQValue cryptedRecord;
                BOOST_REQUIRE(rawDb.Read(
                    std::make_pair(std::string("cpqkey"), expected.first),
                    cryptedRecord));
                BOOST_CHECK(cryptedRecord.second != expected.second);
                BOOST_CHECK(!rawDb.Exists(
                    std::make_pair(std::string("pqkey"), expected.first)));
            }
        }

        BOOST_REQUIRE(wallet->BackupWallet((GetDataDir() / backupFilename).string()));
    }

    bitdb.Flush(false);

    for (const std::string& walletFile : {filename, backupFilename}) {
        BOOST_CHECK(!FileContainsSecret(GetDataDir() / walletFile, migratedSecret));
        BOOST_CHECK(!FileContainsSecret(GetDataDir() / walletFile, addedSecret));
    }

    // A binary backup has the same Berkeley DB file ID as its source. Close
    // each handle before opening the other copy in this environment.
    for (const std::string& walletFile : {backupFilename, filename}) {
        {
            std::unique_ptr<CWallet> wallet = LoadPQWallet(walletFile);
            BOOST_CHECK(wallet->IsCrypted());
            BOOST_CHECK(wallet->IsLocked());

            CPQKey loaded;
            CKey loadedECDSA;
            BOOST_CHECK(!wallet->GetKey(migratedECDSAPubkey.GetID(), loadedECDSA));
            BOOST_CHECK(!wallet->GetPQKey(migratedProgram, loaded));
            BOOST_REQUIRE(wallet->Unlock(passphrase));

            BOOST_REQUIRE(wallet->GetKey(migratedECDSAPubkey.GetID(), loadedECDSA));
            BOOST_CHECK(loadedECDSA.VerifyPubKey(migratedECDSAPubkey));

            BOOST_REQUIRE(wallet->GetPQKey(migratedProgram, loaded));
            BOOST_CHECK(loaded.MatchesPubKey(migratedPubkey));
            BOOST_CHECK(RawSecret(loaded) == migratedSecret);

            BOOST_REQUIRE(wallet->GetPQKey(addedProgram, loaded));
            BOOST_CHECK(loaded.MatchesPubKey(addedPubkey));
            BOOST_CHECK(RawSecret(loaded) == addedSecret);
        }
        bitdb.Flush(false);
    }
}

BOOST_AUTO_TEST_CASE(mixed_plaintext_and_ciphertext_pq_records_fail_load_and_backup)
{
    const std::string filename = "pq-mixed-wallet.dat";
    const std::string backupFilename = "pq-mixed-wallet-backup.dat";
    const SecureString passphrase("pq-mixed-wallet-passphrase");

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();
    const std::vector<unsigned char> secret = RawSecret(key);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
        }
        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));

        // Recreate the failure state left by a dropped pqkey erase error.
        {
            CWalletDBWrapper rawDbw(&bitdb, filename);
            CDB rawDb(rawDbw, "r+");
            BOOST_REQUIRE(rawDb.Write(
                std::make_pair(std::string("pqkey"), witnessProgram),
                PlainPQRecord(pubkey, secret)));
        }

        BOOST_CHECK(!wallet->BackupWallet((GetDataDir() / backupFilename).string()));
        BOOST_CHECK(!fs::exists(GetDataDir() / backupFilename));
    }

    bitdb.Flush(false);

    std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
    bool firstRun = false;
    BOOST_CHECK_EQUAL(wallet->LoadWallet(firstRun), DB_CORRUPT);
}

BOOST_AUTO_TEST_CASE(plaintext_pq_record_with_master_key_fails_load)
{
    const std::string filename = "pq-master-plaintext-wallet.dat";

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
    }
    {
        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r+");
        BOOST_REQUIRE(rawDb.Write(
            std::make_pair(std::string("mkey"), 1U), CMasterKey()));
    }

    bitdb.Flush(false);
    std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
    bool firstRun = false;
    BOOST_CHECK_EQUAL(wallet->LoadWallet(firstRun), DB_CORRUPT);
}

BOOST_AUTO_TEST_CASE(plaintext_ecdsa_record_with_master_key_fails_load_and_backup)
{
    const std::string filename = "ecdsa-mixed-wallet.dat";
    const std::string backupFilename = "ecdsa-mixed-wallet-backup.dat";
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    const std::vector<unsigned char> cryptedSecret(48, 0x6c);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddKeyPubKey(key, pubkey));
    }

    // Construct the failure state independently: encryption metadata exists
    // beside the original plaintext private-key record. Core 4.8.0 accepted
    // this combination because it checked mixed state only for PQ records.
    {
        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r+");
        BOOST_REQUIRE(rawDb.Write(
            std::make_pair(std::string("mkey"), 1U), CMasterKey(), false));
        BOOST_CHECK(rawDb.Exists(std::make_pair(std::string("key"), pubkey)));
    }

    {
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        std::unique_ptr<CWallet> wallet(new CWallet(std::move(dbw)));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet->LoadWallet(firstRun), DB_CORRUPT);
    }

    {
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        BOOST_REQUIRE(wallet.LoadCryptedKey(pubkey, cryptedSecret));
        BOOST_CHECK(!wallet.BackupWallet((GetDataDir() / backupFilename).string()));
        BOOST_CHECK(!fs::exists(GetDataDir() / backupFilename));
    }
}

BOOST_AUTO_TEST_CASE(bip44_encryption_write_failure_returns_and_aborts)
{
    const std::string filename = "bip44-write-failure-wallet.dat";
    const SecureString passphrase("bip44-write-failure-passphrase");
    const std::vector<unsigned char> words = Bip39TestWords();
    const std::vector<unsigned char> mnemonicPassphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> seed = Bip39TestSeed();
    const uint256 wordHash = Hash(words.begin(), words.end());

    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_REQUIRE(wallet->SetHDChain(Bip44TestChain(wallet.get()), false));
        BOOST_REQUIRE(wallet->LoadWords(wordHash, words));
        BOOST_REQUIRE(wallet->LoadPassphrase(mnemonicPassphrase));
        BOOST_REQUIRE(wallet->LoadVchSeed(seed));
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(key, pubkey));
        }
        {
            CWalletDB walletdb(wallet->GetDBHandle());
            BOOST_REQUIRE(walletdb.WriteBip39Words(wordHash, words, false));
            BOOST_REQUIRE(walletdb.WriteBip39Passphrase(mnemonicPassphrase, false));
            BOOST_REQUIRE(walletdb.WriteBip39VchSeed(seed, false));
        }
        {
            // Berkeley DB locks B-tree pages, not just logical records. Place
            // the encrypted BIP39 key on leaves well separated from the
            // short mkey/ckey keys so the blocker cannot stop encryption
            // before the live keystore has mutated.
            CDB filler(wallet->GetDBHandle(), "r+");
            BOOST_REQUIRE(filler.TxnBegin());
            const std::vector<unsigned char> padding(256, 0x31);
            for (int i = 0; i < 512; ++i) {
                BOOST_REQUIRE(filler.Write(strprintf("cbip39v%04d", i), padding));
                BOOST_REQUIRE(filler.Write(strprintf("cbip39x%04d", i), padding));
            }
            BOOST_REQUIRE(filler.TxnCommit());
        }

        // Hold the encrypted-word key in a second transaction. EncryptWallet
        // reaches this write only after EncryptKeys has replaced the live
        // plaintext key map. The fixed path must return through one cleanup,
        // not assert or continue through a freed CWalletDB pointer.
        {
            ScopedDBLockTimeout timeout(bitdb.dbenv, 5000000);
            CWalletDB blocker(wallet->GetDBHandle());
            BOOST_REQUIRE(blocker.TxnBegin());
            BOOST_REQUIRE(blocker.WriteBip39Words(
                wordHash, std::vector<unsigned char>(32, 0xa7), true));
            std::atomic<bool> cycleAttempted{false};
            std::atomic<bool> cycleResolved{false};
            std::atomic<bool> blockerWriteSucceeded{false};
            std::thread cycleThread([&blocker, &blockerWriteSucceeded, &cycleAttempted,
                                     &cycleResolved, &wallet] {
                // IsCrypted becomes true only after EncryptKeys has replaced
                // the live maps and the younger transaction owns mkey/ckey.
                for (int attempt = 0; attempt < 500; ++attempt) {
                    if (wallet->IsCrypted()) {
                        cycleAttempted = true;
                        blockerWriteSucceeded = blocker.WriteMasterKey(1U, CMasterKey());
                        cycleResolved = true;
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
            std::thread detectorThread([&cycleAttempted, &cycleResolved] {
                for (int attempt = 0; attempt < 500 && !cycleAttempted; ++attempt)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                for (int attempt = 0; attempt < 50 && !cycleResolved; ++attempt) {
                    int aborted = 0;
                    bitdb.dbenv->lock_detect(0, DB_LOCK_YOUNGEST, &aborted);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
            const bool encrypted = wallet->EncryptWallet(passphrase);
            cycleThread.join();
            detectorThread.join();
            BOOST_CHECK(!encrypted);
            BOOST_CHECK(cycleAttempted);
            BOOST_CHECK(cycleResolved);
            BOOST_CHECK(blockerWriteSucceeded);
            BOOST_CHECK(wallet->IsCrypted());
            BOOST_CHECK(wallet->IsLocked());
            BOOST_REQUIRE(blocker.TxnAbort());
        }
    }

    bitdb.Flush(false);

    // The failed encryption transaction must leave the original on-disk
    // wallet intact and plaintext; restart reconstructs its exact key and
    // deterministic-seed inputs.
    std::unique_ptr<CWallet> reloaded = LoadPQWallet(filename);
    BOOST_CHECK(!reloaded->IsCrypted());
    CKey loadedKey;
    BOOST_REQUIRE(reloaded->GetKey(pubkey.GetID(), loadedKey));
    BOOST_CHECK(loadedKey.VerifyPubKey(pubkey));
    uint256 loadedHash;
    std::vector<unsigned char> loadedWords;
    std::vector<unsigned char> loadedPassphrase;
    std::vector<unsigned char> loadedSeed;
    reloaded->GetBip39Data(loadedHash, loadedWords, loadedPassphrase, loadedSeed);
    BOOST_CHECK(loadedHash == wordHash);
    BOOST_CHECK(loadedWords == words);
    BOOST_CHECK(loadedPassphrase == mnemonicPassphrase);
    BOOST_CHECK(loadedSeed == seed);
}

BOOST_AUTO_TEST_CASE(wallet_database_clears_entire_new_page)
{
    const std::string filename = "pq-full-page-clear-wallet.dat";
    CWalletDBWrapper dbw(&bitdb, filename);
    CDB db(dbw, "c+");

    LOCK(bitdb.cs_db);
    const auto it = bitdb.mapDb.find(filename);
    BOOST_REQUIRE(it != bitdb.mapDb.end());
    BOOST_REQUIRE(it->second != nullptr);

    u_int32_t pageSize = 0;
    u_int32_t clearLength = 0;
    BOOST_REQUIRE_EQUAL(it->second->get_pagesize(&pageSize), 0);
    DbMpoolFile* mpoolFile = it->second->get_mpf();
    BOOST_REQUIRE(mpoolFile != nullptr);
    BOOST_REQUIRE_EQUAL(mpoolFile->get_clear_len(&clearLength), 0);
    BOOST_REQUIRE_GT(pageSize, 32U);
    BOOST_CHECK_EQUAL(clearLength, pageSize);
}

BOOST_AUTO_TEST_CASE(rewrite_discards_stale_regular_temporary_database)
{
    const std::string filename = "pq-rewrite-stale-wallet.dat";
    const std::string rewriteFilename = filename + ".rewrite";

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
    }

    // Model a complete but stale artifact from an earlier failed rewrite.
    // Its automatically-created version key collides with the source copy and
    // made the old DB_NOOVERWRITE loop fail on every retry.
    {
        CWalletDBWrapper staleDbw(&bitdb, rewriteFilename);
        CDB staleDb(staleDbw, "cr+");
        BOOST_REQUIRE(staleDb.Write(std::string("stale-rewrite-record"), 1));
    }
    // Leave the zero-refcount Berkeley DB handle cached. Rewrite must close it
    // before transactionally removing the stale database on every platform.
    BOOST_REQUIRE(fs::is_regular_file(GetDataDir() / rewriteFilename));

    {
        CWalletDBWrapper sourceDbw(&bitdb, filename);
        BOOST_REQUIRE(sourceDbw.Rewrite());
    }
    BOOST_CHECK(!fs::exists(GetDataDir() / rewriteFilename));

    {
        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        PlainPQValue plainRecord;
        BOOST_REQUIRE(rawDb.Read(
            std::make_pair(std::string("pqkey"), witnessProgram), plainRecord));
        BOOST_CHECK(!rawDb.Exists(std::string("stale-rewrite-record")));
    }
}

BOOST_AUTO_TEST_CASE(rewrite_namespace_transaction_abort_preserves_source)
{
    const std::string filename = "pq-rewrite-abort-wallet.dat";
    const std::string replacementFilename = filename + ".replacement";
    const std::string missingFilename = filename + ".missing";

    {
        CWalletDBWrapper sourceDbw(&bitdb, filename);
        CDB sourceDb(sourceDbw, "cr+");
        BOOST_REQUIRE(sourceDb.Write(std::string("old-source-record"), 1));

        CWalletDBWrapper replacementDbw(&bitdb, replacementFilename);
        CDB replacementDb(replacementDbw, "cr+");
        BOOST_REQUIRE(replacementDb.Write(std::string("new-replacement-record"), 2));
    }
    bitdb.Flush(false);

    // Exercise the same Berkeley DB namespace primitive used by Rewrite. A
    // failure after the transactional remove must restore the source name.
    DbTxn* txn = bitdb.TxnBegin();
    BOOST_REQUIRE(txn != nullptr);
    BOOST_REQUIRE_EQUAL(bitdb.dbenv->dbremove(txn, filename.c_str(), nullptr, 0), 0);
    BOOST_REQUIRE_NE(
        bitdb.dbenv->dbrename(txn, missingFilename.c_str(), nullptr, filename.c_str(), 0),
        0);
    BOOST_REQUIRE_EQUAL(txn->abort(), 0);

    {
        CWalletDBWrapper sourceDbw(&bitdb, filename);
        CDB sourceDb(sourceDbw, "r");
        int value = 0;
        BOOST_REQUIRE(sourceDb.Read(std::string("old-source-record"), value));
        BOOST_CHECK_EQUAL(value, 1);
        BOOST_CHECK(!sourceDb.Exists(std::string("new-replacement-record")));
    }
}

BOOST_AUTO_TEST_CASE(wallet_database_sync_transaction_flushes_log)
{
    const std::string filename = "wallet-sync-transaction.dat";
    CWalletDBWrapper dbw(&bitdb, filename);
    CWalletDB walletdb(dbw, "c+", false);

    DB_LOG_STAT* clearedLogStats = nullptr;
    BOOST_REQUIRE_EQUAL(
        bitdb.dbenv->log_stat(&clearedLogStats, DB_STAT_CLEAR), 0);
    free(clearedLogStats);

    BOOST_REQUIRE(walletdb.TxnBegin(DB_TXN_SYNC));
    BOOST_REQUIRE(walletdb.WriteMinVersion(FEATURE_WALLETCRYPT));
    BOOST_REQUIRE(walletdb.TxnCommit(DB_TXN_SYNC));

    DB_LOG_STAT* transactionLogStats = nullptr;
    BOOST_REQUIRE_EQUAL(bitdb.dbenv->log_stat(&transactionLogStats, 0), 0);
    BOOST_REQUIRE(transactionLogStats != nullptr);
    BOOST_CHECK_GE(transactionLogStats->st_scount, 1U);
    free(transactionLogStats);
}

BOOST_AUTO_TEST_CASE(rewrite_failure_quarantines_until_restart_recovery)
{
    const std::string filename = "pq-rewrite-failure-wallet.dat";
    const std::string backupFilename = "pq-rewrite-failure-wallet-backup.dat";
    const SecureString passphrase("pq-rewrite-failure-passphrase");

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();
    const std::vector<unsigned char> secret = RawSecret(key);
    const fs::path rewritePath = GetDataDir() / (filename + ".rewrite");

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
        }
        bitdb.Flush(false);
        BOOST_REQUIRE(FileContainsSecret(GetDataDir() / filename, secret));

        // CDB::Rewrite creates this path as a database file. A directory at
        // that exact path deterministically injects rewrite failure.
        BOOST_REQUIRE(fs::create_directory(rewritePath));

        BOOST_REQUIRE(!wallet->EncryptWallet(passphrase));
        BOOST_REQUIRE(wallet->IsCrypted());
        BOOST_REQUIRE(wallet->IsLocked());
        BOOST_REQUIRE(wallet->IsEncryptionRewritePending());

        {
            CWalletDBWrapper rawDbw(&bitdb, filename);
            CDB rawDb(rawDbw, "r");
            std::pair<uint32_t, int> marker;
            int minVersion = 0;
            CryptedPQValue cryptedRecord;
            BOOST_REQUIRE(rawDb.Read(
                std::string("encryption_rewrite_pending"), marker));
            BOOST_CHECK_EQUAL(
                marker.first, WALLET_ENCRYPTION_REWRITE_MARKER_VERSION);
            BOOST_CHECK_GE(marker.second, FEATURE_WALLETCRYPT);
            BOOST_CHECK_LE(marker.second, CLIENT_VERSION);
            BOOST_REQUIRE(rawDb.Read(std::string("minversion"), minVersion));
            BOOST_CHECK_EQUAL(
                minVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);
            BOOST_REQUIRE(rawDb.Read(
                std::make_pair(std::string("cpqkey"), witnessProgram),
                cryptedRecord));
            BOOST_CHECK(!rawDb.Exists(
                std::make_pair(std::string("pqkey"), witnessProgram)));
        }
        bitdb.Flush(false);
        BOOST_REQUIRE(FileContainsSecret(GetDataDir() / filename, secret));

        // Remove the injected filesystem failure before testing quarantine.
        // Backup and unlock must still refuse instead of repairing the live
        // wallet through an unrelated operation.
        BOOST_REQUIRE(fs::remove(rewritePath));

        size_t pendingKeypoolSize = 0;
        {
            LOCK(wallet->cs_wallet);
            pendingKeypoolSize = wallet->KeypoolCountExternalKeys();
        }
        BOOST_REQUIRE_GT(pendingKeypoolSize, 0U);
        BOOST_REQUIRE(!wallet->NewKeyPool());
        BOOST_REQUIRE(!wallet->TopUpKeyPool(2));
        CPubKey quarantinedKey;
        BOOST_REQUIRE(!wallet->GetKeyFromPool(quarantinedKey));
        CReserveKey quarantinedReserveKey(wallet.get());
        BOOST_REQUIRE(!quarantinedReserveKey.GetReservedKey(quarantinedKey));
        {
            LOCK(wallet->cs_wallet);
            BOOST_CHECK_EQUAL(
                wallet->KeypoolCountExternalKeys(), pendingKeypoolSize);
        }

        BOOST_REQUIRE(!wallet->Unlock(passphrase));
        BOOST_REQUIRE(!wallet->ChangeWalletPassphrase(passphrase, passphrase));
        BOOST_REQUIRE(!wallet->BackupWallet(
            (GetDataDir() / backupFilename).string()));
        BOOST_REQUIRE(!fs::exists(GetDataDir() / backupFilename));

        CWalletTx preparedTransaction;
        CReserveKey reserveKey(wallet.get());
        CValidationState state;
        BOOST_REQUIRE(!wallet->CommitTransaction(
            preparedTransaction, reserveKey, nullptr, state));

        bitdb.Flush(false);
        BOOST_REQUIRE(FileContainsSecret(GetDataDir() / filename, secret));

        // Recreate the same blocker to prove that an unsuccessful restart does
        // not publish or clear the pending state.
        BOOST_REQUIRE(fs::create_directory(rewritePath));
    }

    bitdb.Flush(false);

    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = true;
        BOOST_CHECK_EQUAL(
            wallet.LoadWallet(firstRun), DB_NEED_REWRITE_ENCRYPTION);
        BOOST_CHECK(wallet.IsEncryptionRewritePending());
        BOOST_CHECK(wallet.IsCrypted());
        BOOST_CHECK(wallet.IsLocked());
        BOOST_CHECK(!wallet.Unlock(passphrase));
    }
    bitdb.Flush(false);
    BOOST_REQUIRE(FileContainsSecret(GetDataDir() / filename, secret));

    {
        CWalletDBWrapper rawDbw(&bitdb, filename);
        CDB rawDb(rawDbw, "r");
        BOOST_CHECK(rawDb.Exists(std::string("encryption_rewrite_pending")));
        int minVersion = 0;
        BOOST_REQUIRE(rawDb.Read(std::string("minversion"), minVersion));
        BOOST_CHECK_EQUAL(minVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);
    }

    BOOST_REQUIRE(fs::remove(rewritePath));
    bitdb.Flush(false);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        BOOST_CHECK(!wallet->IsEncryptionRewritePending());
        BOOST_CHECK(wallet->IsCrypted());
        BOOST_CHECK(wallet->IsLocked());

        // Inspect before backup. Otherwise BackupWallet's own compaction could
        // hide a missing startup recovery.
        bitdb.Flush(false);
        BOOST_CHECK(!FileContainsSecret(GetDataDir() / filename, secret));
        {
            CWalletDBWrapper rawDbw(&bitdb, filename);
            CDB rawDb(rawDbw, "r");
            BOOST_CHECK(!rawDb.Exists(
                std::string("encryption_rewrite_pending")));
            int minVersion = 0;
            BOOST_REQUIRE(rawDb.Read(std::string("minversion"), minVersion));
            BOOST_CHECK_NE(minVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);
            CryptedPQValue cryptedRecord;
            BOOST_REQUIRE(rawDb.Read(
                std::make_pair(std::string("cpqkey"), witnessProgram),
                cryptedRecord));
            BOOST_CHECK(!rawDb.Exists(
                std::make_pair(std::string("pqkey"), witnessProgram)));
        }

        BOOST_REQUIRE(wallet->Unlock(passphrase));
        CPQKey loadedKey;
        BOOST_REQUIRE(wallet->GetPQKey(witnessProgram, loadedKey));
        BOOST_CHECK(loadedKey.MatchesPubKey(pubkey));
        BOOST_REQUIRE(wallet->BackupWallet(
            (GetDataDir() / backupFilename).string()));
    }

    bitdb.Flush(false);
    BOOST_CHECK(!FileContainsSecret(GetDataDir() / backupFilename, secret));
}

BOOST_AUTO_TEST_CASE(encryption_rewrite_marker_states_fail_closed)
{
    const SecureString passphrase("rewrite-marker-state-passphrase");

    auto createEncryptedWallet = [&](const std::string& filename) {
        CKey key;
        key.MakeNewKey(true);
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(key, key.GetPubKey()));
        }
        BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
        return wallet;
    };

    for (int state = 0; state < 3; ++state) {
        const std::string filename =
            strprintf("rewrite-marker-invalid-state-%d.dat", state);
        const std::string backupFilename =
            strprintf("rewrite-marker-invalid-state-%d-backup.dat", state);
        std::unique_ptr<CWallet> liveWallet =
            createEncryptedWallet(filename);
        bitdb.Flush(false);

        {
            CWalletDBWrapper rawDbw(&bitdb, filename);
            CDB rawDb(rawDbw, "r+");
            int previousMinVersion = 0;
            BOOST_REQUIRE(rawDb.Read(
                std::string("minversion"), previousMinVersion));
            BOOST_REQUIRE_NE(
                previousMinVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);

            if (state == 0) {
                BOOST_REQUIRE(rawDb.Write(
                    std::string("minversion"),
                    WALLET_ENCRYPTION_REWRITE_MIN_VERSION));
            } else if (state == 1) {
                BOOST_REQUIRE(rawDb.Write(
                    std::string("encryption_rewrite_pending"),
                    std::make_pair(
                        WALLET_ENCRYPTION_REWRITE_MARKER_VERSION,
                        previousMinVersion)));
            } else {
                BOOST_REQUIRE(rawDb.Write(
                    std::string("encryption_rewrite_pending"),
                    std::make_pair(
                        WALLET_ENCRYPTION_REWRITE_MARKER_VERSION + 1,
                        previousMinVersion)));
                BOOST_REQUIRE(rawDb.Write(
                    std::string("minversion"),
                    WALLET_ENCRYPTION_REWRITE_MIN_VERSION));
            }
        }
        bitdb.Flush(false);

        BOOST_CHECK(!liveWallet->BackupWallet(
            (GetDataDir() / backupFilename).string()));
        BOOST_CHECK(!fs::exists(GetDataDir() / backupFilename));
        liveWallet.reset();
        bitdb.Flush(false);

        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_CORRUPT);
        bitdb.Flush(false);
    }

    const std::string unencryptedFilename =
        "rewrite-marker-unencrypted-state.dat";
    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(unencryptedFilename);
    }
    {
        CWalletDBWrapper rawDbw(&bitdb, unencryptedFilename);
        CDB rawDb(rawDbw, "r+");
        BOOST_REQUIRE(rawDb.Write(
            std::string("encryption_rewrite_pending"),
            std::make_pair(
                WALLET_ENCRYPTION_REWRITE_MARKER_VERSION,
                static_cast<int>(FEATURE_WALLETCRYPT))));
        BOOST_REQUIRE(rawDb.Write(
            std::string("minversion"),
            WALLET_ENCRYPTION_REWRITE_MIN_VERSION));
    }
    bitdb.Flush(false);

    std::unique_ptr<CWalletDBWrapper> unencryptedDbw(
        new CWalletDBWrapper(&bitdb, unencryptedFilename));
    CWallet unencryptedWallet(std::move(unencryptedDbw));
    bool firstRun = false;
    BOOST_CHECK_EQUAL(unencryptedWallet.LoadWallet(firstRun), DB_CORRUPT);
}

BOOST_AUTO_TEST_CASE(encryption_rewrite_preserves_noncritical_load_status)
{
    const std::string filename =
        "rewrite-marker-noncritical-wallet.dat";
    const SecureString passphrase("rewrite-marker-noncritical-passphrase");
    const fs::path rewritePath = GetDataDir() / (filename + ".rewrite");

    {
        CKey key;
        key.MakeNewKey(true);
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddKeyPubKey(key, key.GetPubKey()));
        }
        BOOST_REQUIRE(fs::create_directory(rewritePath));
        BOOST_REQUIRE(!wallet->EncryptWallet(passphrase));
        BOOST_REQUIRE(wallet->IsEncryptionRewritePending());
    }
    bitdb.Flush(false);

    CDataStream malformedNameKey(SER_DISK, CLIENT_VERSION);
    malformedNameKey << std::string("name");
    const std::vector<unsigned char> malformedKey(
        malformedNameKey.begin(), malformedNameKey.end());
    BOOST_REQUIRE(wallet_db::RecoveryTestAccess::WriteRaw(
        filename, malformedKey, std::vector<unsigned char>()));

    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(
            wallet.LoadWallet(firstRun),
            DB_NEED_REWRITE_ENCRYPTION_NONCRITICAL);
        BOOST_CHECK(wallet.IsEncryptionRewritePending());
    }

    BOOST_REQUIRE(fs::remove(rewritePath));
    bitdb.Flush(false);

    {
        std::unique_ptr<CWalletDBWrapper> dbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet wallet(std::move(dbw));
        bool firstRun = false;
        BOOST_CHECK_EQUAL(wallet.LoadWallet(firstRun), DB_NONCRITICAL_ERROR);
        BOOST_CHECK(!wallet.IsEncryptionRewritePending());
    }

    CWalletDBWrapper rawDbw(&bitdb, filename);
    CDB rawDb(rawDbw, "r");
    BOOST_CHECK(!rawDb.Exists(std::string("encryption_rewrite_pending")));
    int minVersion = 0;
    BOOST_REQUIRE(rawDb.Read(std::string("minversion"), minVersion));
    BOOST_CHECK_NE(minVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);
}

BOOST_AUTO_TEST_CASE(explicit_salvage_compacts_pending_wallet_and_retains_sensitive_original)
{
    const std::string filename = "rewrite-salvage-wallet.dat";
    const SecureString passphrase("rewrite-salvage-passphrase");
    const fs::path rewritePath = GetDataDir() / (filename + ".rewrite");

    CPQKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CPQPubKey pubkey = key.GetPubKey();
    const uint256 witnessProgram = pubkey.GetWitnessProgram();
    const std::vector<unsigned char> secret = RawSecret(key);

    {
        std::unique_ptr<CWallet> wallet = LoadPQWallet(filename);
        {
            LOCK(wallet->cs_wallet);
            BOOST_REQUIRE(wallet->AddPQKeyPubKey(key, pubkey));
        }
        bitdb.Flush(false);
        BOOST_REQUIRE(fs::create_directory(rewritePath));
        BOOST_REQUIRE(!wallet->EncryptWallet(passphrase));
        BOOST_REQUIRE(wallet->IsEncryptionRewritePending());
    }
    bitdb.Flush(false);

    const fs::path walletPath = GetDataDir() / filename;
    const std::string pendingBytes = ReadFileBytes(walletPath);
    BOOST_REQUIRE(FileContainsSecret(walletPath, secret));

    CWallet dummyWallet;
    std::string retainedBackupFilename;
    BOOST_REQUIRE(CWalletDB::Recover(
        filename, &dummyWallet, CWalletDB::RecoverKeysOnlyFilter,
        retainedBackupFilename));
    BOOST_REQUIRE(!retainedBackupFilename.empty());

    const fs::path retainedBackupPath =
        GetDataDir() / retainedBackupFilename;
    BOOST_CHECK_EQUAL(ReadFileBytes(retainedBackupPath), pendingBytes);
    BOOST_CHECK(FileContainsSecret(retainedBackupPath, secret));
    BOOST_CHECK(!FileContainsSecret(walletPath, secret));

    {
        CWalletDBWrapper backupDbw(&bitdb, retainedBackupFilename);
        CDB backupDb(backupDbw, "r");
        std::pair<uint32_t, int> marker;
        int minVersion = 0;
        BOOST_REQUIRE(backupDb.Read(
            std::string("encryption_rewrite_pending"), marker));
        BOOST_CHECK_EQUAL(
            marker.first, WALLET_ENCRYPTION_REWRITE_MARKER_VERSION);
        BOOST_REQUIRE(backupDb.Read(std::string("minversion"), minVersion));
        BOOST_CHECK_EQUAL(
            minVersion, WALLET_ENCRYPTION_REWRITE_MIN_VERSION);
    }

    {
        CWalletDBWrapper activeDbw(&bitdb, filename);
        CDB activeDb(activeDbw, "r");
        BOOST_CHECK(!activeDb.Exists(
            std::string("encryption_rewrite_pending")));
        int minVersion = 0;
        BOOST_CHECK(!activeDb.Read(std::string("minversion"), minVersion));
        CryptedPQValue cryptedRecord;
        BOOST_REQUIRE(activeDb.Read(
            std::make_pair(std::string("cpqkey"), witnessProgram),
            cryptedRecord));
        BOOST_CHECK(!activeDb.Exists(
            std::make_pair(std::string("pqkey"), witnessProgram)));
    }

    std::unique_ptr<CWallet> recovered = LoadPQWallet(filename);
    BOOST_CHECK(recovered->IsCrypted());
    BOOST_CHECK(recovered->IsLocked());
    BOOST_REQUIRE(recovered->Unlock(passphrase));
    CPQKey recoveredKey;
    BOOST_REQUIRE(recovered->GetPQKey(witnessProgram, recoveredKey));
    BOOST_CHECK(recoveredKey.MatchesPubKey(pubkey));
}

BOOST_AUTO_TEST_CASE(bip44_creation_transaction_aborts_lineage_and_keypool)
{
    const std::string filename = "bip44-atomic-creation-wallet.dat";
    const std::vector<unsigned char> expectedWords = Bip39TestWords();
    const std::vector<unsigned char> expectedPassphrase = Bip39TestPassphrase();
    const std::vector<unsigned char> expectedSeed = Bip39TestSeed();
    const uint256 expectedWordHash = Hash(expectedWords.begin(), expectedWords.end());

    ScopedArgState mnemonicArg("-mnemonic");
    ScopedArgState passphraseArg("-mnemonicpassphrase");
    ScopedMnemonicInput mnemonicInput;
    gArgs.ClearArg("-mnemonic");
    gArgs.ClearArg("-mnemonicpassphrase");
    ClearPendingMnemonicInput();

    {
        CWalletDBWrapper fillerDbw(&bitdb, filename);
        CDB filler(fillerDbw, "c+");
        BOOST_REQUIRE(filler.TxnBegin());
        const std::vector<unsigned char> padding(256, 0x41);
        for (int i = 0; i < 512; ++i) {
            BOOST_REQUIRE(filler.Write(
                strprintf("bip39passphq%03d", i), padding));
            BOOST_REQUIRE(filler.Write(
                strprintf("bip39passphs%03d", i), padding));
        }
        BOOST_REQUIRE(filler.TxnCommit());
    }

    unsigned int promptCount = 0;
    unsigned int loadNotifications = 0;
    bool blockFirstPrompt = true;
    std::atomic<bool> startBlocker{false};
    std::atomic<bool> blockerReady{false};
    std::atomic<bool> releaseBlocker{false};
    std::atomic<bool> cancelBlocker{false};
    std::atomic<bool> blockerWriteSucceeded{false};
    std::atomic<bool> blockerAbortSucceeded{false};
    std::atomic<bool> blockerDeadlineExpired{false};
    std::thread blockerThread([&] {
        while (!startBlocker && !cancelBlocker)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (cancelBlocker)
            return;

        try {
            CWalletDBWrapper blockerDbw(&bitdb, filename);
            CWalletDB blocker(blockerDbw);
            if (blocker.TxnBegin()) {
                blockerWriteSucceeded = blocker.WriteBip39Passphrase(
                    std::vector<unsigned char>(expectedPassphrase.size(), 0x72),
                    false);
                blockerReady = true;
                const std::chrono::steady_clock::time_point deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(60);
                while (!releaseBlocker &&
                       std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (!releaseBlocker)
                    blockerDeadlineExpired = true;
                blockerAbortSucceeded = blocker.TxnAbort();
                return;
            }
        } catch (...) {
        }
        blockerReady = true;
    });
    ScopedThreadCancellation blockerThreadCleanup(
        blockerThread, cancelBlocker, releaseBlocker);
    boost::signals2::scoped_connection loadConnection(
        uiInterface.LoadWallet.connect(
            [&](CWallet*) {
                ++loadNotifications;
            }));
    boost::signals2::scoped_connection mnemonicConnection(
        uiInterface.ShowMnemonic.connect(
            [&](int) {
                ++promptCount;
                SetPendingMnemonicInput(
                    SecureString(BIP39_TEST_MNEMONIC.begin(),
                                 BIP39_TEST_MNEMONIC.end()),
                    SecureString(BIP39_TEST_PASSPHRASE.begin(),
                                 BIP39_TEST_PASSPHRASE.end()));
                if (!blockFirstPrompt)
                    return;

                blockFirstPrompt = false;
                startBlocker = true;
                while (!blockerReady)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (!blockerWriteSucceeded) {
                    throw std::runtime_error(
                        "failed to establish BIP39 creation blocker");
                }
            }));

    ScopedDBExpiredLockTimeout timeout(bitdb.dbenv, 100000);
    std::atomic<bool> detectorFailed{false};
    std::atomic<bool> timeoutObserved{false};
    std::atomic<bool> factoryAttemptComplete{false};
    std::atomic<bool> detectorDeadlineExpired{false};
    std::thread detectorThread([&] {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!factoryAttemptComplete &&
               std::chrono::steady_clock::now() < deadline) {
            int rejected = 0;
            if (bitdb.dbenv->lock_detect(0, DB_LOCK_EXPIRE, &rejected) != 0) {
                detectorFailed = true;
                break;
            }
            if (rejected > 0) {
                timeoutObserved = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!factoryAttemptComplete && !timeoutObserved && !detectorFailed)
            detectorDeadlineExpired = true;
        releaseBlocker = true;
    });
    ScopedThreadCancellation detectorThreadCleanup(
        detectorThread, factoryAttemptComplete, releaseBlocker);
    CWallet* failedWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        failedWallet = CWallet::CreateWalletFromFile(filename));
    factoryAttemptComplete = true;
    releaseBlocker = true;
    detectorThread.join();
    cancelBlocker = true;
    blockerThread.join();
    if (failedWallet) {
        UnregisterValidationInterface(failedWallet);
        delete failedWallet;
        BOOST_FAIL("wallet creation unexpectedly survived the blocked write");
    }

    BOOST_CHECK(!detectorFailed);
    BOOST_CHECK(!detectorDeadlineExpired);
    BOOST_CHECK(!blockerDeadlineExpired);
    BOOST_CHECK(timeoutObserved);
    BOOST_CHECK(blockerWriteSucceeded);
    BOOST_CHECK(blockerAbortSucceeded);
    BOOST_CHECK_EQUAL(promptCount, 1U);
    BOOST_CHECK_EQUAL(loadNotifications, 0U);
    BOOST_CHECK(!HasPendingMnemonicInput());
    BOOST_CHECK(!WalletContainsAnyRecordType(
        filename,
        {"hdchain", "bip39words", "bip39passphrase", "bip39vchseed"}));
    BOOST_CHECK(!WalletContainsAnyRecordType(
        filename, {"key", "wkey", "ckey", "keymeta", "pool"}));

    {
        std::unique_ptr<CWalletDBWrapper> probeDbw(
            new CWalletDBWrapper(&bitdb, filename));
        CWallet probe(std::move(probeDbw));
        bool firstRun = false;
        BOOST_REQUIRE_EQUAL(probe.LoadWallet(firstRun, false), DB_LOAD_OK);
        BOOST_CHECK(firstRun);
    }

    CWallet* createdWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        createdWallet = CWallet::CreateWalletFromFile(filename));
    BOOST_REQUIRE(createdWallet != nullptr);
    BOOST_CHECK_EQUAL(promptCount, 2U);
    BOOST_CHECK_EQUAL(loadNotifications, 1U);
    BOOST_CHECK(!HasPendingMnemonicInput());
    loadConnection.disconnect();
    mnemonicConnection.disconnect();
    UnregisterValidationInterface(createdWallet);
    delete createdWallet;
    bitdb.Flush(false);

    std::unique_ptr<CWalletDBWrapper> reloadedDbw(
        new CWalletDBWrapper(&bitdb, filename));
    std::unique_ptr<CWallet> reloaded(new CWallet(std::move(reloadedDbw)));
    bool firstRun = true;
    BOOST_REQUIRE_EQUAL(reloaded->LoadWallet(firstRun, false), DB_LOAD_OK);
    BOOST_CHECK(!firstRun);

    uint256 wordHash;
    std::vector<unsigned char> words;
    std::vector<unsigned char> passphrase;
    std::vector<unsigned char> seed;
    reloaded->GetBip39Data(wordHash, words, passphrase, seed);
    BOOST_CHECK(wordHash == expectedWordHash);
    BOOST_CHECK(words == expectedWords);
    BOOST_CHECK(passphrase == expectedPassphrase);
    BOOST_CHECK(seed == expectedSeed);

    const std::vector<unsigned char> expectedBytes = ParseHex(
        "023765b56ecb006a47d775beee38c45a9fe5dbe11d100b2e2ea3c99196dc915a2d");
    const CPubKey expectedFirstExternal(expectedBytes.begin(), expectedBytes.end());
    BOOST_REQUIRE(expectedFirstExternal.IsValid());
    CPubKey firstExternal;
    BOOST_REQUIRE(reloaded->GetKeyFromPool(firstExternal, false));
    BOOST_CHECK(firstExternal == expectedFirstExternal);
}

BOOST_AUTO_TEST_CASE(failed_wallet_creation_is_not_published)
{
    const std::string filename = "failed-unpublished-wallet.dat";
    unsigned int loadNotifications = 0;
    CWallet* notifiedWallet = nullptr;

    boost::signals2::scoped_connection loadConnection(
        uiInterface.LoadWallet.connect(
            [&](CWallet* wallet) {
                ++loadNotifications;
                notifiedWallet = wallet;
            }));
    boost::signals2::scoped_connection mnemonicConnection(
        uiInterface.ShowMnemonic.connect(
            [](int) {
                throw std::runtime_error("injected mnemonic UI failure");
            }));

    CWallet* unexpectedlyCreated = nullptr;
    BOOST_CHECK_THROW(
        unexpectedlyCreated = CWallet::CreateWalletFromFile(filename),
        std::runtime_error);
    if (unexpectedlyCreated) {
        UnregisterValidationInterface(unexpectedlyCreated);
        delete unexpectedlyCreated;
    }
    BOOST_CHECK_EQUAL(loadNotifications, 0U);
    BOOST_CHECK(notifiedWallet == nullptr);
}

BOOST_AUTO_TEST_CASE(throwing_load_observer_cannot_dangle_wallet)
{
    const std::string filename = "throwing-load-observer-wallet.dat";
    CWallet* notifiedWallet = nullptr;
    boost::signals2::scoped_connection retainingConnection(
        uiInterface.LoadWallet.connect(
            [&](CWallet* wallet) {
                notifiedWallet = wallet;
            }));
    boost::signals2::scoped_connection throwingConnection(
        uiInterface.LoadWallet.connect(
            [](CWallet*) {
                throw std::runtime_error("injected load observer failure");
            }));

    CWallet* createdWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        createdWallet = CWallet::CreateWalletFromFile(filename));
    BOOST_REQUIRE(createdWallet != nullptr);
    BOOST_CHECK_EQUAL(createdWallet, notifiedWallet);

    UnregisterValidationInterface(createdWallet);
    delete createdWallet;
}

BOOST_AUTO_TEST_CASE(nonstandard_load_observer_cannot_dangle_wallet)
{
    const std::string filename = "nonstandard-load-observer-wallet.dat";
    CWallet* notifiedWallet = nullptr;
    boost::signals2::scoped_connection retainingConnection(
        uiInterface.LoadWallet.connect(
            [&](CWallet* wallet) {
                notifiedWallet = wallet;
            }));
    boost::signals2::scoped_connection throwingConnection(
        uiInterface.LoadWallet.connect(
            [](CWallet*) {
                throw 7;
            }));

    CWallet* createdWallet = nullptr;
    BOOST_CHECK_NO_THROW(
        createdWallet = CWallet::CreateWalletFromFile(filename));
    BOOST_REQUIRE(createdWallet != nullptr);
    BOOST_CHECK_EQUAL(createdWallet, notifiedWallet);

    UnregisterValidationInterface(createdWallet);
    delete createdWallet;
}

BOOST_AUTO_TEST_SUITE_END()
