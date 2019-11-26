// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "myrestrictedassetrecord.h"

#include "assets/assets.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include <stdint.h>

#include <QDebug>


/* Return positive answer if transaction should be shown in list.
 */
bool MyRestrictedAssetRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}


/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<MyRestrictedAssetRecord> MyRestrictedAssetRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<MyRestrictedAssetRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    for(unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
        const CTxOut &txout = wtx.tx->vout[i];
        isminetype mine = ISMINE_NO;

        if (txout.scriptPubKey.IsNullAssetTxDataScript()) {
            CNullAssetTxData data;
            std::string address;
            if (!AssetNullDataFromScript(txout.scriptPubKey, data, address)) {
                continue;
            }
            mine = IsMine(*wallet, DecodeDestination(address));
            if (mine & ISMINE_ALL) {
                MyRestrictedAssetRecord sub(hash, nTime);
                sub.involvesWatchAddress = mine & ISMINE_SPENDABLE ? false : true;
                sub.assetName = data.asset_name;
                sub.address = address;

                if (IsAssetNameAQualifier(data.asset_name)) {
                    if (data.flag == (int) QualifierType::ADD_QUALIFIER) {
                        sub.type = MyRestrictedAssetRecord::Type::Tagged;
                    } else {
                        sub.type = MyRestrictedAssetRecord::Type::UnTagged;
                    }
                } else if (IsAssetNameAnRestricted(data.asset_name)) {
                    if (data.flag == (int) RestrictedType::FREEZE_ADDRESS) {
                        sub.type = MyRestrictedAssetRecord::Type::Frozen;
                    } else {
                        sub.type = MyRestrictedAssetRecord::Type::UnFrozen;
                    }
                }
                parts.append(sub);
            }
        }
    }
    return parts;
}
QString MyRestrictedAssetRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int MyRestrictedAssetRecord::getOutputIndex() const
{
    return idx;
}
