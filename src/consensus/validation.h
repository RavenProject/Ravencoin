// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_CONSENSUS_VALIDATION_H
#define RAVEN_CONSENSUS_VALIDATION_H

#include <string>
#include "version.h"
#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include "primitives/block.h"

/** "reject" message codes */
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
// static const unsigned char REJECT_DUST = 0x41; // part of BIP 61
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;
/** RVN START */
static const unsigned char REJECT_MAXREORGDEPTH = 0x44;
/** RVN END */

/** Capture information about block/transaction validation */
class CValidationState {
private:
    enum mode_state {
        MODE_VALID,   //!< everything ok
        MODE_INVALID, //!< network rule violation (DoS value may be set)
        MODE_ERROR,   //!< run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned int chRejectCode;
    bool corruptionPossible;
    std::string strDebugMessage;
    uint256 failedTransaction;

public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
    bool DoS(int level, bool ret = false,
             unsigned int chRejectCodeIn=0, const std::string &strRejectReasonIn="",
             bool corruptionIn=false,
             const std::string &strDebugMessageIn="", uint256 tx=uint256()) {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        strDebugMessage = strDebugMessageIn;
        if (mode == MODE_ERROR)
            return ret;
        nDoS += level;
        mode = MODE_INVALID;
        return ret;
    }
    bool Invalid(bool ret = false,
                 unsigned int _chRejectCode=0, const std::string &_strRejectReason="",
                 const std::string &_strDebugMessage="") {
        return DoS(0, ret, _chRejectCode, _strRejectReason, false, _strDebugMessage);
    }
    bool Error(const std::string& strRejectReasonIn) {
        if (mode == MODE_VALID)
            strRejectReason = strRejectReasonIn;
        mode = MODE_ERROR;
        return false;
    }
    bool IsValid() const {
        return mode == MODE_VALID;
    }
    bool IsInvalid() const {
        return mode == MODE_INVALID;
    }
    bool IsError() const {
        return mode == MODE_ERROR;
    }
    bool IsInvalid(int &nDoSOut) const {
        if (IsInvalid()) {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    bool CorruptionPossible() const {
        return corruptionPossible;
    }
    void SetCorruptionPossible() {
        corruptionPossible = true;
    }
    void SetFailedTransaction(const uint256& txhash) {
        failedTransaction = txhash;
    }
    uint256 GetFailedTransaction() {
        return failedTransaction;
    }
    bool IsTransactionError() const  {
        return failedTransaction != uint256();
    }
    unsigned int GetRejectCode() const { return chRejectCode; }
    std::string GetRejectReason() const { return strRejectReason; }
    std::string GetDebugMessage() const { return strDebugMessage; }
};

/**
 * Ravencoin 4.8.0 KAWPOW header-height invariant.
 * Before the 4.8 height gate (or before KAWPOW itself) the legacy field is
 * not enforced. At/after both gates, the height declared in the PoW header
 * must exactly match the block's contextual chain height.
 */
static inline bool IsKAWPOWHeaderHeightValid(const CBlockHeader& block,
                                              int actualHeight,
                                              int headerHeightCheckActivation,
                                              uint32_t kawpowActivationTime)
{
    return actualHeight < headerHeightCheckActivation ||
           block.nTime < kawpowActivationTime ||
           block.nHeight == static_cast<uint32_t>(actualHeight);
}

// These implement the weight = (stripped_size * 4) + witness_size formula,
// using only serialization with and without witness data. As witness_size
// is equal to total_size - stripped_size, this formula is identical to:
// weight = (stripped_size * 3) + total_size.
static inline int64_t GetPQWitnessInputDiscount(const CTxIn& txin)
{
    const auto& stack = txin.scriptWitness.stack;
    if (stack.size() != 2 || stack[0].size() != 2420 || stack[1].size() != 1312)
        return 0;

    const int64_t pqBytes = (int64_t)stack[0].size() + (int64_t)stack[1].size();
    return pqBytes * (PQ_WITNESS_SCALE_FACTOR - WITNESS_SCALE_FACTOR) / PQ_WITNESS_SCALE_FACTOR;
}

static inline int64_t GetPQWitnessDiscount(const CTransaction& tx)
{
    int64_t discount = 0;
    for (const auto& txin : tx.vin)
        discount += GetPQWitnessInputDiscount(txin);
    return discount;
}

static inline int64_t GetTransactionWeight(const CTransaction& tx)
{
    const int64_t standardWeight = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1)
                                 + ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    // RIP-25 fee/policy accounting keeps the approved 8x ML-DSA witness discount.
    return standardWeight - GetPQWitnessDiscount(tx);
}

static inline int64_t GetBlockWeight(const CBlock& block)
{
    // Legacy/RIP-2 consensus weight. The RIP-25 block-level discount is
    // activation-gated and UTXO-bound in validation.cpp.
    return ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1)
         + ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
}

static inline int64_t GetBlockWeightRIP25(const CBlock& block)
{
    // Optimistic/structural RIP-25 weight. Consensus validation additionally
    // binds every discounted input to an actual witness-v2 32-byte prevout.
    int64_t weight = GetBlockWeight(block);
    for (const auto& tx : block.vtx)
        weight -= GetPQWitnessDiscount(*tx);
    return weight;
}

#endif // RAVEN_CONSENSUS_VALIDATION_H
