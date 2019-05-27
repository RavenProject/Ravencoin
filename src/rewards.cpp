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
void ProcessRewards();

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

        //  Join the thread to wait for it to finish
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
        LogPrintf("rewards_thread: Processing awards...\n");

        //  And check for the shutdown signal
        {
            boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
            gs_signalCondition.wait_for(lock, boost::chrono::seconds(SECONDS_TO_WAIT_FOR_SHUTDOWN));
        }
    } while (!gs_beginShutdown);

    LogPrintf("rewards_thread: Rewards processor thread is shutting down...\n");

    //  Signal successful shutdown
    {
        boost::unique_lock<boost::mutex> lock(gs_conditionProtectionMutex);
        gs_shutdownCompleted = true;
        gs_signalCondition.notify_all();
    }

    LogPrintf("rewards_thread: Rewards processor thread has stopped.\n");
}

void ProcessRewards()
{
    LogPrintf("rewards_thread: Processing rewards...\n");

    //  Actual logic to process rewards

    LogPrintf("rewards_thread: Rewards processing completed.\n");
}