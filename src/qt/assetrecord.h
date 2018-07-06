// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_ASSETRECORD_H
#define RAVEN_QT_ASSETRECORD_H

#include "math.h"
#include "amount.h"


/** UI model for unspent assets.
 */
class AssetRecord
{
public:

    AssetRecord():
            name(""), quantity(0), units(0)
    {
    }

    AssetRecord(const std::string _name, const CAmount& _quantity, const int _units):
            name(_name), quantity(_quantity), units(_units)
    {
    }


    std::string formatted(){
        char formatted[100];
        if (units == 0) {
            sprintf(formatted, "%lld", quantity);
        } else {
            CAmount unit_pow = ipow(10,units);
            sprintf(formatted, "%lld.%lld", quantity / unit_pow, quantity % unit_pow);
        }
        return(formatted);
    }

    /** @name Immutable attributes
      @{*/
    std::string name;
    CAmount quantity;
    int units;
    /**@}*/

private: 
    CAmount ipow(CAmount base, CAmount exp)
    {
        CAmount result = 1;
        for (;;)
        {
            if (exp & 1)
                result *= base;
            exp >>= 1;
            if (!exp)
                break;
            base *= base;
        }

        return result;
    }
};

#endif // RAVEN_QT_ASSETRECORD_H
