// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rewardsdb.h"

static const char SNAPSHOTCHECK_FLAG = 'C'; // Snapshot Check

CRewardsDB::CRewardsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "Rewards", nCacheSize, fMemory, fWipe) {
}

bool CRewardsDB::WriteSnapshotCheck(const std::string& asset_name, const int& block_height)
{
    std::set<std::string> setAssetNames;
    ReadSnapshotCheck(block_height, setAssetNames);
    setAssetNames.insert(asset_name);

    return Write(std::make_pair(SNAPSHOTCHECK_FLAG, block_height), setAssetNames);
}

bool CRewardsDB::ReadSnapshotCheck(const int& block_height, std::set<std::string>& setAssetNames)
{
    return Read(std::make_pair(SNAPSHOTCHECK_FLAG, block_height), setAssetNames);
}