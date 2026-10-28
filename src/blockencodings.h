// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_BLOCK_ENCODINGS_H
#define RAVEN_BLOCK_ENCODINGS_H

#include "consensus/consensus.h"
#include "primitives/block.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

class CTxMemPool;
class CDatabasedAssetData;

// Dumb helper to handle CTransaction compression at serialize-time
struct TransactionCompressor {
private:
    CTransactionRef& tx;
public:
    explicit TransactionCompressor(CTransactionRef& txIn) : tx(txIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tx); //TODO: Compress tx encoding
    }
};

class BlockTransactionsRequest {
public:
    // A BlockTransactionsRequest message
    uint256 blockhash;
    std::vector<uint32_t> indexes;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockhash);
        uint64_t indexes_size = (uint64_t)indexes.size();
        READWRITE(COMPACTSIZE(indexes_size));
        if (indexes_size > MAX_BLOCK_TRANSACTION_COUNT) {
            throw std::ios_base::failure("BlockTransactionsRequest count exceeds structural limit");
        }
        if (ser_action.ForRead()) {
            std::vector<uint32_t> parsed;
            parsed.reserve(static_cast<size_t>(indexes_size));
            uint64_t next_index = 0;
            for (uint64_t i = 0; i < indexes_size; ++i) {
                uint64_t differential_index = 0;
                READWRITE(COMPACTSIZE(differential_index));
                if (differential_index >= MAX_BLOCK_TRANSACTION_COUNT ||
                        next_index >= MAX_BLOCK_TRANSACTION_COUNT ||
                        differential_index >= MAX_BLOCK_TRANSACTION_COUNT - next_index) {
                    throw std::ios_base::failure("BlockTransactionsRequest index exceeds structural limit");
                }
                const uint64_t absolute_index = next_index + differential_index;
                parsed.push_back(static_cast<uint32_t>(absolute_index));
                next_index = absolute_index + 1;
            }
            indexes.swap(parsed);
        } else {
            uint64_t next_index = 0;
            for (size_t i = 0; i < indexes.size(); i++) {
                const uint64_t absolute_index = indexes[i];
                if (absolute_index >= MAX_BLOCK_TRANSACTION_COUNT ||
                        absolute_index < next_index) {
                    throw std::ios_base::failure(
                        "BlockTransactionsRequest indexes are not strictly increasing within structural limit");
                }
                uint64_t differential_index = absolute_index - next_index;
                READWRITE(COMPACTSIZE(differential_index));
                next_index = absolute_index + 1;
            }
        }
    }
};

class BlockTransactions {
public:
    // A BlockTransactions message
    uint256 blockhash;
    std::vector<CTransactionRef> txn;

    BlockTransactions() {}
    explicit BlockTransactions(const BlockTransactionsRequest& req) :
        blockhash(req.blockhash), txn(req.indexes.size()) {}

    template <typename Stream>
    inline void UnserializeTransactions(Stream& s, uint64_t txnSize) {
        if (txnSize > MAX_BLOCK_TRANSACTION_COUNT) {
            throw std::ios_base::failure("BlockTransactions count exceeds structural limit");
        }

        std::vector<CTransactionRef> parsed;
        parsed.reserve(static_cast<size_t>(txnSize));
        for (uint64_t i = 0; i < txnSize; ++i) {
            CTransactionRef transaction;
            TransactionCompressor compressor(transaction);
            ::Unserialize(s, compressor);
            parsed.push_back(std::move(transaction));
        }
        txn.swap(parsed);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockhash);
        uint64_t txn_size = (uint64_t)txn.size();
        READWRITE(COMPACTSIZE(txn_size));
        if (txn_size > MAX_BLOCK_TRANSACTION_COUNT) {
            throw std::ios_base::failure("BlockTransactions count exceeds structural limit");
        }
        if (ser_action.ForRead()) {
            std::vector<CTransactionRef> parsed;
            parsed.reserve(static_cast<size_t>(txn_size));
            for (uint64_t i = 0; i < txn_size; ++i) {
                CTransactionRef transaction;
                TransactionCompressor compressor(transaction);
                READWRITE(REF(compressor));
                parsed.push_back(std::move(transaction));
            }
            txn.swap(parsed);
        } else {
            for (CTransactionRef& transaction : txn) {
                TransactionCompressor compressor(transaction);
                READWRITE(REF(compressor));
            }
        }
    }
};

// Dumb serialization/storage-helper for CBlockHeaderAndShortTxIDs and PartiallyDownloadedBlock
struct PrefilledTransaction {
    // Used as an offset since last prefilled tx in CBlockHeaderAndShortTxIDs,
    // as a proper transaction-in-block-index in PartiallyDownloadedBlock
    uint32_t index{0};
    CTransactionRef tx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint64_t idx = index;
        READWRITE(COMPACTSIZE(idx));
        if (idx >= MAX_BLOCK_TRANSACTION_COUNT) {
            throw std::ios_base::failure("PrefilledTransaction index exceeds structural limit");
        }
        index = static_cast<uint32_t>(idx);
        READWRITE(REF(TransactionCompressor(tx)));
    }
};

typedef enum ReadStatus_t
{
    READ_STATUS_OK,
    READ_STATUS_INVALID, // Invalid object, peer is sending bogus crap
    READ_STATUS_FAILED, // Failed to process object
    READ_STATUS_CHECKBLOCK_FAILED, // Used only by FillBlock to indicate a
                                   // failure in CheckBlock.
} ReadStatus;

class CBlockHeaderAndShortTxIDs {
private:
    mutable uint64_t shorttxidk0, shorttxidk1;
    uint64_t nonce;

    void FillShortTxIDSelector() const;

    friend class PartiallyDownloadedBlock;

    static const int SHORTTXIDS_LENGTH = 6;
protected:
    std::vector<uint64_t> shorttxids;
    std::vector<PrefilledTransaction> prefilledtxn;

public:
    CBlockHeader header;

    // Dummy for deserialization
    CBlockHeaderAndShortTxIDs() {}

    CBlockHeaderAndShortTxIDs(const CBlock& block, bool fUseWTXID);

    uint64_t GetShortID(const uint256& txhash) const;

    size_t BlockTxCount() const { return shorttxids.size() + prefilledtxn.size(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(nonce);

        uint64_t shorttxids_size = (uint64_t)shorttxids.size();
        READWRITE(COMPACTSIZE(shorttxids_size));
        if (shorttxids_size > MAX_BLOCK_TRANSACTION_COUNT) {
            throw std::ios_base::failure("Compact block short ID count exceeds structural limit");
        }

        std::vector<uint64_t> parsedShortTxIDs;
        if (ser_action.ForRead()) {
            parsedShortTxIDs.resize(static_cast<size_t>(shorttxids_size));
            for (uint64_t i = 0; i < shorttxids_size; ++i) {
                uint32_t lsb = 0;
                uint16_t msb = 0;
                READWRITE(lsb);
                READWRITE(msb);
                parsedShortTxIDs[static_cast<size_t>(i)] =
                    (uint64_t(msb) << 32) | uint64_t(lsb);
            }
        } else {
            for (size_t i = 0; i < shorttxids.size(); i++) {
                uint32_t lsb = shorttxids[i] & 0xffffffff;
                uint16_t msb = (shorttxids[i] >> 32) & 0xffff;
                READWRITE(lsb);
                READWRITE(msb);
            }
        }
        static_assert(SHORTTXIDS_LENGTH == 6, "shorttxids serialization assumes 6-byte shorttxids");

        uint64_t prefilledSize = prefilledtxn.size();
        READWRITE(COMPACTSIZE(prefilledSize));
        if (prefilledSize > MAX_BLOCK_TRANSACTION_COUNT - shorttxids_size) {
            throw std::ios_base::failure("Compact block transaction count exceeds structural limit");
        }

        if (ser_action.ForRead()) {
            std::vector<PrefilledTransaction> parsedPrefilled;
            parsedPrefilled.reserve(static_cast<size_t>(prefilledSize));
            for (uint64_t i = 0; i < prefilledSize; ++i) {
                PrefilledTransaction transaction;
                READWRITE(transaction);
                parsedPrefilled.push_back(std::move(transaction));
            }
            shorttxids.swap(parsedShortTxIDs);
            prefilledtxn.swap(parsedPrefilled);
            FillShortTxIDSelector();
        } else {
            for (PrefilledTransaction& transaction : prefilledtxn) {
                READWRITE(transaction);
            }
        }
    }
};

class PartiallyDownloadedBlock {
protected:
    std::vector<CTransactionRef> txn_available;
    size_t prefilled_count = 0, mempool_count = 0, extra_count = 0;
    CTxMemPool* pool;
public:
    CBlockHeader header;
    explicit PartiallyDownloadedBlock(CTxMemPool* poolIn) : pool(poolIn) {}

    // extra_txn is a list of extra transactions to look at, in <witness hash, reference> form
    ReadStatus InitData(const CBlockHeaderAndShortTxIDs& cmpctblock, const std::vector<std::pair<uint256, CTransactionRef>>& extra_txn);
    bool IsTxAvailable(size_t index) const;
    bool TryGetMissingTxCount(size_t& missing) const;
    ReadStatus FillBlock(CBlock& block, const std::vector<CTransactionRef>& vtx_missing);
};

class SerializedAssetData {
public:
    std::string name;
    int8_t units;
    CAmount amount;
    int8_t reissuable;
    int8_t hasIPFS;
    std::string ipfs;
    int32_t nHeight;

    SerializedAssetData(const CDatabasedAssetData &assetData);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(name);
        READWRITE(amount);
        READWRITE(units);
        READWRITE(reissuable);
        READWRITE(hasIPFS);
        READWRITE(ipfs);
        READWRITE(nHeight);
    }


};
#endif
