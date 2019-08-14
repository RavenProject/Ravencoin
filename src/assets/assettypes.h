// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_NEWASSET_H
#define RAVENCOIN_NEWASSET_H

#include <string>
#include <sstream>
#include <list>
#include <unordered_map>
#include "amount.h"
#include "script/standard.h"
#include "primitives/transaction.h"

#define MAX_UNIT 8
#define MIN_UNIT 0

class CAssetsCache;

enum class AssetType
{
    ROOT = 0,
    SUB = 1,
    UNIQUE = 2,
    MSGCHANNEL = 3,
    QUALIFIER = 4,
    SUB_QUALIFIER = 5,
    RESTRICTED = 6,
    VOTE = 7,
    REISSUE = 8,
    OWNER = 9,
    NULL_ADD_QUALIFIER = 10,
    INVALID = 11
};

enum class QualifierType
{
    REMOVE_QUALIFIER = 0,
    ADD_QUALIFIER = 1
};

enum class RestrictedType
{
    UNFREEZE_ADDRESS = 0,
    FREEZE_ADDRESS= 1,
    GLOBAL_UNFREEZE = 2,
    GLOBAL_FREEZE = 3
};

int IntFromAssetType(AssetType type);
AssetType AssetTypeFromInt(int nType);

const char IPFS_SHA2_256 = 0x12;
const char TXID_NOTIFIER = 0x54;
const char IPFS_SHA2_256_LEN = 0x20;

template <typename Stream, typename Operation>
bool ReadWriteAssetHash(Stream &s, Operation ser_action, std::string &strIPFSHash)
{
    // assuming 34-byte IPFS SHA2-256 decoded hash (0x12, 0x20, 32 more bytes)
    if (ser_action.ForRead())
    {
        strIPFSHash = "";
        if (!s.empty() and s.size() >= 33) {
            char _sha2_256;
            ::Unserialize(s, _sha2_256);
            std::basic_string<char> hash;
            ::Unserialize(s, hash);

            std::ostringstream os;

            // If it is an ipfs hash, we put the Q and the m 'Qm' at the front
            if (_sha2_256 == IPFS_SHA2_256)
                os << IPFS_SHA2_256 << IPFS_SHA2_256_LEN;

            os << hash.substr(0, 32); // Get the 32 bytes of data
            strIPFSHash = os.str();
            return true;
        }
    }
    else
    {
        if (strIPFSHash.length() == 34) {
            ::Serialize(s, IPFS_SHA2_256);
            ::Serialize(s, strIPFSHash.substr(2));
            return true;
        } else if (strIPFSHash.length() == 32) {
            ::Serialize(s, TXID_NOTIFIER);
            ::Serialize(s, strIPFSHash);
            return true;
        }
    }
    return false;
};

class CNewAsset
{
public:
    std::string strName; // MAX 31 Bytes
    CAmount nAmount;     // 8 Bytes
    int8_t units;        // 1 Byte
    int8_t nReissuable;  // 1 Byte
    int8_t nHasIPFS;     // 1 Byte
    std::string strIPFSHash; // MAX 40 Bytes

    CNewAsset()
    {
        SetNull();
    }

    CNewAsset(const std::string& strName, const CAmount& nAmount, const int& units, const int& nReissuable, const int& nHasIPFS, const std::string& strIPFSHash);
    CNewAsset(const std::string& strName, const CAmount& nAmount);

    CNewAsset(const CNewAsset& asset);
    CNewAsset& operator=(const CNewAsset& asset);

    void SetNull()
    {
        strName= "";
        nAmount = 0;
        units = int8_t(MAX_UNIT);
        nReissuable = int8_t(0);
        nHasIPFS = int8_t(0);
        strIPFSHash = "";
    }

    bool IsNull() const;
    std::string ToString();

    void ConstructTransaction(CScript& script) const;
    void ConstructOwnerTransaction(CScript& script) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strName);
        READWRITE(nAmount);
        READWRITE(units);
        READWRITE(nReissuable);
        READWRITE(nHasIPFS);
        if (nHasIPFS == 1) {
            ReadWriteAssetHash(s, ser_action, strIPFSHash);
        }
    }
};

class AssetComparator
{
public:
    bool operator()(const CNewAsset& s1, const CNewAsset& s2) const
    {
        return s1.strName < s2.strName;
    }
};

class CDatabasedAssetData
{
public:
    CNewAsset asset;
    int nHeight;
    uint256 blockHash;

    CDatabasedAssetData(const CNewAsset& asset, const int& nHeight, const uint256& blockHash);
    CDatabasedAssetData();

    void SetNull()
    {
        asset.SetNull();
        nHeight = -1;
        blockHash = uint256();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(asset);
        READWRITE(nHeight);
        READWRITE(blockHash);
    }
};

class CAssetTransfer
{
public:
    std::string strName;
    CAmount nAmount;
    std::string message;
    int64_t nExpireTime;

    CAssetTransfer()
    {
        SetNull();
    }

    void SetNull()
    {
        nAmount = 0;
        strName = "";
        message = "";
        nExpireTime = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strName);
        READWRITE(nAmount);
        bool validIPFS = ReadWriteAssetHash(s, ser_action, message);
        if (validIPFS) {
            if (ser_action.ForRead()) {
                if (!s.empty() && s.size() >= sizeof(int64_t)) {
                    ::Unserialize(s, nExpireTime);
                }
            } else {
                if (nExpireTime != 0) {
                    ::Serialize(s, nExpireTime);
                }
            }
        }

    }

    CAssetTransfer(const std::string& strAssetName, const CAmount& nAmount, const std::string& message = "", const int64_t& nExpireTime = 0);
    bool IsValid(std::string& strError) const;
    void ConstructTransaction(CScript& script) const;
    bool ContextualCheckAgainstVerifyString(CAssetsCache *assetCache, const std::string& address, std::string& strError) const;
};

class CReissueAsset
{
public:
    std::string strName;
    CAmount nAmount;
    int8_t nUnits;
    int8_t nReissuable;
    std::string strIPFSHash;

    CReissueAsset()
    {
        SetNull();
    }

    void SetNull()
    {
        nAmount = 0;
        strName = "";
        nUnits = 0;
        nReissuable = 1;
        strIPFSHash = "";
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strName);
        READWRITE(nAmount);
        READWRITE(nUnits);
        READWRITE(nReissuable);
        ReadWriteAssetHash(s, ser_action, strIPFSHash);
    }

    CReissueAsset(const std::string& strAssetName, const CAmount& nAmount, const int& nUnits, const int& nReissuable, const std::string& strIPFSHash);
    void ConstructTransaction(CScript& script) const;
    bool IsNull() const;
};

class CNullAssetTxData {
public:
    std::string asset_name;
    int8_t flag; // on/off but could be used to determine multiple options later on

    CNullAssetTxData()
    {
        SetNull();
    }

    void SetNull()
    {
        flag = -1;
        asset_name = "";
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(asset_name);
        READWRITE(flag);
    }

    CNullAssetTxData(const std::string& strAssetname, const int8_t& nFlag);
    bool IsValid(std::string& strError, CAssetsCache& assetCache, bool fForceCheckPrimaryAssetExists) const;
    void ConstructTransaction(CScript& script) const;
    void ConstructGlobalRestrictionTransaction(CScript &script) const;
};

class CNullAssetTxVerifierString {

public:
    std::string verifier_string;

    CNullAssetTxVerifierString()
    {
        SetNull();
    }

    void SetNull()
    {
        verifier_string ="";
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(verifier_string);
    }

    CNullAssetTxVerifierString(const std::string& verifier);
    void ConstructTransaction(CScript& script) const;
};

/** THESE ARE ONLY TO BE USED WHEN ADDING THINGS TO THE CACHE DURING CONNECT AND DISCONNECT BLOCK */
struct CAssetCacheNewAsset
{
    CNewAsset asset;
    std::string address;
    uint256 blockHash;
    int blockHeight;

    CAssetCacheNewAsset(const CNewAsset& asset, const std::string& address, const int& blockHeight, const uint256& blockHash)
    {
        this->asset = asset;
        this->address = address;
        this->blockHash = blockHash;
        this->blockHeight = blockHeight;
    }

    bool operator<(const CAssetCacheNewAsset& rhs) const
    {
        return asset.strName < rhs.asset.strName;
    }
};

struct CAssetCacheReissueAsset
{
    CReissueAsset reissue;
    std::string address;
    COutPoint out;
    uint256 blockHash;
    int blockHeight;


    CAssetCacheReissueAsset(const CReissueAsset& reissue, const std::string& address, const COutPoint& out, const int& blockHeight, const uint256& blockHash)
    {
        this->reissue = reissue;
        this->address = address;
        this->out = out;
        this->blockHash = blockHash;
        this->blockHeight = blockHeight;
    }

    bool operator<(const CAssetCacheReissueAsset& rhs) const
    {
        return out < rhs.out;
    }

};

struct CAssetCacheNewTransfer
{
    CAssetTransfer transfer;
    std::string address;
    COutPoint out;

    CAssetCacheNewTransfer(const CAssetTransfer& transfer, const std::string& address, const COutPoint& out)
    {
        this->transfer = transfer;
        this->address = address;
        this->out = out;
    }

    bool operator<(const CAssetCacheNewTransfer& rhs ) const
    {
        return out < rhs.out;
    }
};

struct CAssetCacheNewOwner
{
    std::string assetName;
    std::string address;

    CAssetCacheNewOwner(const std::string& assetName, const std::string& address)
    {
        this->assetName = assetName;
        this->address = address;
    }

    bool operator<(const CAssetCacheNewOwner& rhs) const
    {

        return assetName < rhs.assetName;
    }
};

struct CAssetCacheUndoAssetAmount
{
    std::string assetName;
    std::string address;
    CAmount nAmount;

    CAssetCacheUndoAssetAmount(const std::string& assetName, const std::string& address, const CAmount& nAmount)
    {
        this->assetName = assetName;
        this->address = address;
        this->nAmount = nAmount;
    }
};

struct CAssetCacheSpendAsset
{
    std::string assetName;
    std::string address;
    CAmount nAmount;

    CAssetCacheSpendAsset(const std::string& assetName, const std::string& address, const CAmount& nAmount)
    {
        this->assetName = assetName;
        this->address = address;
        this->nAmount = nAmount;
    }
};

struct CAssetCacheQualifierAddress {
    std::string assetName;
    std::string address;
    QualifierType type;

    CAssetCacheQualifierAddress(const std::string &assetName, const std::string &address, const QualifierType &type) {
        this->assetName = assetName;
        this->address = address;
        this->type = type;
    }

    bool operator<(const CAssetCacheQualifierAddress &rhs) const {
        return assetName < rhs.assetName || (assetName == rhs.assetName && address < rhs.address);
    }

    uint256 GetHash();
};

struct CAssetCacheRootQualifierChecker {
    std::string rootAssetName;
    std::string address;

    CAssetCacheRootQualifierChecker(const std::string &assetName, const std::string &address) {
        this->rootAssetName = assetName;
        this->address = address;
    }

    bool operator<(const CAssetCacheRootQualifierChecker &rhs) const {
        return rootAssetName < rhs.rootAssetName || (rootAssetName == rhs.rootAssetName && address < rhs.address);
    }

    uint256 GetHash();
};

struct CAssetCacheRestrictedAddress
{
    std::string assetName;
    std::string address;
    RestrictedType type;

    CAssetCacheRestrictedAddress(const std::string& assetName, const std::string& address, const RestrictedType& type)
    {
        this->assetName = assetName;
        this->address = address;
        this->type = type;
    }

    bool operator<(const CAssetCacheRestrictedAddress& rhs) const
    {
        return assetName < rhs.assetName || (assetName == rhs.assetName && address < rhs.address);
    }

    uint256 GetHash();
};

struct CAssetCacheRestrictedGlobal
{
    std::string assetName;
    RestrictedType type;

    CAssetCacheRestrictedGlobal(const std::string& assetName, const RestrictedType& type)
    {
        this->assetName = assetName;
        this->type = type;
    }

    bool operator<(const CAssetCacheRestrictedGlobal& rhs) const
    {
        return assetName < rhs.assetName;
    }
};

struct CAssetCacheRestrictedVerifiers
{
    std::string assetName;
    std::string verifier;
    bool fUndoingRessiue;

    CAssetCacheRestrictedVerifiers(const std::string& assetName, const std::string& verifier)
    {
        this->assetName = assetName;
        this->verifier = verifier;
        fUndoingRessiue = false;
    }

    bool operator<(const CAssetCacheRestrictedVerifiers& rhs) const
    {
        return assetName < rhs.assetName;
    }
};

// Least Recently Used Cache
template<typename cache_key_t, typename cache_value_t>
class CLRUCache
{
public:
    typedef typename std::pair<cache_key_t, cache_value_t> key_value_pair_t;
    typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

    CLRUCache(size_t max_size) : maxSize(max_size)
    {
    }
    CLRUCache()
    {
        SetNull();
    }

    void Put(const cache_key_t& key, const cache_value_t& value)
    {
        auto it = cacheItemsMap.find(key);
        cacheItemsList.push_front(key_value_pair_t(key, value));
        if (it != cacheItemsMap.end())
        {
            cacheItemsList.erase(it->second);
            cacheItemsMap.erase(it);
        }
        cacheItemsMap[key] = cacheItemsList.begin();

        if (cacheItemsMap.size() > maxSize)
        {
            auto last = cacheItemsList.end();
            last--;
            cacheItemsMap.erase(last->first);
            cacheItemsList.pop_back();
        }
    }

    void Erase(const cache_key_t& key)
    {
        auto it = cacheItemsMap.find(key);
        if (it != cacheItemsMap.end())
        {
            cacheItemsList.erase(it->second);
            cacheItemsMap.erase(it);
        }
    }

    const cache_value_t& Get(const cache_key_t& key)
    {
        auto it = cacheItemsMap.find(key);
        if (it == cacheItemsMap.end())
        {
            throw std::range_error("There is no such key in cache");
        }
        else
        {
            cacheItemsList.splice(cacheItemsList.begin(), cacheItemsList, it->second);
            return it->second->second;
        }
    }

    bool Exists(const cache_key_t& key) const
    {
        return cacheItemsMap.find(key) != cacheItemsMap.end();
    }

    size_t Size() const
    {
        return cacheItemsMap.size();
    }


    void Clear()
    {
        cacheItemsMap.clear();
        cacheItemsList.clear();
    }

    void SetNull()
    {
        maxSize = 0;
        Clear();
    }

    size_t MaxSize() const
    {
        return maxSize;
    }


    void SetSize(const size_t size)
    {
        maxSize = size;
    }

   const std::unordered_map<cache_key_t, list_iterator_t>& GetItemsMap()
    {
        return cacheItemsMap;
    };

    const std::list<key_value_pair_t>& GetItemsList()
    {
        return cacheItemsList;
    };


    CLRUCache(const CLRUCache& cache)
    {
        this->cacheItemsList = cache.cacheItemsList;
        this->cacheItemsMap = cache.cacheItemsMap;
        this->maxSize = cache.maxSize;
    }

private:
    std::list<key_value_pair_t> cacheItemsList;
    std::unordered_map<cache_key_t, list_iterator_t> cacheItemsMap;
    size_t maxSize;
};

#endif //RAVENCOIN_NEWASSET_H
