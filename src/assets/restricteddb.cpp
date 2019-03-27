// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "restricteddb.h"

static const char DB_FLAG = 'D';
static const char VERIFIER_FLAG = 'V';
static const char ADDRESS_TAG_FLAG = 'T';
static const char RESTRICTED_ADDRESS_FLAG = 'R';
static const char GLOBAL_RESTRICTION_FLAG = 'G';


CRestrictedDB::CRestrictedDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assets" / "restricted", nCacheSize, fMemory, fWipe) {
}

// Restricted Verifier Strings
bool CRestrictedDB::WriteVerifier(const std::string& assetName, const std::string& verifier)
{
    return Write(std::make_pair(VERIFIER_FLAG, assetName), verifier);
}

bool CRestrictedDB::ReadVerifier(const std::string& assetName, std::string& verifier)
{
    return Read(std::make_pair(VERIFIER_FLAG, assetName), verifier);
}

bool CRestrictedDB::EraseVerifier(const std::string& assetName)
{
    return Erase(std::make_pair(VERIFIER_FLAG, assetName));
}

// Address Tags
bool CRestrictedDB::WriteAddressTag(const std::string& address, const std::string& tag)
{
    int8_t i = 1;
    return Write(std::make_pair(ADDRESS_TAG_FLAG, std::make_pair(address, tag)), i);
}

bool CRestrictedDB::ReadAddressTag(const std::string& address, const std::string& tag)
{
    int8_t i;
    return Read(std::make_pair(ADDRESS_TAG_FLAG, std::make_pair(address, tag)), i);
}

bool CRestrictedDB::EraseAddressTag(const std::string& address, const std::string& tag)
{
    return Erase(std::make_pair(ADDRESS_TAG_FLAG, std::make_pair(address, tag)));
}

// Address Restriction
bool CRestrictedDB::WriteRestrictedAddress(const std::string& address, const std::string& assetName)
{
    int8_t i = 1;
    return Write(std::make_pair(RESTRICTED_ADDRESS_FLAG, std::make_pair(address, assetName)), i);
}

bool CRestrictedDB::ReadRestrictedAddress(const std::string& address, const std::string& assetName)
{
    int8_t i;
    return Read(std::make_pair(RESTRICTED_ADDRESS_FLAG, std::make_pair(address, assetName)), i);
}

bool CRestrictedDB::EraseRestrictedAddress(const std::string& address, const std::string& assetName)
{
    return Erase(std::make_pair(RESTRICTED_ADDRESS_FLAG, std::make_pair(address, assetName)));
}

// Global Restriction
bool CRestrictedDB::WriteGlobalRestriction(const std::string& assetName)
{
    int8_t i = 1;
    return Write(std::make_pair(GLOBAL_RESTRICTION_FLAG, assetName), i);
}

bool CRestrictedDB::ReadGlobalRestriction(const std::string& assetName)
{
    int8_t i;
    return Read(std::make_pair(GLOBAL_RESTRICTION_FLAG, assetName), i);
}

bool CRestrictedDB::EraseGlobalRestriction(const std::string& assetName)
{
    return Erase(std::make_pair(GLOBAL_RESTRICTION_FLAG, assetName));
}

bool CRestrictedDB::WriteFlag(const std::string &name, bool fValue)
{
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CRestrictedDB::ReadFlag(const std::string &name, bool &fValue)
{
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}
