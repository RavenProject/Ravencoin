// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/mineabledb.h"
#include "validation.h"

static const char SCHEDULE_FLAG = 'M';

CMineableAssetsDB::CMineableAssetsDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "assets" / "mineable", nCacheSize, fMemory, fWipe)
{
}

bool CMineableAssetsDB::WriteSchedule(const CMineableSchedule& schedule)
{
    return Write(std::make_pair(SCHEDULE_FLAG, schedule.strMineableAsset), schedule);
}

bool CMineableAssetsDB::ReadSchedule(const std::string& assetName, CMineableSchedule& schedule)
{
    return Read(std::make_pair(SCHEDULE_FLAG, assetName), schedule);
}

bool CMineableAssetsDB::EraseSchedule(const std::string& assetName)
{
    return Erase(std::make_pair(SCHEDULE_FLAG, assetName));
}

bool CMineableAssetsDB::ListSchedules(std::vector<CMineableSchedule>& schedules)
{
    schedules.clear();
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(SCHEDULE_FLAG, std::string()));

    while (pcursor->Valid()) {
        std::pair<char, std::string> key;
        if (!pcursor->GetKey(key) || key.first != SCHEDULE_FLAG)
            break;
        CMineableSchedule schedule;
        if (!pcursor->GetValue(schedule))
            return false;
        if (schedule.fActive)
            schedules.push_back(schedule);
        pcursor->Next();
    }
    return true;
}

bool CMineableAssetsDB::Flush()
{
    return CDBWrapper::Flush();
}
