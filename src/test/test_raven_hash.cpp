// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <hash.h>
#include <utilstrencodings.h>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc == 3)
    {
        std::vector<unsigned char> rawHeader = ParseHex(argv[1]);
        int whichalgo = strtol(argv[2], nullptr, 10);

        std::vector<unsigned char> rawHashPrevBlock(rawHeader.begin() + 4, rawHeader.begin() + 36);
        uint256 hashPrevBlock(rawHashPrevBlock);

        if (whichalgo == 1)
            std::cout << HashX16R(rawHeader.data(), rawHeader.data() + 80, hashPrevBlock).GetHex();
        else if (whichalgo == 2)
            std::cout << HashX16RV2(rawHeader.data(), rawHeader.data() + 80, hashPrevBlock).GetHex();
        else
        {
            std::cerr << "Usage: test_raven_hash blockHex algorithm (1=x16r, 2=x16rv2)" << std::endl;
            return 1;
        }
    }

    else
    {
        std::cerr << "Usage: test_raven_hash blockHex algorithm (1=x16r, 2=x16rv2)" << std::endl;
        return 1;
    }

    return 0;
}