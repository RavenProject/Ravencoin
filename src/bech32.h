// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: Bech32m encoding for post-quantum witness v2 addresses (BIP350)

#ifndef RAVEN_BECH32_H
#define RAVEN_BECH32_H

#include <stdint.h>
#include <string>
#include <vector>

namespace bech32
{

enum Encoding {
    INVALID,
    BECH32,   // BIP173
    BECH32M,  // BIP350
};

/** Encode a Bech32 or Bech32m string. */
std::string Encode(Encoding encoding, const std::string& hrp, const std::vector<uint8_t>& values);

/** Decode a Bech32 or Bech32m string. Returns (encoding, hrp, data). */
struct DecodeResult {
    Encoding encoding;
    std::string hrp;
    std::vector<uint8_t> data;
};
DecodeResult Decode(const std::string& str);

} // namespace bech32

#endif // RAVEN_BECH32_H
