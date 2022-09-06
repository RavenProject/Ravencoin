// Copyright (c) 2022 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_CSNAPSHOTDB_H
#define RAVENCOIN_CSNAPSHOTDB_H

#include "dbwrapper.h"
#include <boost/filesystem/path.hpp>
#include <amount.h>

class UniValue;
class CCoinsViewCache;

// A Snapshot will be created after the first block that is mined after the given timestamp.
class CSnapshot
{

public:
    CSnapshot()
    {
        SetNull();
    }

    void SetNull()
    {
        nSnapshotTimeOrHeight = 0;
        nActualSnapshotHeight = 0;
        mapBalances.clear();
    }

    int64_t nSnapshotTimeOrHeight;
    int32_t nActualSnapshotHeight;
    std::map<std::string,CAmount> mapBalances;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nSnapshotTimeOrHeight);
        READWRITE(nActualSnapshotHeight);
        READWRITE(mapBalances);
    }
};

UniValue PrettyPrintSnapshot(const int64_t& nTimeOrHeight);
void CheckForSnapshot(CCoinsViewCache *);
void CreateSnapshot(const int64_t& nTime, CCoinsViewCache *);

class CSnapshotDB : public CDBWrapper
{

public:
    CSnapshotDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CSnapshotDB(const CSnapshotDB&);
    void operator=(const CSnapshotDB&);

public:
    bool WriteRavenSnapshot(const CSnapshot& snapshot);
    bool ReadRavenSnapshot(const int64_t& nTime, CSnapshot &snapshot);
};


#endif //RAVENCOIN_CSNAPSHOTDB_H