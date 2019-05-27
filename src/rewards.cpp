// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

#include "util.h"
#include "rewards.h"
#include "validation.h"
#include "assets/rewardsdb.h"
#include "wallet/wallet.h"
#include "univalue.h"
#include "wallet/coincontrol.h"

#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

static boost::thread gs_processorThread;

//  Global+Static variables for thread synchronization
static boost::mutex gs_conditionProtectionMutex;
static boost::condition_variable gs_signalCondition;
static bool gs_startupCompleted = false;
static bool gs_beginShutdown = false;
static bool gs_shutdownCompleted = false;

//  Number of seconds to wait for shutdown while processing rewards
const unsigned int SECONDS_TO_WAIT_FOR_SHUTDOWN = 1;

//  Addresses are delimited by commas
static const std::string ADDRESS_COMMA_DELIMITER = ",";

//  Thread function for the rewards processor thread
void RewardsProcessorThreadFunc();

//  Main functionality for the rewards processor
void InitializeRewardProcessing();
void ProcessRewards();
void ShutdownRewardsProcessing();

//  Retrieves all of the payable owners for the specified asset, excluding those
//      in the exception address list.
bool RetrievePayableOwnersOfAsset(
    const std::string & p_assetName, const std::string & p_exceptionAddresses,
    std::set<std::string> & p_payableOwners);

//  Transfer the specified amount of the asset from the source to the target
bool InitiateTransfer(
    CWallet * const p_walletPtr, int64_t p_xferAmt, std::string p_src, std::string p_dest,
    std::string & p_resultantTxID);

//  Retrieves the wallet with teh specified name
CWallet *GetWalletByName(const std::string & p_walletName);

//  This is in another module
extern CAmount AmountFromValue(const UniValue & p_value);

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

    //  Lock access to the databases
    LOCK(cs_main);

    //  Verify that DB pointers are good
    if (pRewardsDb == nullptr) {
        LogPrintf("rewards_thread: Invalid rewards DB!\n");
        return;
    }
    if (passetsdb == nullptr) {
        LogPrintf("rewards_thread: Invalid assets DB!\n");
        return;
    }

    //  Retrieve list of un-processed reward requests
    std::set<CRewardsDBEntry> dbEntriesToProcess;
    int currentHeight = chainActive.Height();

    if (!pRewardsDb->LoadPayableRewards(dbEntriesToProcess, currentHeight)) {
        LogPrintf("rewards_thread: Failed to retrieve pending records from rewards DB!\n");
        return;
    }

    if (dbEntriesToProcess.size() == 0) {
        LogPrintf("rewards_thread: There are no rewards pending processing at the current height of %d!\n",
            currentHeight);
        return;
    }

    //  Iterate through list of requests, processing each one
    for (auto const & rewardEntry : dbEntriesToProcess) {
        //  For each request, retrieve the list of non-exception owner records for the specified asset
        std::set<std::string> payableOwners;

        if (!RetrievePayableOwnersOfAsset(rewardEntry.payoutSrc, rewardEntry.exceptionAddresses, payableOwners)) {
            LogPrintf("rewards_thread: Failed to retrieve payable owners of '%s'!\n",
                rewardEntry.payoutSrc.c_str());

            continue;
        }

        if (payableOwners.size() > 0) {
            //  Divide the total payout amount by the number of payable asset owners
            int64_t payoutAmountPerAccount = rewardEntry.totalPayoutAmt / payableOwners.size();

            //  Retrieve the specified wallet for this payout
            CWallet * const walletPtr = GetWalletByName(rewardEntry.walletName);
            if (!EnsureWalletIsAvailable(walletPtr, true)) {
                LogPrintf("rewards_thread: Wallet associated with reward is unavailable!\n");

                continue;
            }

            //  Lock the wallet while we're doing stuff
            LOCK2(cs_main, walletPtr->cs_wallet);
            EnsureWalletIsUnlocked(walletPtr);

            //  Loop through each payable account for the asset, sending it the appropriate portion of the total payout amount
            for (auto const & payableOwner : payableOwners) {
                //  Send the amount to the destination
                std::string transactionID;

                if (!InitiateTransfer(walletPtr, payoutAmountPerAccount, rewardEntry.payoutSrc, payableOwner, transactionID)) {
                    LogPrintf("rewards_thread: Failed to transfer %lld of '%s' to '%s'!\n",
                        static_cast<long long>(payoutAmountPerAccount), rewardEntry.payoutSrc.c_str(),
                        payableOwner.c_str());
                }
            }
        }

        //  Delete the processed reward request from the database
        pRewardsDb->RemoveCompletedReward(rewardEntry);
    }

    LogPrintf("rewards_thread: Rewards processing completed.\n");
}

void ShutdownRewardsProcessing()
{
    //  Cleanup anything needed after the main loop exits
}

bool RetrievePayableOwnersOfAsset(
    const std::string & p_assetName, const std::string & p_exceptionAddresses,
    std::set<std::string> & p_payableOwners)
{
    bool fcnRetVal = false;

    LogPrintf("Retrieving payable owners...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Split up the exception address string
        std::vector<std::string> exceptionAddressList;
        boost::split(exceptionAddressList, p_exceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

        //  Retrieve the payable owners for the specified asset
        //  This is done in batches. First, the total count is retrieved, then all of the addresses.
        std::vector<std::pair<std::string, CAmount>> addressInfoPairs;
        int totalEntryCount = 0;

        //  Step 1 - find out how many addresses currently exist
        if (!passetsdb->AssetAddressDir(addressInfoPairs, totalEntryCount, true, p_assetName, INT_MAX, 0)) {
            LogPrintf("Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
            break;
        }

        //  Step 2 - retrieve all of the addresses in batches
        const int MAX_RETRIEVAL_COUNT = 100;
        for (int retrievalOffset = 0; retrievalOffset < totalEntryCount; retrievalOffset += MAX_RETRIEVAL_COUNT) {
            //  Retrieve the specified segment of addresses
            addressInfoPairs.clear();

            if (!passetsdb->AssetAddressDir(addressInfoPairs, totalEntryCount, false, p_assetName, MAX_RETRIEVAL_COUNT, retrievalOffset)) {
                LogPrintf("Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
                break;
            }

            //  Add only non-exception addresses to the list
            for (auto const & addressInfoPair : addressInfoPairs) {
                bool isExceptionAddress = false;

                for (auto const & exceptionAddr : exceptionAddressList) {
                    if (addressInfoPair.first.compare(exceptionAddr) == 0) {
                        isExceptionAddress = true;
                    }
                }

                if (!isExceptionAddress) {
                    p_payableOwners.insert(addressInfoPair.first);
                }
            }
        }

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Payable owner retrieval %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

bool InitiateTransfer(
    CWallet * const p_walletPtr, int64_t p_xferAmt, std::string p_src, std::string p_dest,
    std::string & p_resultantTxID)
{
    bool fcnRetVal = false;

    LogPrintf("Initiating transfer...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Transfer the specified amount of the asset from the source to the target
        CAmount nAmount = AmountFromValue(p_xferAmt);

        std::pair<int, std::string> error;
        std::vector< std::pair<CAssetTransfer, std::string> >vTransfers;

        vTransfers.emplace_back(std::make_pair(CAssetTransfer(p_src, nAmount, DecodeAssetData(""), 0), p_dest));
        CReserveKey reservekey(p_walletPtr);
        CWalletTx transaction;
        CAmount nRequiredFee;

        CCoinControl ctrl;

        // Create the Transaction
        if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee)) {
            LogPrintf("Failed to create transfer asset transaction\n");
            break;
        }

        // Send the Transaction to the network
        if (!SendAssetTransaction(p_walletPtr, transaction, reservekey, error, p_resultantTxID)) {
            LogPrintf("Failed to send asset transaction\n");
            break;
        }

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Transfer processing %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

CWallet *GetWalletByName(const std::string & p_walletName)
{
    for (CWalletRef pwallet : ::vpwallets) {
        if (pwallet->GetName() == p_walletName) {
            return pwallet;
        }
    }

    return ::vpwallets.size() == 1 ? ::vpwallets[0] : nullptr;
}
