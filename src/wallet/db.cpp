// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include "addrman.h"
#include "fs.h"
#include "hash.h"
#include "protocol.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <cerrno>
#include <cctype>
#include <stdint.h>
#include <limits>
#include <sstream>

#ifndef WIN32
#include <sys/stat.h>
#endif

#include <boost/thread.hpp>

namespace {
using SalvageString =
    std::basic_string<char, std::char_traits<char>, zero_after_free_allocator<char>>;
using SalvageStream =
    std::basic_stringstream<char, std::char_traits<char>, zero_after_free_allocator<char>>;

void ClearSalvageString(SalvageString& value)
{
    // Short lines may live inside the string object rather than its allocator.
    if (value.capacity() != 0) {
        value.resize(value.capacity(), '\0');
        memory_cleanse(&value[0], value.size());
    }
    SalvageString().swap(value);
}

CDBEnv::SalvagedBytes ParseSalvageHex(const SalvageString& encoded)
{
    CDBEnv::SalvagedBytes result;
    const char* psz = encoded.c_str();
    while (true) {
        while (std::isspace(static_cast<unsigned char>(*psz)))
            ++psz;
        signed char digit = HexDigit(*psz++);
        if (digit == -1)
            break;
        unsigned char byte = digit << 4;
        digit = HexDigit(*psz++);
        if (digit == -1)
            break;
        result.push_back(byte | digit);
    }
    return result;
}

//! Make sure database has a unique fileid within the environment. If it
//! doesn't, throw an error. BDB caches do not work properly when more than one
//! open database has the same fileid (values written to one database may show
//! up in reads to other databases).
//!
//! BerkeleyDB generates unique fileids by default
//! (https://docs.oracle.com/cd/E17275_01/html/programmer_reference/program_copy.html),
//! so raven should never create different databases with the same fileid, but
//! this error can be triggered if users manually copy database files.
void CheckUniqueFileid(const CDBEnv& env, const std::string& filename, Db& db)
{
    if (env.IsMock()) return;

    u_int8_t fileid[DB_FILE_ID_LEN];
    int ret = db.get_mpf()->get_fileid(fileid);
    if (ret != 0) {
        throw std::runtime_error(strprintf("CDB: Can't open database %s (get_fileid failed with %d)", filename, ret));
    }

    for (const auto& item : env.mapDb) {
        u_int8_t item_fileid[DB_FILE_ID_LEN];
        if (item.second && item.second->get_mpf()->get_fileid(item_fileid) == 0 &&
            memcmp(fileid, item_fileid, sizeof(fileid)) == 0) {
            const char* item_filename = nullptr;
            item.second->get_dbname(&item_filename, nullptr);
            throw std::runtime_error(strprintf("CDB: Can't open database %s (duplicates fileid %s from %s)", filename,
                HexStr(std::begin(item_fileid), std::end(item_fileid)),
                item_filename ? item_filename : "(unknown database)"));
        }
    }
}

void ClearSalvagedData(std::vector<CDBEnv::KeyValPair>& rows)
{
    for (CDBEnv::KeyValPair& row : rows) {
        if (!row.first.empty())
            memory_cleanse(row.first.data(), row.first.size());
        if (!row.second.empty())
            memory_cleanse(row.second.data(), row.second.size());
        CDBEnv::SalvagedBytes().swap(row.first);
        CDBEnv::SalvagedBytes().swap(row.second);
    }
    std::vector<CDBEnv::KeyValPair>().swap(rows);
}

void RemoveRecoveryDatabase(CDBEnv& env, const std::string& filename)
{
    DbTxn* txn = env.TxnBegin(DB_TXN_SYNC);
    if (!txn) {
        LogPrintf("CDB::Recover: Cannot begin cleanup transaction for %s\n", filename);
        return;
    }

    const int removeResult = env.dbenv->dbremove(txn, filename.c_str(), nullptr, 0);
    if (removeResult != 0) {
        txn->abort();
        LogPrintf("CDB::Recover: Cannot remove temporary database %s: %d\n",
                  filename, removeResult);
        return;
    }

    // Berkeley DB invalidates the transaction handle after commit, including
    // on an error return, so never inspect or abort txn beyond this call.
    const int commitResult = txn->commit(DB_TXN_SYNC);
    if (commitResult != 0) {
        LogPrintf("CDB::Recover: Cannot commit cleanup of %s: %d\n",
                  filename, commitResult);
    }
}
} // namespace

//
// CDB
//

CDBEnv bitdb;

void CDBEnv::EnvShutdown()
{
    if (!fDbEnvInit)
        return;

    fDbEnvInit = false;
    int ret = dbenv->close(0);
    if (ret != 0)
        LogPrintf("CDBEnv::EnvShutdown: Error %d shutting down database environment: %s\n", ret, DbEnv::strerror(ret));
    if (!fMockDb)
        DbEnv((u_int32_t)0).remove(strPath.c_str(), 0);
}

void CDBEnv::Reset()
{
    delete dbenv;
    dbenv = new DbEnv(DB_CXX_NO_EXCEPTIONS);
    fDbEnvInit = false;
    fMockDb = false;
}

CDBEnv::CDBEnv() : dbenv(nullptr)
{
    Reset();
}

CDBEnv::~CDBEnv()
{
    EnvShutdown();
    delete dbenv;
    dbenv = nullptr;
}

void CDBEnv::Close()
{
    EnvShutdown();
}

bool CDBEnv::Open(const fs::path& pathIn)
{
    if (fDbEnvInit)
        return true;

    boost::this_thread::interruption_point();

    strPath = pathIn.string();
    fs::path pathLogDir = pathIn / "database";
    TryCreateDirectories(pathLogDir);
    fs::path pathErrorFile = pathIn / "db.log";
    LogPrintf("CDBEnv::Open: LogDir=%s ErrorFile=%s\n", pathLogDir.string(), pathErrorFile.string());

    unsigned int nEnvFlags = 0;
    if (gArgs.GetBoolArg("-privdb", DEFAULT_WALLET_PRIVDB))
        nEnvFlags |= DB_PRIVATE;

    dbenv->set_lg_dir(pathLogDir.string().c_str());
    dbenv->set_cachesize(0, 0x100000, 1); // 1 MiB should be enough for just the wallet
    dbenv->set_lg_bsize(0x10000);
    dbenv->set_lg_max(1048576);
    dbenv->set_lk_max_locks(40000);
    dbenv->set_lk_max_objects(40000);
    dbenv->set_errfile(fsbridge::fopen(pathErrorFile, "a")); /// debug
    dbenv->set_flags(DB_AUTO_COMMIT, 1);
    dbenv->set_flags(DB_TXN_WRITE_NOSYNC, 1);
    dbenv->log_set_config(DB_LOG_AUTO_REMOVE, 1);
    int ret = dbenv->open(strPath.c_str(),
                         DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_RECOVER |
                             nEnvFlags,
                         S_IRUSR | S_IWUSR);
    if (ret != 0) {
        dbenv->close(0);
        Reset();
        return error("CDBEnv::Open: Error %d opening database environment: %s\n", ret, DbEnv::strerror(ret));
    }

    fDbEnvInit = true;
    fMockDb = false;
    return true;
}

void CDBEnv::MakeMock()
{
    if (fDbEnvInit)
        throw std::runtime_error("CDBEnv::MakeMock: Already initialized");

    boost::this_thread::interruption_point();

    LogPrint(BCLog::DB, "CDBEnv::MakeMock\n");

    dbenv->set_cachesize(1, 0, 1);
    dbenv->set_lg_bsize(10485760 * 4);
    dbenv->set_lg_max(10485760);
    dbenv->set_lk_max_locks(10000);
    dbenv->set_lk_max_objects(10000);
    dbenv->set_flags(DB_AUTO_COMMIT, 1);
    dbenv->log_set_config(DB_LOG_IN_MEMORY, 1);
    int ret = dbenv->open(nullptr,
                         DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_PRIVATE,
                         S_IRUSR | S_IWUSR);
    if (ret != 0) {
        dbenv->close(0);
        Reset();
        throw std::runtime_error(strprintf("CDBEnv::MakeMock: Error %d opening database environment.", ret));
    }

    fDbEnvInit = true;
    fMockDb = true;
}

CDBEnv::VerifyResult CDBEnv::Verify(const std::string& strFile, recoverFunc_type recoverFunc, std::string& out_backup_filename)
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    Db db(dbenv, 0);
    int result = db.verify(strFile.c_str(), nullptr, nullptr, 0);
    if (result == 0)
        return VERIFY_OK;
    else if (recoverFunc == nullptr)
        return RECOVER_FAIL;

    // Try to recover:
    bool fRecovered = (*recoverFunc)(strFile, out_backup_filename);
    return (fRecovered ? RECOVER_OK : RECOVER_FAIL);
}

bool CDB::Recover(const std::string& filename, void *callbackDataIn, bool (*recoverKVcallback)(void* callbackData, CDataStream ssKey, CDataStream ssValue), std::string& newFilename)
{
    return RecoverInternal(filename, callbackDataIn, recoverKVcallback,
                           newFilename, nullptr);
}

bool CDB::RecoverInternal(const std::string& filename,
                          void* callbackDataIn,
                          bool (*recoverKVcallback)(void*, CDataStream, CDataStream),
                          std::string& newFilename,
                          const RecoveryTestOptions* testOptions)
{
    LOCK(bitdb.cs_db);
    newFilename.clear();

    const auto inUse = bitdb.mapFileUseCount.find(filename);
    if (inUse != bitdb.mapFileUseCount.end() && inUse->second != 0) {
        LogPrintf("CDB::Recover: Refusing to recover open database %s\n", filename);
        return false;
    }
    if (!bitdb.CloseDb(filename)) {
        LogPrintf("CDB::Recover: Cannot close source database %s\n", filename);
        return false;
    }
    bitdb.mapFileUseCount.erase(filename);

    std::vector<CDBEnv::KeyValPair> salvagedData;
    std::unique_ptr<Db> recoveryDb;
    DbTxn* activeTxn = nullptr;
    bool recoveryDbOpen = false;
    bool recoveryDbCreated = false;
    std::string recoveryFilename;

    auto closeRecoveryDb = [&]() {
        if (!recoveryDb || !recoveryDbOpen)
            return 0;
        const int result = recoveryDb->close(0);
        recoveryDbOpen = false;
        return result;
    };
    auto failRecovery = [&]() {
        if (activeTxn) {
            activeTxn->abort();
            activeTxn = nullptr;
        }
        closeRecoveryDb();
        recoveryDb.reset();
        if (recoveryDbCreated)
            RemoveRecoveryDatabase(bitdb, recoveryFilename);
        ClearSalvagedData(salvagedData);
        newFilename.clear();
        return false;
    };

    try {
        CDBEnv::SalvageResult salvageResult =
            bitdb.Salvage(filename, true, salvagedData);
        if (testOptions && testOptions->force_partial_salvage &&
            salvageResult == CDBEnv::SalvageResult::COMPLETE) {
            salvageResult = CDBEnv::SalvageResult::PARTIAL;
        }
        if (salvageResult == CDBEnv::SalvageResult::FAILED || salvagedData.empty()) {
            LogPrintf("CDB::Recover: Aggressive salvage found no usable records in %s\n",
                      filename);
            return failRecovery();
        }

        LogPrintf("CDB::Recover: Aggressive salvage found %u records%s\n",
                  salvagedData.size(),
                  salvageResult == CDBEnv::SalvageResult::PARTIAL ? " (partial)" : "");

        const std::string recoveryToken = GetRandHash().GetHex();
        recoveryFilename = testOptions && !testOptions->temp_filename.empty()
            ? testOptions->temp_filename
            : strprintf("%s.recover.%s", filename, recoveryToken);
        const std::string backupFilename =
            testOptions && !testOptions->backup_filename.empty()
                ? testOptions->backup_filename
                : strprintf("%s.%d.%s.bak", filename, GetTime(),
                            recoveryToken.substr(0, 16));

        recoveryDb.reset(new Db(bitdb.dbenv, 0));
        const int openResult = recoveryDb->open(nullptr,
                                                recoveryFilename.c_str(),
                                                "main",
                                                DB_BTREE,
                                                DB_CREATE | DB_EXCL | DB_AUTO_COMMIT,
                                                0);
        if (openResult != 0) {
            LogPrintf("CDB::Recover: Cannot create exclusive temporary database %s: %d\n",
                      recoveryFilename, openResult);
            recoveryDb->close(0);
            recoveryDb.reset();
            return failRecovery();
        }
        recoveryDbOpen = true;
        recoveryDbCreated = true;

        activeTxn = testOptions &&
                            testOptions->fault == RecoveryFault::NULL_WRITE_TRANSACTION
            ? nullptr
            : bitdb.TxnBegin(DB_TXN_SYNC);
        if (!activeTxn) {
            LogPrintf("CDB::Recover: Cannot begin temporary database transaction\n");
            return failRecovery();
        }

        auto putRow = [&](CDBEnv::KeyValPair& row, u_int32_t flags) {
            if (row.first.size() > std::numeric_limits<u_int32_t>::max() ||
                row.second.size() > std::numeric_limits<u_int32_t>::max()) {
                return EINVAL;
            }
            Dbt datKey(row.first.empty() ? nullptr : row.first.data(),
                       static_cast<u_int32_t>(row.first.size()));
            Dbt datValue(row.second.empty() ? nullptr : row.second.data(),
                         static_cast<u_int32_t>(row.second.size()));
            return recoveryDb->put(activeTxn, &datKey, &datValue, flags);
        };

        if (testOptions && testOptions->duplicate_first_row) {
            const int injectedPut = putRow(salvagedData.front(), 0);
            if (injectedPut != 0) {
                LogPrintf("CDB::Recover: Cannot prepare duplicate-row regression: %d\n",
                          injectedPut);
                return failRecovery();
            }
        }

        size_t rowsWritten = 0;
        for (CDBEnv::KeyValPair& row : salvagedData) {
            if (row.first.size() > std::numeric_limits<u_int32_t>::max() ||
                row.second.size() > std::numeric_limits<u_int32_t>::max()) {
                LogPrintf("CDB::Recover: Salvaged row exceeds Berkeley DB size limits\n");
                return failRecovery();
            }

            if (recoverKVcallback) {
                CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                if (!row.first.empty()) {
                    ssKey.write(reinterpret_cast<const char*>(row.first.data()),
                                row.first.size());
                }
                if (!row.second.empty()) {
                    ssValue.write(reinterpret_cast<const char*>(row.second.data()),
                                  row.second.size());
                }
                if (!(*recoverKVcallback)(callbackDataIn, ssKey, ssValue))
                    continue;
            }

            const int putResult = putRow(row, DB_NOOVERWRITE);
            if (putResult != 0) {
                LogPrintf("CDB::Recover: Cannot write salvaged row: %d\n", putResult);
                return failRecovery();
            }
            ++rowsWritten;
        }

        if (rowsWritten == 0) {
            LogPrintf("CDB::Recover: Recovery filter retained no records\n");
            return failRecovery();
        }

        int writeCommitResult;
        if (testOptions && testOptions->fault == RecoveryFault::WRITE_COMMIT) {
            activeTxn->abort();
            activeTxn = nullptr;
            writeCommitResult = DB_RUNRECOVERY;
        } else {
            writeCommitResult = activeTxn->commit(DB_TXN_SYNC);
            activeTxn = nullptr;
        }
        if (writeCommitResult != 0) {
            LogPrintf("CDB::Recover: Cannot commit temporary database: %d\n",
                      writeCommitResult);
            return failRecovery();
        }

        const int actualCloseResult = closeRecoveryDb();
        const int closeResult = testOptions &&
                                        testOptions->fault == RecoveryFault::TEMP_CLOSE
            ? DB_RUNRECOVERY
            : actualCloseResult;
        recoveryDb.reset();
        if (closeResult != 0) {
            LogPrintf("CDB::Recover: Cannot close temporary database: %d\n", closeResult);
            return failRecovery();
        }

        // No plaintext row is needed after the checked temporary database is
        // closed. Release it before the namespace transaction is attempted.
        ClearSalvagedData(salvagedData);

        activeTxn = bitdb.TxnBegin(DB_TXN_SYNC);
        if (!activeTxn) {
            LogPrintf("CDB::Recover: Cannot begin installation transaction\n");
            return failRecovery();
        }

        const int backupRenameResult = bitdb.dbenv->dbrename(
            activeTxn, filename.c_str(), nullptr, backupFilename.c_str(), 0);
        const int installRenameResult = backupRenameResult != 0
            ? backupRenameResult
            : (testOptions && testOptions->fault == RecoveryFault::SECOND_RENAME
                   ? DB_RUNRECOVERY
                   : bitdb.dbenv->dbrename(activeTxn, recoveryFilename.c_str(),
                                           nullptr, filename.c_str(), 0));
        if (backupRenameResult != 0 || installRenameResult != 0) {
            activeTxn->abort();
            activeTxn = nullptr;
            LogPrintf("CDB::Recover: Atomic installation rename failed: %d/%d\n",
                      backupRenameResult, installRenameResult);
            return failRecovery();
        }

        int installCommitResult;
        if (testOptions && testOptions->fault == RecoveryFault::INSTALL_COMMIT) {
            activeTxn->abort();
            activeTxn = nullptr;
            installCommitResult = DB_RUNRECOVERY;
        } else {
            installCommitResult = activeTxn->commit(DB_TXN_SYNC);
            activeTxn = nullptr;
        }
        if (installCommitResult != 0) {
            LogPrintf("CDB::Recover: Cannot commit atomic installation: %d\n",
                      installCommitResult);
            return failRecovery();
        }

        recoveryDbCreated = false;
        newFilename = backupFilename;
        LogPrintf("CDB::Recover: Installed recovered %s and retained original as %s\n",
                  filename, newFilename);
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CDB::Recover: Exception while recovering %s: %s\n",
                  filename, e.what());
        return failRecovery();
    } catch (...) {
        LogPrintf("CDB::Recover: Unknown exception while recovering %s\n", filename);
        return failRecovery();
    }
}

bool CDB::VerifyEnvironment(const std::string& walletFile, const fs::path& dataDir, std::string& errorStr)
{
    LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
    LogPrintf("Using wallet %s\n", walletFile);

    // Wallet file must be a plain filename without a directory
    if (walletFile != fs::basename(walletFile) + fs::extension(walletFile))
    {
        errorStr = strprintf(_("Wallet %s resides outside data directory %s"), walletFile, dataDir.string());
        return false;
    }

    if (!bitdb.Open(dataDir))
    {
        // try moving the database env out of the way
        fs::path pathDatabase = dataDir / "database";
        fs::path pathDatabaseBak = dataDir / strprintf("database.%d.bak", GetTime());
        try {
            fs::rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        } catch (const fs::filesystem_error&) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!bitdb.Open(dataDir)) {
            // if it still fails, it probably means we can't even create the database env
            errorStr = strprintf(_("Error initializing wallet database environment %s!"), GetDataDir());
            return false;
        }
    }
    return true;
}

bool CDB::VerifyDatabaseFile(const std::string& walletFile, const fs::path& dataDir, std::string& warningStr, std::string& errorStr, CDBEnv::recoverFunc_type recoverFunc)
{
    if (fs::exists(dataDir / walletFile))
    {
        std::string backup_filename;
        CDBEnv::VerifyResult r = bitdb.Verify(walletFile, recoverFunc, backup_filename);
        if (r == CDBEnv::RECOVER_OK)
        {
            warningStr = strprintf(_("Warning: Wallet file corrupt, data salvaged!"
                                     " Original %s saved as %s in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."),
                                   walletFile, backup_filename, dataDir);
        }
        if (r == CDBEnv::RECOVER_FAIL)
        {
            errorStr = strprintf(_("%s corrupt, salvage failed"), walletFile);
            return false;
        }
    }
    // also return true if files does not exists
    return true;
}

/* End of headers, beginning of key/value data */
static const char *HEADER_END = "HEADER=END";
/* End of key/value data */
static const char *DATA_END = "DATA=END";

CDBEnv::SalvageResult CDBEnv::Salvage(const std::string& strFile, bool fAggressive, std::vector<CDBEnv::KeyValPair>& vResult)
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    u_int32_t flags = DB_SALVAGE;
    if (fAggressive)
        flags |= DB_AGGRESSIVE;

    // Berkeley DB renders the entire wallet as hex. The locked secure allocator
    // cannot hold large wallets, so use a cleanse-on-free allocator here.
    SalvageStream strDump;

    Db db(dbenv, 0);
    int result = db.verify(strFile.c_str(), nullptr, &strDump, flags);
    if (result == DB_VERIFY_BAD) {
        LogPrintf("CDBEnv::Salvage: Database salvage found errors, all data may not be recoverable.\n");
        if (!fAggressive) {
            LogPrintf("CDBEnv::Salvage: Rerun with aggressive mode to ignore errors and continue.\n");
            return SalvageResult::FAILED;
        }
    }
    if (result != 0 && result != DB_VERIFY_BAD) {
        LogPrintf("CDBEnv::Salvage: Database salvage failed with result %d.\n", result);
        return SalvageResult::FAILED;
    }

    // Format of bdb dump is ascii lines:
    // header lines...
    // HEADER=END
    //  hexadecimal key
    //  hexadecimal value
    //  ... repeated
    // DATA=END

    SalvageString strLine;
    SalvageString keyHex, valueHex;
    struct LineCleaner {
        SalvageString& header;
        SalvageString& key;
        SalvageString& value;
        ~LineCleaner()
        {
            ClearSalvageString(header);
            ClearSalvageString(key);
            ClearSalvageString(value);
        }
    } cleanLines{strLine, keyHex, valueHex};
    while (!strDump.eof() && strLine != HEADER_END)
        getline(strDump, strLine); // Skip past header

    while (!strDump.eof() && keyHex != DATA_END) {
        getline(strDump, keyHex);
        if (keyHex != DATA_END) {
            if (strDump.eof())
                break;
            getline(strDump, valueHex);
            if (valueHex == DATA_END) {
                LogPrintf("CDBEnv::Salvage: WARNING: Number of keys in data does not match number of values.\n");
                break;
            }
            vResult.emplace_back(ParseSalvageHex(keyHex), ParseSalvageHex(valueHex));
        }
    }

    if (keyHex != DATA_END) {
        LogPrintf("CDBEnv::Salvage: WARNING: Unexpected end of file while reading salvage output.\n");
        return SalvageResult::FAILED;
    }

    return result == DB_VERIFY_BAD ? SalvageResult::PARTIAL
                                   : SalvageResult::COMPLETE;
}


void CDBEnv::CheckpointLSN(const std::string& strFile)
{
    dbenv->txn_checkpoint(0, 0, 0);
    if (fMockDb)
        return;
    dbenv->lsn_reset(strFile.c_str(), 0);
}


CDB::CDB(CWalletDBWrapper& dbw, const char* pszMode, bool fFlushOnCloseIn) : pdb(nullptr), activeTxn(nullptr)
{
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    fFlushOnClose = fFlushOnCloseIn;
    env = dbw.env;
    if (dbw.IsDummy()) {
        return;
    }
    const std::string &strFilename = dbw.strFile;

    bool fCreate = strchr(pszMode, 'c') != nullptr;
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK(env->cs_db);
        if (!env->Open(GetDataDir()))
            throw std::runtime_error("CDB: Failed to open database environment.");

        pdb = env->mapDb[strFilename];
        if (pdb == nullptr) {
            int ret;
            std::unique_ptr<Db> pdb_temp(new Db(env->dbenv, 0));

            bool fMockDb = env->IsMock();
            if (fMockDb) {
                DbMpoolFile* mpf = pdb_temp->get_mpf();
                ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
                if (ret != 0) {
                    throw std::runtime_error(strprintf("CDB: Failed to configure for no temp file backing for database %s", strFilename));
                }
            }

            ret = pdb_temp->open(nullptr,                             // Txn pointer
                            fMockDb ? nullptr : strFilename.c_str(),  // Filename
                            fMockDb ? strFilename.c_str() : "main",   // Logical db name
                            DB_BTREE,                                 // Database type
                            nFlags,                                   // Flags
                            0);

            if (ret != 0) {
                throw std::runtime_error(strprintf("CDB: Error %d, can't open database %s", ret, strFilename));
            }
            CheckUniqueFileid(*env, strFilename, *pdb_temp);

            pdb = pdb_temp.release();
            env->mapDb[strFilename] = pdb;

            if (fCreate && !Exists(std::string("version"))) {
                bool fTmp = fReadOnly;
                fReadOnly = false;
                WriteVersion(CLIENT_VERSION);
                fReadOnly = fTmp;
            }
        }
        ++env->mapFileUseCount[strFilename];
        strFile = strFilename;
    }
}

void CDB::Flush()
{
    if (activeTxn)
        return;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
        nMinutes = 1;

    env->dbenv->txn_checkpoint(nMinutes ? gArgs.GetArg("-dblogsize", DEFAULT_WALLET_DBLOGSIZE) * 1024 : 0, nMinutes, 0);
}

void CWalletDBWrapper::IncrementUpdateCounter()
{
    ++nUpdateCounter;
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (activeTxn)
        activeTxn->abort();
    activeTxn = nullptr;
    pdb = nullptr;

    if (fFlushOnClose)
        Flush();

    {
        LOCK(env->cs_db);
        --env->mapFileUseCount[strFile];
    }
}

bool CDBEnv::CloseDb(const std::string& strFile)
{
    LOCK(cs_db);
    if (mapDb[strFile] == nullptr)
        return true;

    // Berkeley DB invalidates a handle after close even on an error return.
    Db* pdb = mapDb[strFile];
    const int result = pdb->close(0);
    delete pdb;
    mapDb[strFile] = nullptr;
    if (result != 0) {
        LogPrintf("CDBEnv::CloseDb: Error %d closing database %s\n",
                  result, strFile);
        return false;
    }
    return true;
}

bool CDB::Rewrite(CWalletDBWrapper& dbw, const char* pszSkip)
{
    if (dbw.IsDummy()) {
        return true;
    }
    CDBEnv *env = dbw.env;
    const std::string& strFile = dbw.strFile;
    while (true) {
        {
            LOCK(env->cs_db);
            if (!env->mapFileUseCount.count(strFile) || env->mapFileUseCount[strFile] == 0) {
                // Flush log data to the dat file
                env->CloseDb(strFile);
                env->CheckpointLSN(strFile);
                env->mapFileUseCount.erase(strFile);

                bool fSuccess = true;
                LogPrintf("CDB::Rewrite: Rewriting %s...\n", strFile);
                std::string strFileRes = strFile + ".rewrite";
                const fs::path pathRewrite = GetDataDir() / strFileRes;
                try {
                    const fs::file_status rewriteStatus = fs::symlink_status(pathRewrite);
                    if (fs::exists(rewriteStatus)) {
                        // The source still exists at this point, so a regular
                        // temporary database can only be stale. Never follow
                        // or remove an unexpected symlink/directory.
                        if (fs::is_symlink(rewriteStatus) ||
                            !fs::is_regular_file(rewriteStatus)) {
                            LogPrintf("CDB::Rewrite: Refusing stale non-regular path %s\n",
                                      pathRewrite.string());
                            return false;
                        }

                        // Keep stale-file cleanup inside Berkeley DB as well.
                        // Renaming or unlinking an environment database behind
                        // Berkeley DB's back can invalidate its recovery state.
                        if (env->mapFileUseCount.count(strFileRes) &&
                            env->mapFileUseCount[strFileRes] != 0) {
                            LogPrintf("CDB::Rewrite: Stale database file is still in use %s\n",
                                      strFileRes);
                            return false;
                        }
                        env->CloseDb(strFileRes);
                        env->mapFileUseCount.erase(strFileRes);
                        DbTxn* cleanupTxn = env->TxnBegin();
                        if (!cleanupTxn) {
                            return false;
                        }
                        if (env->dbenv->dbremove(cleanupTxn, strFileRes.c_str(), nullptr, 0) != 0) {
                            cleanupTxn->abort();
                            LogPrintf("CDB::Rewrite: Can't remove stale database file %s\n",
                                      strFileRes);
                            return false;
                        }
                        if (cleanupTxn->commit(DB_TXN_SYNC) != 0) {
                            LogPrintf("CDB::Rewrite: Can't commit removal of stale database file %s\n",
                                      strFileRes);
                            return false;
                        }
                    }
                } catch (const fs::filesystem_error& e) {
                    LogPrintf("CDB::Rewrite: Can't clear stale database file %s: %s\n",
                              pathRewrite.string(), e.what());
                    return false;
                }
                { // surround usage of db with extra {}
                    CDB db(dbw, "r");
                    Db* pdbCopy = new Db(env->dbenv, 0);

                    int ret = pdbCopy->open(nullptr,               // Txn pointer
                                            strFileRes.c_str(), // Filename
                                            "main",             // Logical db name
                                            DB_BTREE,           // Database type
                                            DB_CREATE,          // Flags
                                            0);
                    if (ret != 0) {
                        LogPrintf("CDB::Rewrite: Can't create database file %s\n", strFileRes);
                        fSuccess = false;
                    }

                    if (fSuccess) {
                        u_int32_t pageSize = 0;
                        u_int32_t clearLength = 0;
                        DbMpoolFile* mpf = pdbCopy->get_mpf();
                        const int pageResult = pdbCopy->get_pagesize(&pageSize);
                        const int clearResult = mpf ? mpf->get_clear_len(&clearLength) : EINVAL;
                        // Berkeley DB treats an unset clear length as a full page.
                        if (pageResult != 0 || clearResult != 0 || pageSize == 0 ||
                            (clearLength != std::numeric_limits<u_int32_t>::max() &&
                             clearLength != pageSize)) {
                            LogPrintf("CDB::Rewrite: Refusing rewrite without full-page clearing "
                                      "(page result %d, clear result %d, page size %u, clear length %u)\n",
                                      pageResult, clearResult, pageSize, clearLength);
                            fSuccess = false;
                        }
                    }

                    Dbc* pcursor = fSuccess ? db.GetCursor() : nullptr;
                    bool fReachedEnd = false;
                    if (!pcursor)
                        fSuccess = false;
                    while (fSuccess) {
                        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                        int ret1 = db.ReadAtCursor(pcursor, ssKey, ssValue);
                        if (ret1 == DB_NOTFOUND) {
                            fReachedEnd = true;
                            break;
                        } else if (ret1 != 0) {
                            fSuccess = false;
                            break;
                        }
                        if (pszSkip &&
                            strncmp(ssKey.data(), pszSkip, std::min(ssKey.size(), strlen(pszSkip))) == 0)
                            continue;
                        if (strncmp(ssKey.data(), "\x07version", 8) == 0) {
                            // Update version:
                            ssValue.clear();
                            ssValue << CLIENT_VERSION;
                        }
                        Dbt datKey(ssKey.data(), ssKey.size());
                        Dbt datValue(ssValue.data(), ssValue.size());
                        int ret2 = pdbCopy->put(nullptr, &datKey, &datValue, DB_NOOVERWRITE);
                        if (ret2 != 0)
                            fSuccess = false;
                    }
                    if (pcursor && pcursor->close() != 0)
                        fSuccess = false;
                    if (!fReachedEnd)
                        fSuccess = false;
                    if (fSuccess) {
                        db.Close();
                        env->CloseDb(strFile);
                        if (pdbCopy->close(0))
                            fSuccess = false;
                    } else {
                        pdbCopy->close(0);
                    }
                    delete pdbCopy;
                }
                if (fSuccess) {
                    // Keep the namespace update inside one durable Berkeley
                    // DB transaction. A crash leaves either the complete
                    // source or the complete rewritten database at the
                    // configured path after Berkeley DB recovery; there is no
                    // non-transactional remove/rename gap.
                    DbTxn* ptxn = env->TxnBegin();
                    if (!ptxn) {
                        fSuccess = false;
                    } else {
                        const int removeResult =
                            env->dbenv->dbremove(ptxn, strFile.c_str(), nullptr, 0);
                        const int renameResult = removeResult == 0
                            ? env->dbenv->dbrename(ptxn, strFileRes.c_str(), nullptr,
                                                  strFile.c_str(), 0)
                            : removeResult;
                        if (removeResult != 0 || renameResult != 0) {
                            ptxn->abort();
                            fSuccess = false;
                        } else if (ptxn->commit(DB_TXN_SYNC) != 0) {
                            fSuccess = false;
                        }
                    }
                }
                if (!fSuccess)
                    LogPrintf("CDB::Rewrite: Failed to rewrite database file %s\n", strFileRes);
                return fSuccess;
            }
        }
        MilliSleep(100);
    }
}


void CDBEnv::Flush(bool fShutdown)
{
    int64_t nStart = GetTimeMillis();
    // Flush log data to the actual data file on all files that are not in use
    LogPrint(BCLog::DB, "CDBEnv::Flush: Flush(%s)%s\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " database not started");
    if (!fDbEnvInit)
        return;
    {
        LOCK(cs_db);
        std::map<std::string, int>::iterator mi = mapFileUseCount.begin();
        while (mi != mapFileUseCount.end()) {
            std::string strFile = (*mi).first;
            int nRefCount = (*mi).second;
            LogPrint(BCLog::DB, "CDBEnv::Flush: Flushing %s (refcount = %d)...\n", strFile, nRefCount);
            if (nRefCount == 0) {
                // Move log data to the dat file
                CloseDb(strFile);
                LogPrint(BCLog::DB, "CDBEnv::Flush: %s checkpoint\n", strFile);
                dbenv->txn_checkpoint(0, 0, 0);
                LogPrint(BCLog::DB, "CDBEnv::Flush: %s detach\n", strFile);
                if (!fMockDb)
                    dbenv->lsn_reset(strFile.c_str(), 0);
                LogPrint(BCLog::DB, "CDBEnv::Flush: %s closed\n", strFile);
                mapFileUseCount.erase(mi++);
            } else
                mi++;
        }
        LogPrint(BCLog::DB, "CDBEnv::Flush: Flush(%s)%s took %15dms\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " database not started", GetTimeMillis() - nStart);
        if (fShutdown) {
            char** listp;
            if (mapFileUseCount.empty()) {
                dbenv->log_archive(&listp, DB_ARCH_REMOVE);
                Close();
                if (!fMockDb)
                    fs::remove_all(fs::path(strPath) / "database");
            }
        }
    }
}

bool CDB::PeriodicFlush(CWalletDBWrapper& dbw)
{
    if (dbw.IsDummy()) {
        return true;
    }
    bool ret = false;
    CDBEnv *env = dbw.env;
    const std::string& strFile = dbw.strFile;
    TRY_LOCK(bitdb.cs_db,lockDb);
    if (lockDb)
    {
        // Don't do this if any databases are in use
        int nRefCount = 0;
        std::map<std::string, int>::iterator mit = env->mapFileUseCount.begin();
        while (mit != env->mapFileUseCount.end())
        {
            nRefCount += (*mit).second;
            mit++;
        }

        if (nRefCount == 0)
        {
            boost::this_thread::interruption_point();
            std::map<std::string, int>::iterator mi = env->mapFileUseCount.find(strFile);
            if (mi != env->mapFileUseCount.end())
            {
                LogPrint(BCLog::DB, "Flushing %s\n", strFile);
                int64_t nStart = GetTimeMillis();

                // Flush wallet file so it's self contained
                env->CloseDb(strFile);
                env->CheckpointLSN(strFile);

                env->mapFileUseCount.erase(mi++);
                LogPrint(BCLog::DB, "Flushed %s %dms\n", strFile, GetTimeMillis() - nStart);
                ret = true;
            }
        }
    }

    return ret;
}

bool CWalletDBWrapper::Rewrite(const char* pszSkip)
{
    return CDB::Rewrite(*this, pszSkip);
}

bool CWalletDBWrapper::Backup(const std::string& strDest)
{
    if (IsDummy()) {
        return false;
    }
    while (true)
    {
        {
            LOCK(env->cs_db);
            if (!env->mapFileUseCount.count(strFile) || env->mapFileUseCount[strFile] == 0)
            {
                // Flush log data to the dat file
                env->CloseDb(strFile);
                env->CheckpointLSN(strFile);
                env->mapFileUseCount.erase(strFile);

                // Copy wallet file
                fs::path pathSrc = GetDataDir() / strFile;
                fs::path pathDest(strDest);
                if (fs::is_directory(pathDest))
                    pathDest /= strFile;

                try {
                    fs::copy_file(pathSrc, pathDest, fs::copy_option::overwrite_if_exists);
                    LogPrintf("copied %s to %s\n", strFile, pathDest.string());
                    return true;
                } catch (const fs::filesystem_error& e) {
                    LogPrintf("error copying %s to %s - %s\n", strFile, pathDest.string(), e.what());
                    return false;
                }
            }
        }
        MilliSleep(100);
    }
}

void CWalletDBWrapper::Flush(bool shutdown)
{
    if (!IsDummy()) {
        env->Flush(shutdown);
    }
}
