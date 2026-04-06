// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus.h"
#include <validation.h>

unsigned int GetMaxBlockWeight()
{
    // RIP-25: Phase 1 PQ block weight increase
    if (fPQHybridIsActive)
        return MAX_BLOCK_WEIGHT_RIP25_PHASE1;

    // RIP-2: Asset block weight
    return MAX_BLOCK_WEIGHT_RIP2;
}

unsigned int GetMaxBlockSerializedSize()
{
    // RIP-25: Phase 1 PQ block serialized size increase
    if (fPQHybridIsActive)
        return MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE1;

    // RIP-2: Asset block serialized size
    return MAX_BLOCK_SERIALIZED_SIZE_RIP2;
}