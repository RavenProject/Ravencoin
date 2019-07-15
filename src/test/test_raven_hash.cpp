// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <stdio.h>
#include <string.h>
#include <utilstrencodings.h>

#include "hash.h"
#include "crypto/warnings.h"

using namespace std;
using namespace crypto;
typedef crypto::hash chash;

struct V4_Data
{
    const void* data;
    size_t length;
    uint64_t height;
};

PUSH_WARNINGS
DISABLE_VS_WARNINGS(4297)
extern "C" {
    static void cn_slow_hash_4(const void *data, size_t, char *hash) {
        const V4_Data* p = reinterpret_cast<const V4_Data*>(data);
        return cn_slow_hash(p->data, p->length, hash, 4/*variant*/, 0/*prehashed*/, p->height);
    }
}
POP_WARNINGS

int main(int argc, char **argv)
{
    if (argc == 2)
    {
        std::vector<unsigned char> rawHeader = ParseHex(argv[1]);

        std::vector<unsigned char> rawHashPrevBlock(rawHeader.begin() + 4, rawHeader.begin() + 36);
        uint256 hashPrevBlock(rawHashPrevBlock);

        std::cout << "X16R: " << HashX16R(rawHeader.data(), rawHeader.data() + 80, hashPrevBlock).GetHex() << std::endl;

        V4_Data d;
        d.data = &rawHeader;
        d.length = rawHeader.size();

        // get height from test data / command line (using 0 for now)
//        get(input, d.height);
        d.height = 0;

        chash actual;

        cn_slow_hash_4(&d, 0, (char *) &actual);

        std::cout << "CN4: " << (char *) &actual << std::endl;
    } else
    {
        std::cerr << "Usage: test_raven_hash blockHex" << std::endl;
        return 1;
    }

    return 0;
}