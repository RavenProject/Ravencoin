// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_ASSETRECORD_H
#define RAVEN_QT_ASSETRECORD_H

#include "amount.h"


/** UI model for unspent assets.
 */
class AssetRecord
{
public:

    AssetRecord():
            name(""), quantity(0)
    {
    }

    AssetRecord(const std::string _name, const CAmount& _quantity):
            name(_name), quantity(_quantity)
    {
    }

    /** @name Immutable attributes
      @{*/
    std::string name;
    CAmount quantity;
    /**@}*/
};

#endif // RAVEN_QT_ASSETRECORD_H
