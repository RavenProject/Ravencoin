// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_MINEABLEDB_H
#define RAVENCOIN_MINEABLEDB_H

#include "assets/mineable.h"
#include <dbwrapper.h>
#include <string>
#include <vector>

class CMineableAssetsDB : public CDBWrapper
{
public:
    explicit CMineableAssetsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMineableAssetsDB(const CMineableAssetsDB&) = delete;
    CMineableAssetsDB& operator=(const CMineableAssetsDB&) = delete;

    bool WriteSchedule(const CMineableSchedule& schedule);
    bool ReadSchedule(const std::string& assetName, CMineableSchedule& schedule);
    bool EraseSchedule(const std::string& assetName);
    bool ListSchedules(std::vector<CMineableSchedule>& schedules);

    bool Flush();
};

#endif // RAVENCOIN_MINEABLEDB_H
