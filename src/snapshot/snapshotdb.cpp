// Copyright (c) 2022 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <bits/stdc++.h>
#include "snapshotdb.h"
#include <boost/filesystem.hpp>
#include "univalue.h"
#include "rpc/server.h"
#include "validation.h"
#include "core_io.h"
#include "base58.h"


static const char DB_SNAPSHOT = 's';

CSnapshotDB::CSnapshotDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "snapshots", nCacheSize, fMemory, fWipe) {}


bool CSnapshotDB::WriteRavenSnapshot(const CSnapshot &snapshot)
{
    LogPrintf("Snapshot: Wrote snapshot to database at height %d\n", snapshot.nActualSnapshotHeight);
    return Write(std::make_pair(DB_SNAPSHOT, snapshot.nSnapshotTimeOrHeight), snapshot);
}

bool CSnapshotDB::ReadRavenSnapshot(const int64_t& nTimeOrHeight, CSnapshot &snapshot)
{
    LogPrintf("Snapshot: Reading snapshot from database at height/time = %d\n", nTimeOrHeight);
    return Read(std::make_pair(DB_SNAPSHOT, nTimeOrHeight), snapshot);
}

UniValue PrettyPrintSnapshot(const int64_t& nTimeOrHeight)
{
    CSnapshot snapshot;
    snapshot.nSnapshotTimeOrHeight = nTimeOrHeight;

    if (pSnapshotDB && pSnapshotDB->ReadRavenSnapshot(nTimeOrHeight, snapshot)) {
        UniValue data(UniValue::VOBJ);

        data.pushKV("timeorheightgiven", nTimeOrHeight);
        data.pushKV("actualheight", snapshot.nActualSnapshotHeight);

        UniValue balances(UniValue::VOBJ);
        LogPrintf("Snapshot: Printing %d address balances\n", snapshot.mapBalances.size());
        float iPercent = 0;
        float i = 0;
        for (const auto& pair: snapshot.mapBalances) {
            std::vector<unsigned char> charhash;
            if (DecodeBase58(pair.first, charhash)) {   // this should always be true
                balances.pushKV(pair.first, HexStr(charhash.begin(), charhash.end())+", "+ValueFromAmountString(pair.second, 8));
            } else {
                balances.pushKV(pair.first, " , "+ValueFromAmountString(pair.second, 8));
            }
            i++;
            if (fmod(i, 25000.0) == 0) {
                iPercent = i / float(snapshot.mapBalances.size());
                LogPrintf("Snapshot Printing Progress: %f%%\n", iPercent * 100);
            }
        }

        data.pushKV("balances", balances);
        LogPrintStr("Snapshot Printing Progress: 100%\n");

        return data;
    }

    return NullUniValue;
}

void CheckForSnapshot(CCoinsViewCache* coinsview)
{
    if (!pSnapshotDB) {
        return;
    }

    // Check if snapshot timestamp was set
    int64_t nSnapshotArg = gArgs.GetArg("-snapshot", 0);
    if (nSnapshotArg <= 0) {
        return;
    }

    // Check if snapshot at this timestamp is already completed
    if (pSnapshotDB->Exists(std::make_pair(DB_SNAPSHOT, nSnapshotArg))) {
        return;
    }

    {
        // Locking as we are accessing chainActive
        LOCK(cs_main);

        // nSnapshotTime could be a block height or a timestamp
        if (nSnapshotArg < 1000000000) {
            // We are looking at block heights only
            int64_t nCurrentHeight = chainActive.Height();
            if (nCurrentHeight != nSnapshotArg) {
                return;
            }
        } else {
            // We are looking at timestamps only
            int64_t tipTime = chainActive.Tip()->nTime;
            int64_t nPrevTime = chainActive[chainActive.Height() - 1]->nTime;

            // The snap shot time must be greater than the block two blocks ago but less that the current tip
            if (nSnapshotArg < nPrevTime) {
                return;
            }

            if (nSnapshotArg > tipTime) {
                return;
            }
        }
    }

    // If you made it this far, create the snapshot
    CreateSnapshot(nSnapshotArg, coinsview);
}

void CreateSnapshot(const int64_t& nTimeOrHeight, CCoinsViewCache* coinsview)
{

    CSnapshot snapshot;
    snapshot.nSnapshotTimeOrHeight = nTimeOrHeight;

    // Set the block height
    {
        LOCK(cs_main);
        snapshot.nActualSnapshotHeight = chainActive.Height();
    }

    // Flush the state to before accessing the coins database.
    // This will ensure all data is as valid as it can be
    FlushStateToDisk();

    {
        LOCK(cs_main);
        coinsview->GetAllBalances(snapshot.mapBalances);
    }

    if(pSnapshotDB->WriteRavenSnapshot(snapshot)) {
        LogPrintf("Snapshot: Wrote snapshot to db\n");
    } else {
        LogPrintf("Snapshot: Failed to write snapshot to db\n");
    }
}
