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
    CWallet * const p_walletPtr, const CAmount p_paymentAmt,
    const std::string & p_ownedAssetName, const std::string & p_payoutSrcName, const std::string & p_exceptionAddresses);

//  Transfer the specified amount of the asset from the source to the target
bool InitiateTransfer(
    CWallet * const p_walletPtr, const std::string & p_src,
    const std::vector<std::pair<std::string, CAmount>> & p_addrAmtPairs,
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
    if (passets == nullptr) {
        LogPrintf("rewards_thread: Invalid assets cache!\n");
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
                LogPrintf("rewards_thread: Failed to process batched transactions for '%s'!\n",
                    rewardEntry.tgtAssetName.c_str());

                //  Regardless of success or failure, ensure that this reward entry is deleted
                deleteReward = true;
            }

            //  Delete the reward entry
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
    CWallet * const p_walletPtr, const CAmount p_paymentAmt,
    const std::string & p_ownedAssetName, const std::string & p_payoutSrcName, const std::string & p_exceptionAddresses)
{
    bool fcnRetVal = false;

    LogPrintf("Generating batched transactions...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Get details on the specified source asset
        CNewAsset srcAsset;
        bool srcIsIndivisible = false;
        CAmount srcUnitMultiplier = COIN;  //  Default to multiplier for RVN

        if (p_payoutSrcName.compare("RVN") != 0) {
            if (!passets->GetAssetMetaDataIfExists(p_payoutSrcName, srcAsset)) {
                LogPrintf("Failed to retrieve asset details for '%s'\n", p_payoutSrcName.c_str());
                break;
            }

            LogPrintf("Source asset '%s' has units %d and multiplier %d\n",
                p_payoutSrcName.c_str(), srcAsset.units, srcUnitMultiplier);

            //  If the token is indivisible, signal this to later code with a zero multiplier
            if (srcAsset.units == 0) {
                srcIsIndivisible = true;
                srcUnitMultiplier = 1;
            }
            else {
                srcUnitMultiplier = pow(10, srcAsset.units);
            }
        }
        else {
            LogPrintf("Source is RVN with multiplier %d\n", srcUnitMultiplier);
        }

        //  Get details on the target asset
        CNewAsset tgtAsset;
        CAmount tgtUnitMultiplier = 0;
        if (!passets->GetAssetMetaDataIfExists(p_ownedAssetName, tgtAsset)) {
            LogPrintf("Failed to retrieve asset details for '%s'\n", p_ownedAssetName.c_str());
            break;
        }

        //  If the token is indivisible, signal this to later code with a zero multiplier
        if (tgtAsset.units == 0) {
            tgtUnitMultiplier = 1;
        }
        else {
            tgtUnitMultiplier = pow(10, tgtAsset.units);
        }

        LogPrintf("Target asset '%s' has units %d and multiplier %d\n",
            p_ownedAssetName.c_str(), tgtAsset.units, tgtUnitMultiplier);

        //  Split up the exception address string
        std::vector<std::string> exceptionAddressList;
        boost::split(exceptionAddressList, p_exceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

        //  Retrieve the payable owners for the specified asset
        //  This is done in batches. First, the total count is retrieved, then all of the addresses.
        std::vector<std::pair<std::string, CAmount>> addressAmtPairs;
        int totalEntryCount = 0;

        //  Step 0 - Retrieve the total amount of the target asset not owned by exception addresses
        LogPrintf("Retrieving total non-exception amount...\n");

        CAmount totalAssetAmt = 0;

        if (!passetsdb->AssetTotalAmountNotExcluded(p_ownedAssetName, exceptionAddressList, totalAssetAmt)) {
            LogPrintf("Failed to retrieve total asset amount for '%s'\n", p_ownedAssetName.c_str());
            break;
        }
        if (totalAssetAmt <= 0) {
            LogPrintf("No instances of asset '%s' are owned by non-exception addresses\n", p_ownedAssetName.c_str());
            break;
        }

        LogPrintf("Total non-exception amount is %d.%08d\n",
            totalAssetAmt / tgtUnitMultiplier, totalAssetAmt % tgtUnitMultiplier);

        //  If the asset is indivisible, and there are fewer rewards than individual ownerships,
        //      fail the distribution, since it is not possible to reward all ownerships equally.
        if (srcIsIndivisible && p_paymentAmt < totalAssetAmt) {
            LogPrintf("Source asset '%s' is indivisible, and not enough reward value was provided for all ownership of '%s'\n",
                p_payoutSrcName.c_str(), p_ownedAssetName.c_str());
            break;
        }


        //  Step 1 - find out how many addresses currently exist
        LogPrintf("Retrieving payable owners...\n");

        if (!passetsdb->AssetAddressDir(addressAmtPairs, totalEntryCount, true, p_ownedAssetName, INT_MAX, 0)) {
            LogPrintf("Failed to retrieve assets directory for '%s'\n", p_ownedAssetName.c_str());
            break;
        }

        //  Step 2 - retrieve all of the addresses in batches
        const int MAX_RETRIEVAL_COUNT = 100;
        std::vector<std::string> paymentAddresses;

        for (int retrievalOffset = 0; retrievalOffset < totalEntryCount; retrievalOffset += MAX_RETRIEVAL_COUNT) {
            //  Retrieve the specified segment of addresses
            addressAmtPairs.clear();

            if (!passetsdb->AssetAddressDir(addressAmtPairs, totalEntryCount, false, p_ownedAssetName, MAX_RETRIEVAL_COUNT, retrievalOffset)) {
                LogPrintf("Failed to retrieve assets directory for '%s'\n", p_ownedAssetName.c_str());
                break;
            }

            //  Verify that some addresses were returned
            if (addressAmtPairs.size() == 0) {
                LogPrintf("No addresses were retrieved.\n");
                continue;
            }

            //  Remove exception addresses from the list
            std::vector<std::pair<std::string, CAmount>>::iterator entryIT;

            for (entryIT = addressAmtPairs.begin(); entryIT != addressAmtPairs.end(); ) {
                bool isExceptionAddress = false;

                for (auto const & exceptionAddr : exceptionAddressList) {
                    if (entryIT->first.compare(exceptionAddr) == 0) {
                        isExceptionAddress = true;
                        break;
                    }
                }

                if (isExceptionAddress) {
                    entryIT = addressAmtPairs.erase(entryIT);
                }
                else {
                    ++entryIT;
                }
            }

            //  Make sure we have some addresses to pay to
            if (addressAmtPairs.size() == 0) {
                LogPrintf("Current batch includes only exception addresses.\n");
                continue;
            }

            //  Calculate the per-address payout amount based on their ownership
            double floatingPaymentAmt = p_paymentAmt;
            double floatingTotalAssetAmt = totalAssetAmt;
            for (auto & addressAmtPair : addressAmtPairs) {
                //  Replace the ownership amount with the reward amount
                double floatingCurrAssetAmt = addressAmtPair.second;
                double floatingRewardAmt = floatingPaymentAmt * floatingCurrAssetAmt / floatingTotalAssetAmt;
                CAmount rewardAmt = floatingRewardAmt;

                LogPrintf("Found ownership address for '%s': '%s' owns %d.%08d => reward %d.%08d\n",
                    p_ownedAssetName.c_str(), addressAmtPair.first.c_str(),
                    addressAmtPair.second / tgtUnitMultiplier, addressAmtPair.second % tgtUnitMultiplier,
                    rewardAmt / srcUnitMultiplier, rewardAmt % srcUnitMultiplier);

                addressAmtPair.second = rewardAmt;
            }

            //  Lock the wallet while we're generating a transaction
            LOCK2(cs_main, p_walletPtr->cs_wallet);
            EnsureWalletIsUnlocked(p_walletPtr);

            //  Loop through each payable account for the asset, sending it the appropriate portion of the total payout amount
            std::string transactionID;
            if (!InitiateTransfer(p_walletPtr, p_payoutSrcName, addressAmtPairs, transactionID)) {
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
    CWallet * const p_walletPtr, const std::string & p_src,
    const std::vector<std::pair<std::string, CAmount>> & p_addrAmtPairs,
    std::string & p_resultantTxID)
{
    bool fcnRetVal = false;

    LogPrintf("Initiating transfer...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Transfer the specified amount of the asset from the source to the target
        CCoinControl ctrl;
        CWalletTx transaction;

        //  Handle payouts using RVN differently from those using an asset
        if (p_src.compare("RVN") == 0) {
            // Check amount
            CAmount curBalance = p_walletPtr->GetBalance();

            if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
                LogPrintf("Error: Peer-to-peer functionality missing or disabled\n");
                break;
            }

            std::vector<CRecipient> vDestinations;
            bool errorsOccurred = false;
            CAmount totalPaymentAmt = 0;

            for (auto const & addrAmtPair : p_addrAmtPairs) {
                // Parse Raven address
                CTxDestination dest = DecodeDestination(addrAmtPair.first);
                if (!IsValidDestination(dest)) {
                    LogPrintf("Destination address '%s' is invalid.\n", addrAmtPair.first.c_str());
                    errorsOccurred = true;
                    continue;
                }

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, addrAmtPair.second, false};
                vDestinations.emplace_back(recipient);

                totalPaymentAmt += addrAmtPair.second;
            }

            if (errorsOccurred) {
                LogPrintf("Breaking out due to invalid destination address(es)\n");
                break;
            }

            //  Verify funds
            if (totalPaymentAmt > curBalance) {
                LogPrintf("Insufficient funds\n");
                break;
            }

            // Create and send the transaction
            CReserveKey reservekey(p_walletPtr);
            CAmount nFeeRequired;
            std::string strError;
            int nChangePosRet = -1;

            if (!p_walletPtr->CreateTransaction(vDestinations, transaction, reservekey, nFeeRequired, nChangePosRet, strError, ctrl)) {
                if (totalPaymentAmt + nFeeRequired > curBalance)
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

            for (auto const & addrAmtPair : p_addrAmtPairs) {
                LogPrintf("Sending asset '%s' to address '%s' as reward %d\n",
                    p_src.c_str(), addrAmtPair.first.c_str(),
                    addrAmtPair.second);

                vDestinations.emplace_back(std::make_pair(CAssetTransfer(p_src, addrAmtPair.second, DecodeAssetData(""), 0), addrAmtPair.first));
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
