// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Rewards-specific functionality
 */
#ifndef RAVEN_REWARDS_H
#define RAVEN_REWARDS_H

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

// Launches the rewards processing thread
bool LaunchRewardsProcessorThread();

// Shuts down the rewards processing thread
bool ShutdownRewardsProcessorThread();

#endif // RAVEN_REWARDS_H
