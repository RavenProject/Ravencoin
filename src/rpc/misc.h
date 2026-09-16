// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_RPC_MISC_H
#define RAVEN_RPC_MISC_H

#include "amount.h"

#include <stdint.h>

/** Add a positive address-index delta without signed overflow. */
bool AddToReceivedAmount(uint64_t& received, CAmount delta);

#endif // RAVEN_RPC_MISC_H
