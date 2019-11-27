// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus.h"
#include <validation.h>

unsigned int GetMaxBlockWeight()
{
    // Now that Assets have gone live, we should make checks against the new larger block size only
    // This is necessary because when the chain loads, it can fail certain blocks(that are valid) when
    // The asset active state isn't set like during a reindex
    return MAX_BLOCK_WEIGHT_RIP2;

    // Old block weight for when assets weren't activated
//    return MAX_BLOCK_WEIGHT;
}

unsigned int GetMaxBlockSerializedSize()
{
    // Now that Assets have gone live, we should make checks against the new larger block size only
    // This is necessary because when the chain loads, it can fail certain blocks(that are valid) when
    // The asset active state isn't set like during a reindex
    return MAX_BLOCK_SERIALIZED_SIZE_RIP2;

    // Old block serialized size for when assets weren't activated
//    return MAX_BLOCK_SERIALIZED_SIZE;
}