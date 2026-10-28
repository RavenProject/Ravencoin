// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus.h"

unsigned int GetMaxBlockWeight()
{
    // Absolute structural ceiling supported by an RIP-25-aware binary.
    // The active 8/12/16 MWU consensus limit is enforced contextually
    // from pindexPrev and VersionBits in validation.cpp.
    return MAX_BLOCK_WEIGHT_RIP25_PHASE2;
}

unsigned int GetMaxBlockSerializedSize()
{
    // Absolute buffer/import ceiling. The active phased limit is enforced
    // contextually together with block weight in validation.cpp.
    return MAX_BLOCK_SERIALIZED_SIZE_RIP25_PHASE2;
}
