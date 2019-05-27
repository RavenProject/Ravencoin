// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

#include "util.h"
#include "rewards.h"

#include <boost/thread.hpp>

static boost::thread gs_processorThread;

//  Global+Static variables for thread synchronization
static boost::mutex gs_conditionProtectionMutex;
static boost::condition_variable gs_signalCondition;
static bool gs_startupCompleted = false;
static bool gs_beginShutdown = false;
static bool gs_shutdownCompleted = false;

//  Number of seconds to wait for shutdown while processing rewards
const unsigned int SECONDS_TO_WAIT_FOR_SHUTDOWN = 1;

//  Thread function for the rewards processor thread
void RewardsProcessorThreadFunc();

//  Main functionality for the rewards processor
void InitializeRewardProcessing();
void ProcessRewards();
void ShutdownRewardsProcessing();

bool LaunchRewardsProcessorThread()
{
    bool fcnRetVal = false;

    LogPrintf("Launching rewards processor thread...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Ensure that the thread is not already running
        if (gs_processorThread.joinable()) {
            LogPrintf("Rewards processor thread is already running!\n");
            break;
        }

        //  Ensure that the "begin shutdown" and "startup completed" signals have been reset
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            gs_beginShutdown = false;
            gs_startupCompleted = false;
        }

        //  Launch the rewards processor thread
        gs_processorThread = boost::thread(&RewardsProcessorThreadFunc);

        //  Wait for successful startup signal
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            while (!gs_startupCompleted) {
                gs_signalCondition.wait(lock);
            }
        }

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Rewards processor thread startup %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

bool ShutdownRewardsProcessorThread()
{
    bool fcnRetVal = false;

    LogPrintf("Stopping rewards processor thread...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Ensure that the thread is already running
        if (!gs_processorThread.joinable()) {
            LogPrintf("Rewards processor thread is not running!\n");
            break;
        }

        //  Shutdown the rewards processor thread
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            gs_beginShutdown = true;
            gs_shutdownCompleted = false;
        }

        //  Wait for successful shutdown signal
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            while (!gs_shutdownCompleted) {
                gs_signalCondition.wait(lock);
            }
        }

        //  Join the thread to wait for it to actually finish
        gs_processorThread.join();

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Rewards processor thread shutdown %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

void RewardsProcessorThreadFunc()
{
    LogPrintf("rewards_thread: Rewards processor thread is starting up...\n");

    //  Perform intialization
    InitializeRewardProcessing();

    LogPrintf("rewards_thread: Rewards processor thread has started.\n");

    //  Signal successful startup
    {
        boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
        gs_startupCompleted = true;
        gs_signalCondition.notify_all();
    }

    //  Process work until a shutdown signal is received
    do {
        //  Process pending rewards
        ProcessRewards();

        //  And check for the shutdown signal
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            gs_signalCondition.wait_for(lock, boost::chrono::seconds(SECONDS_TO_WAIT_FOR_SHUTDOWN));
        }
    } while (!gs_beginShutdown);

    LogPrintf("rewards_thread: Rewards processor thread is shutting down...\n");

    //  Perform shutdown processing
    ShutdownRewardsProcessing();

    //  Signal successful shutdown
    {
        boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
        gs_shutdownCompleted = true;
        gs_signalCondition.notify_all();
    }

    LogPrintf("rewards_thread: Rewards processor thread has stopped.\n");
}

void InitializeRewardProcessing()
{
    //  Load/initialize anything that can be done outside the main loop
}

void ProcessRewards()
{
    LogPrintf("rewards_thread: Processing rewards...\n");

    //  Retrieve list of un-processed reward requests

    //  Iterate through list of requests, processing each one
    {
        //  For each request, retrieve the list of non-exception owner records for the specified asset

        //  Use the count of payable asset owners to create a divisor

        //  Divide the total payout amount by the number of payable asset owners

        //  Loop through each payable account, sending it the appropriate portion of the total payout amount

        //  Delete the processed reward request from the database
    }
    LogPrintf("rewards_thread: Rewards processing completed.\n");
}

void ShutdownRewardsProcessing()
{
    //  Cleanup anything needed after the main loop exits
}
