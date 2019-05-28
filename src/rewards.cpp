// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

#include "util.h"
#include "rewards.h"
#include "validation.h"
#include "consensus/validation.h"
#include "validationinterface.h"
#include "assets/rewardsdb.h"
#include "wallet/wallet.h"
#include "univalue.h"
#include "wallet/coincontrol.h"
#include "net.h"
#include "utilmoneystr.h"


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
bool GenerateBatchedTransactions(
    CWallet * const p_walletPtr, const int64_t p_paymentAmt,
    const std::string & p_assetName, const std::string & p_payoutSrc, const std::string & p_exceptionAddresses);

//  Transfer the specified amount of the asset from the source to the target
bool InitiateTransfer(
    CWallet * const p_walletPtr, int64_t p_xferAmt, const std::string & p_src,
    const std::vector<std::string> & p_destAddresses, size_t p_offset, size_t p_count,
    std::string & p_resultantTxID);

//  Retrieves the wallet with teh specified name
CWallet *GetWalletByName(const std::string & p_walletName);

//  These are in other modules
extern CAmount AmountFromValue(const UniValue & p_value);
extern CTxDestination DecodeDestination(const std::string& str);
bool IsValidDestinationString(const std::string& str);

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

    if (currentHeight == 0) {
        LogPrintf("rewards_thread: Chain height is currently zero!\n");
        return;
    }

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
        bool deleteReward = false;

        do {
            //  Verify entry is good
            if (rewardEntry.tgtAssetName.length() == 0) {
                LogPrintf("rewards_thread: Reward entry has invalid target asset name!\n");
                deleteReward = true;
                break;
            }
            if (rewardEntry.payoutSrc.length() == 0) {
                LogPrintf("rewards_thread: Reward entry has invalid payout source name!\n");
                deleteReward = true;
                break;
            }

            //  For each request, retrieve the list of non-exception owner records for the specified asset
            std::set<std::string> payableOwners;

            //  Retrieve the specified wallet for this payout
            CWallet * const walletPtr = GetWalletByName(rewardEntry.walletName);
            if (!EnsureWalletIsAvailable(walletPtr, true)) {
                LogPrintf("rewards_thread: Wallet associated with reward is unavailable!\n");

                continue;
            }

            //  Generate batched transactions based on the owner addresses
            if (!GenerateBatchedTransactions(walletPtr, rewardEntry.totalPayoutAmt, rewardEntry.tgtAssetName, rewardEntry.payoutSrc, rewardEntry.exceptionAddresses)) {
                LogPrintf("rewards_thread: Failed to retrieve payable owners of '%s'!\n",
                    rewardEntry.payoutSrc.c_str());

                continue;
            }

            //  Indicate success
            deleteReward = true;
        } while (false);

        //  Delete the processed reward request from the database
        if (deleteReward) {
            pRewardsDb->RemoveCompletedReward(rewardEntry);
        }
    }

    LogPrintf("rewards_thread: Rewards processing completed.\n");
}

void ShutdownRewardsProcessing()
{
    //  Cleanup anything needed after the main loop exits
}

bool GenerateBatchedTransactions(
    CWallet * const p_walletPtr, const int64_t p_paymentAmt,
    const std::string & p_assetName, const std::string & p_payoutSrc, const std::string & p_exceptionAddresses)
{
    bool fcnRetVal = false;

    LogPrintf("Generating batched transactions...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Split up the exception address string
        std::vector<std::string> exceptionAddressList;
        boost::split(exceptionAddressList, p_exceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

        //  Retrieve the payable owners for the specified asset
        //  This is done in batches. First, the total count is retrieved, then all of the addresses.
        std::vector<std::pair<std::string, CAmount>> addressAmtPairs;
        int totalEntryCount = 0;

        //  Step 1 - find out how many addresses currently exist
        LogPrintf("Retrieving payable owners...\n");

        if (!passetsdb->AssetAddressDir(addressAmtPairs, totalEntryCount, true, p_assetName, INT_MAX, 0)) {
            LogPrintf("Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
            break;
        }

        //  Step 2 - retrieve all of the addresses in batches
        const int MAX_RETRIEVAL_COUNT = 100;
        std::vector<std::string> paymentAddresses;

        for (int retrievalOffset = 0; retrievalOffset < totalEntryCount; retrievalOffset += MAX_RETRIEVAL_COUNT) {
            //  Retrieve the specified segment of addresses
            addressAmtPairs.clear();

            if (!passetsdb->AssetAddressDir(addressAmtPairs, totalEntryCount, false, p_assetName, MAX_RETRIEVAL_COUNT, retrievalOffset)) {
                LogPrintf("Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
                break;
            }

            //  Add only non-exception addresses to the list
            for (auto const & addressAmtPair : addressAmtPairs) {
                bool isExceptionAddress = false;

                for (auto const & exceptionAddr : exceptionAddressList) {
                    if (addressAmtPair.first.compare(exceptionAddr) == 0) {
                        isExceptionAddress = true;
                    }
                }

                if (!isExceptionAddress) {
                    LogPrintf("Found ownership address for '%s': '%s'\n", p_assetName.c_str(), addressAmtPair.first.c_str());
                    paymentAddresses.push_back(addressAmtPair.first);
                }
            }
        }

        //  Divide the total payout amount by the number of payable asset owners
        int64_t payoutAmountPerAccount = p_paymentAmt / paymentAddresses.size();

        //  Lock the wallet while we're doing stuff
        LOCK2(cs_main, p_walletPtr->cs_wallet);
        EnsureWalletIsUnlocked(p_walletPtr);

        //  Loop through each payable account for the asset, sending it the appropriate portion of the total payout amount
        const size_t MAX_ADDRESSES_PER_TRANSACTION = 100;
        for (size_t ctr = 0; ctr < paymentAddresses.size(); ctr += MAX_ADDRESSES_PER_TRANSACTION) {
            //  Process the address lists in batches
            std::string transactionID;

            if (!InitiateTransfer(p_walletPtr, payoutAmountPerAccount, p_payoutSrc, paymentAddresses, ctr, MAX_ADDRESSES_PER_TRANSACTION, transactionID)) {
                LogPrintf("rewards_thread: Transaction generation failed!\n");
            }
        }

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Batched transaction generation %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

bool InitiateTransfer(
    CWallet * const p_walletPtr, int64_t p_xferAmt, const std::string & p_src,
    const std::vector<std::string> & p_destAddresses, size_t p_offset, size_t p_count,
    std::string & p_resultantTxID)
{
    bool fcnRetVal = false;

    LogPrintf("Initiating transfer...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Ensure that we don't somehow run past the end of the address vector
        if (p_offset >= p_destAddresses.size()) {
            LogPrintf("Out of range issue with destination address list: offset=%u, size=%u\n",
                static_cast<unsigned int>(p_offset), static_cast<unsigned int>(p_destAddresses.size()));
            break;
        }

        //  Transfer the specified amount of the asset from the source to the target
        CAmount nAmount = AmountFromValue(p_xferAmt);
        CCoinControl ctrl;
        CWalletTx transaction;

        //  Handle payouts using RVN differently from those using an asset
        if (p_src.compare("RVN") == 0) {
            // Check amount
            CAmount curBalance = p_walletPtr->GetBalance();

            if (nAmount <= 0) {
                LogPrintf("Invalid amount\n");
                break;
            }

            if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
                LogPrintf("Error: Peer-to-peer functionality missing or disabled\n");
                break;
            }

            std::vector<CRecipient> vDestinations;
            bool errorsOccurred = false;

            for (size_t idx = p_offset; idx < (p_offset + p_count) && idx < p_destAddresses.size(); idx++) {
                // Parse Raven address
                CTxDestination dest = DecodeDestination(p_destAddresses[idx]);
                if (!IsValidDestination(dest)) {
                    LogPrintf("Destination address '%s' is invalid.\n", p_destAddresses[idx].c_str());
                    errorsOccurred = true;
                    continue;
                }

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, nAmount, false};
                vDestinations.push_back(recipient);
            }

            if (errorsOccurred) {
                LogPrintf("Breaking out due to invalid destination address(es)\n");
                break;
            }

            //  Verify funds
            if (nAmount * static_cast<int64_t>(vDestinations.size()) > curBalance) {
                LogPrintf("Insufficient funds\n");
                break;
            }

            // Create and send the transaction
            CReserveKey reservekey(p_walletPtr);
            CAmount nFeeRequired;
            std::string strError;
            int nChangePosRet = -1;

            if (!p_walletPtr->CreateTransaction(vDestinations, transaction, reservekey, nFeeRequired, nChangePosRet, strError, ctrl)) {
                if (nAmount * static_cast<int64_t>(vDestinations.size()) + nFeeRequired > curBalance)
                    strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
                LogPrintf("%s\n", strError.c_str());
                break;
            }

            CValidationState state;
            if (!p_walletPtr->CommitTransaction(transaction, reservekey, g_connman.get(), state)) {
                LogPrintf("Error: The transaction was rejected! Reason given: %s\n", state.GetRejectReason());
                break;
            }
        }
        else {
            std::pair<int, std::string> error;
            std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;

            for (size_t idx = p_offset; idx < (p_offset + p_count) && idx < p_destAddresses.size(); idx++) {
                vDestinations.emplace_back(std::make_pair(CAssetTransfer(p_src, nAmount, DecodeAssetData(""), 0), p_destAddresses[idx]));
            }

            CReserveKey reservekey(p_walletPtr);
            CAmount nRequiredFee;

            // Create the Transaction
            if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, transaction, reservekey, nRequiredFee)) {
                LogPrintf("Failed to create transfer asset transaction: %s\n", error.second.c_str());
                break;
            }

            // Send the Transaction to the network
            if (!SendAssetTransaction(p_walletPtr, transaction, reservekey, error, p_resultantTxID)) {
                LogPrintf("Failed to send asset transaction: %s\n", error.second.c_str());
                break;
            }
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
