// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_RewardS_H
#define RAVENCOIN_RewardS_H

#include <dbwrapper.h>

class CRewardsDB  : public CDBWrapper {
public:
    explicit CRewardsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CRewardsDB(const CRewardsDB&) = delete;
    CRewardsDB& operator=(const CRewardsDB&) = delete;

    // My snapshot checks
    bool WriteSnapshotCheck(const std::string& asset_name, const int& block_height);
    bool ReadSnapshotCheck(const int& block_height, std::set<std::string>& setAssetNames);


//    bool WriteMyMessageChannel(const std::string& channelname);
//    bool ReadMyMessageChannel(const std::string& channelname);
//    bool EraseMyMessageChannel(const std::string& channelname);
//    bool LoadMyMessageChannels(std::set<std::string>& setChannels);
//
//    bool WriteUsedAddress(const std::string& address);
//    bool ReadUsedAddress(const std::string& address);
//    bool EraseUsedAddress(const std::string& address);

//    // Write / Read Database flags
//    bool WriteFlag(const std::string &name, bool fValue);
//    bool ReadFlag(const std::string &name, bool &fValue);

    bool Flush();
};


#endif //RAVENCOIN_RewardS_H
