// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RIP-25: Bech32m encoding for post-quantum witness v2 addresses (BIP350)

#include "bech32.h"

namespace bech32
{

namespace
{

const char* CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

const int8_t CHARSET_REV[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    15, -1, 10, 17, 21, 20, 26, 30,  7,  5, -1, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1
};

/** Bech32 constant: 1, Bech32m constant: 0x2bc830a3 */
uint32_t EncodingConstant(Encoding encoding) {
    if (encoding == BECH32) return 1;
    return 0x2bc830a3; // BECH32M
}

uint32_t PolyMod(const std::vector<uint8_t>& v)
{
    uint32_t c = 1;
    for (const auto v_i : v) {
        uint8_t c0 = c >> 25;
        c = ((c & 0x1ffffff) << 5) ^ v_i;
        if (c0 & 1)  c ^= 0x3b6a57b2;
        if (c0 & 2)  c ^= 0x26508e6d;
        if (c0 & 4)  c ^= 0x1ea119fa;
        if (c0 & 8)  c ^= 0x3d4233dd;
        if (c0 & 16) c ^= 0x2a1462b3;
    }
    return c;
}

std::vector<uint8_t> HrpExpand(const std::string& hrp)
{
    std::vector<uint8_t> ret;
    ret.reserve(hrp.size() + 1 + hrp.size());
    for (size_t i = 0; i < hrp.size(); ++i) {
        ret.push_back(hrp[i] >> 5);
    }
    ret.push_back(0);
    for (size_t i = 0; i < hrp.size(); ++i) {
        ret.push_back(hrp[i] & 0x1f);
    }
    return ret;
}

bool VerifyChecksum(const std::string& hrp, const std::vector<uint8_t>& values, Encoding& enc)
{
    std::vector<uint8_t> exp = HrpExpand(hrp);
    exp.insert(exp.end(), values.begin(), values.end());
    uint32_t res = PolyMod(exp);
    if (res == EncodingConstant(BECH32)) {
        enc = BECH32;
        return true;
    }
    if (res == EncodingConstant(BECH32M)) {
        enc = BECH32M;
        return true;
    }
    return false;
}

std::vector<uint8_t> CreateChecksum(Encoding encoding, const std::string& hrp, const std::vector<uint8_t>& values)
{
    std::vector<uint8_t> enc = HrpExpand(hrp);
    enc.insert(enc.end(), values.begin(), values.end());
    enc.resize(enc.size() + 6, 0);
    uint32_t mod = PolyMod(enc) ^ EncodingConstant(encoding);
    std::vector<uint8_t> ret(6);
    for (size_t i = 0; i < 6; ++i) {
        ret[i] = (mod >> (5 * (5 - i))) & 31;
    }
    return ret;
}

} // namespace

std::string Encode(Encoding encoding, const std::string& hrp, const std::vector<uint8_t>& values)
{
    std::vector<uint8_t> checksum = CreateChecksum(encoding, hrp, values);
    std::string ret = hrp + '1';
    ret.reserve(ret.size() + values.size() + checksum.size());
    for (const auto c : values) {
        ret += CHARSET[c];
    }
    for (const auto c : checksum) {
        ret += CHARSET[c];
    }
    return ret;
}

DecodeResult Decode(const std::string& str)
{
    DecodeResult result = {INVALID, "", {}};

    bool lower = false, upper = false;
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c >= 'a' && c <= 'z') lower = true;
        if (c >= 'A' && c <= 'Z') upper = true;
        if (c < 33 || c > 126) return result;
    }
    if (lower && upper) return result;

    size_t pos = str.rfind('1');
    if (pos == str.npos || pos == 0 || pos + 7 > str.size() || str.size() > 90) {
        return result;
    }

    std::string hrp;
    for (size_t i = 0; i < pos; ++i) {
        hrp += (str[i] >= 'A' && str[i] <= 'Z') ? (str[i] - 'A' + 'a') : str[i];
    }

    std::vector<uint8_t> values;
    values.reserve(str.size() - 1 - pos);
    for (size_t i = pos + 1; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c > 127) return result;
        int8_t rev = CHARSET_REV[c];
        if (rev == -1) return result;
        values.push_back(rev);
    }

    Encoding enc;
    if (!VerifyChecksum(hrp, values, enc)) return result;

    result.encoding = enc;
    result.hrp = hrp;
    result.data.assign(values.begin(), values.end() - 6);
    return result;
}

} // namespace bech32
