// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

/// @file
///
/// ProgPoW API
///
/// This file provides the public API for ProgPoW as the Ethash API extension.

#include <crypto/ethash/include/ethash/ethash.hpp>

namespace progpow
{
using namespace ethash;  // Include ethash namespace.

/// The ProgPoW algorithm revision implemented as specified in the spec
/// https://github.com/ifdefelse/ProgPOW.
constexpr auto revision = "0.9.4";

constexpr int period_length = 3;
constexpr uint32_t num_regs = 32;
constexpr size_t num_lanes = 16;
constexpr int num_cache_accesses = 11;
constexpr int num_math_operations = 18;
constexpr size_t l1_cache_size = 16 * 1024;
constexpr size_t l1_cache_num_items = l1_cache_size / sizeof(uint32_t);

result hash(const epoch_context& context, int block_number, const hash256& header_hash,
    uint64_t nonce) noexcept;

result hash(const epoch_context_full& context, int block_number, const hash256& header_hash,
    uint64_t nonce) noexcept;

bool verify(const epoch_context& context, int block_number, const hash256& header_hash,
    const hash256& mix_hash, uint64_t nonce, const hash256& boundary) noexcept;

hash256 hash_no_verify(const int& block_number, const hash256& header_hash,
    const hash256& mix_hash, const uint64_t& nonce) noexcept;

search_result search_light(const epoch_context& context, int block_number,
    const hash256& header_hash, const hash256& boundary, uint64_t start_nonce,
    size_t iterations) noexcept;

search_result search(const epoch_context_full& context, int block_number,
    const hash256& header_hash, const hash256& boundary, uint64_t start_nonce,
    size_t iterations) noexcept;

}  // namespace progpow
