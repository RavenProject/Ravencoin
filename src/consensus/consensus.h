// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_CONSENSUS_CONSENSUS_H
#define RAVEN_CONSENSUS_CONSENSUS_H

#include <stdlib.h>
#include <stdint.h>

class CBlockIndex;
namespace Consensus { struct Params; }

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;

/** The maximum allowed weight for a block, after RIP 2 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT_RIP2 = 8000000;

/** The maximum allowed size for a serialized block, in bytes after RIP 2(only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE_RIP2 = 8000000;

/** RIP-25: Phase 1 PQ block weight limit (12 MWU) */
static const unsigned int MAX_BLOCK_WEIGHT_RIP25_PHASE1 = 12000000;
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE1 = 12000000;
/** RIP-25: Phase 2 PQ block weight limit (16 MWU), one nominal year after Phase 1. */
static const unsigned int MAX_BLOCK_WEIGHT_RIP25_PHASE2 = 16000000;
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE2 = 16000000;
/** RIP-25: PQ witness scaling and per-element safety bound. */
static const int PQ_WITNESS_SCALE_FACTOR = 8;
static const unsigned int MAX_PQ_WITNESS_ELEMENT_SIZE = 4096;
/** One active witness-v2 ML-DSA verification consumes two sigop cost units. */
static const int64_t PQ_WITNESS_V2_SIGOPS_COST = 2;

/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for a serialized CTransaction
/** No valid phase-2 block can contain more transactions than this. */
static const size_t MAX_BLOCK_TRANSACTION_COUNT = MAX_BLOCK_WEIGHT_RIP25_PHASE2 / MIN_TRANSACTION_WEIGHT;

#define UNUSED_VAR     __attribute__ ((unused))
//! This variable needs to in this class because undo.h uses it. However because it is in this class
//! it causes unused variable warnings when compiling. This UNUSED_VAR removes the unused warnings
UNUSED_VAR static bool fAssetsIsActive = false;
UNUSED_VAR static bool fEnforcedValuesIsActive = false;
UNUSED_VAR static bool fCheckCoinbaseAssetsIsActive = false;

/** Structural upper bounds supported by this binary. Exact active limits are contextual. */
unsigned int GetMaxBlockWeight();
unsigned int GetMaxBlockSerializedSize();

/** RIP-25 activation/resource state for the block after pindexPrev. */
bool IsPQWitnessDiscountActive(const CBlockIndex* pindexPrev, const Consensus::Params& params);
unsigned int GetMaxBlockWeightForPrev(const CBlockIndex* pindexPrev, const Consensus::Params& params);
unsigned int GetMaxBlockSerializedSizeForPrev(const CBlockIndex* pindexPrev, const Consensus::Params& params);

/** Flags for nSequence and nLockTime locks */
enum {
    /* Interpret sequence numbers as relative lock-time constraints. */
    LOCKTIME_VERIFY_SEQUENCE = (1 << 0),

    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

#endif // RAVEN_CONSENSUS_CONSENSUS_H
