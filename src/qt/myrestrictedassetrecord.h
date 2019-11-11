// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "uint256.h"

#include <QList>
#include <QString>

class CWallet;
class CWalletTx;

/** UI model for a transaction. A core transaction can be represented by multiple UI transactions if it has
    multiple outputs.
 */
class MyRestrictedAssetRecord
{
public:
    enum Type
    {
        Other,
        Frozen,
        UnFrozen,
        Tagged,
        UnTagged,
    };

    /** Number of confirmation recommended for accepting a transaction */
    static const int RecommendedNumConfirmations = 6;

    MyRestrictedAssetRecord():
            hash(), time(0), type(Other), address(""), assetName("RVN"), idx(0)
    {
    }

    MyRestrictedAssetRecord(uint256 _hash, qint64 _time):
            hash(_hash), time(_time), type(Other), address(""), assetName("RVN"), idx(0)
    {
    }

    MyRestrictedAssetRecord(uint256 _hash, qint64 _time,
                      Type _type, const std::string &_address,
                      const CAmount& _debit, const CAmount& _credit):
            hash(_hash), time(_time), type(_type), address(_address),
            assetName("RVN"), idx(0)
    {
    }

    /** Decompose CWallet transaction to model transaction records.
     */
    static bool showTransaction(const CWalletTx &wtx);
    static QList<MyRestrictedAssetRecord> decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx);

    /** @name Immutable transaction attributes
      @{*/
    uint256 hash;
    qint64 time;
    Type type;
    std::string address;
    std::string assetName;
    /**@}*/

    /** Subtransaction index, for sort key */
    int idx;

    /** Whether the transaction was sent/received with a watch-only address */
    bool involvesWatchAddress;

    /** Return the unique identifier for this transaction (part) */
    QString getTxID() const;

    /** Return the output index of the subtransaction  */
    int getOutputIndex() const;

};

