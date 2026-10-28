#!/usr/bin/env bash
set -euo pipefail

fail() {
  echo "RIP-25/v4.8 invariant failure: $*" >&2
  exit 1
}

require_fixed() {
  local needle="$1" file="$2" message="$3"
  grep -Fq -- "$needle" "$file" || fail "$message"
}

reject_fixed() {
  local needle="$1" file="$2" message="$3"
  if grep -Fq -- "$needle" "$file"; then fail "$message"; fi
}

require_text() {
  local text="$1" needle="$2" message="$3"
  grep -Fq -- "$needle" <<<"$text" || fail "$message"
}

require_min_count() {
  local needle="$1" file="$2" minimum="$3" message="$4"
  local count
  count="$(grep -Fc -- "$needle" "$file" || true)"
  (( count >= minimum )) || fail "$message"
}

mode="${1:---run-tests}"
if (( $# > 1 )); then
  fail 'usage: check-rip25-v48-invariants.sh [--structural-only|--run-tests]'
fi
case "$mode" in
  --structural-only|--run-tests) ;;
  *) fail 'usage: check-rip25-v48-invariants.sh [--structural-only|--run-tests]' ;;
esac

# Approved RIP-25 protocol architecture.
require_fixed 'DEPLOYMENT_PQ_HYBRID' src/consensus/params.h 'PQ BIP9 deployment missing'
require_fixed 'vDeployments[Consensus::DEPLOYMENT_PQ_HYBRID].bit = 12' src/chainparams.cpp 'PQ deployment must remain on BIP9 bit 12'
require_fixed '  bit:                                    12' doc/RIP-0025-PQ-Signatures.md 'RIP-25 specification must document BIP9 bit 12'
require_fixed 'MAX_BLOCK_WEIGHT_RIP2 = 8000000' src/consensus/consensus.h 'pre-RIP-25 limit must remain 8 MWU'
require_fixed 'MAX_BLOCK_WEIGHT_RIP25_PHASE1 = 12000000' src/consensus/consensus.h 'RIP-25 phase 1 must remain 12 MWU'
require_fixed 'MAX_BLOCK_WEIGHT_RIP25_PHASE2 = 16000000' src/consensus/consensus.h 'RIP-25 phase 2 must remain 16 MWU'
require_fixed 'PQ_WITNESS_SCALE_FACTOR = 8' src/consensus/consensus.h 'PQ witness discount must remain 8x'
require_fixed 'witversion == 2 && (flags & SCRIPT_VERIFY_PQ_HYBRID)' src/script/interpreter.cpp 'witness-v2 ML-DSA verifier missing'
require_fixed 'mldsa::PUBLICKEY_BYTES' src/script/interpreter.cpp 'ML-DSA-44 public-key size check missing'
require_fixed 'mldsa::SIGNATURE_BYTES' src/script/interpreter.cpp 'ML-DSA-44 signature size check missing'
reject_fixed 'fPQHybridIsActive' src/consensus/consensus.h 'forbidden mutable/static PQ activation state'
reject_fixed 'SetPQHybridBlockLimitsActive' src/consensus/consensus.h 'forbidden mutable block-limit state'

# GLM-002: contextual BIP9 activation must reach consensus script flags.
block_flags="$(sed -n '/^static unsigned int GetBlockScriptFlags(/,/^[[:space:]]*return flags;/p' src/validation.cpp)"
require_text "$block_flags" 'IsPQHybridActiveLocked(pindex->pprev, consensusparams)' 'GetBlockScriptFlags is not driven by contextual PQ BIP9 state'
require_text "$block_flags" 'flags |= SCRIPT_VERIFY_PQ_HYBRID' 'GetBlockScriptFlags does not enable PQ verification after activation'
require_fixed 'unsigned int flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus())' src/validation.cpp 'ConnectBlock does not use contextual script flags'
require_fixed 'scriptVerifyFlags |= SCRIPT_VERIFY_PQ_HYBRID' src/validation.cpp 'active mempool validation does not enable PQ verification'
require_fixed 'premature-pq-witness' src/validation.cpp 'pre-activation witness-v2 output relay rejection missing'
require_fixed 'witness.stack.size() != 2' src/script/interpreter.cpp 'active witness-v2 must require exactly two witness elements'
require_fixed 'SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED' src/script/interpreter.cpp 'invalid ML-DSA signatures are not rejected'
sigop_function="$(sed -n '/^size_t static WitnessSigOps(/,/^}/p' src/script/interpreter.cpp)"
require_text "$sigop_function" '(flags & SCRIPT_VERIFY_PQ_HYBRID)' 'witness-v2 sigops are not activation-gated'
if grep -A30 'STANDARD_SCRIPT_VERIFY_FLAGS' src/policy/policy.h | grep -Fq 'SCRIPT_VERIFY_PQ_HYBRID'; then
  fail 'SCRIPT_VERIFY_PQ_HYBRID must not be unconditional in standard flags'
fi

# RIP-25 protects native RVN only. Asset-bearing outputs retain the legacy
# P2PKH envelope until a separately specified and activated extension exists.
reject_fixed 'All asset operations work with both legacy and PQ addresses' doc/RIP-0025-PQ-Signatures.md 'RIP-25 specification still claims unsupported PQ asset protection'
require_fixed 'RIP-25 witness-v2 protects native RVN outputs only' doc/RIP-0025-PQ-Signatures.md 'RIP-25 asset scope is not documented'
require_fixed 'bool IsSupportedAssetDestination' src/script/standard.cpp 'asset construction lacks a central legacy-destination predicate'
require_fixed 'RIP-25 witness-v2 destinations protect native RVN only' src/assets/assets.cpp 'wallet asset builder does not reject unsupported PQ destinations explicitly'
require_fixed 'RIP-25 witness-v2 destinations protect native RVN only' src/rpc/rawtransaction.cpp 'raw asset builder does not reject unsupported PQ destinations explicitly'
require_fixed 'needsAssetChangeScript' src/wallet/wallet.cpp 'wallet does not separate native RVN change from legacy asset change'
require_fixed '!IsSupportedAssetDestination(destination)' src/wallet/wallet.cpp 'wallet asset entry points accept unsupported destination types'
require_fixed 'pq_asset_envelope_is_not_witness_v2_test' src/test/assets/asset_tx_tests.cpp 'PQ-plus-asset consensus rejection regression missing'
require_fixed 'Testing PQ native RVN change with independent legacy asset change' test/functional/rpc_assettransfer.py 'native-PQ plus legacy-asset change regression missing'
require_fixed "'rpc_assettransfer.py'" test/functional/test_runner.py 'asset destination functional regression is not in the functional suite'
require_fixed 'RIP-0025-PQ-Assets-Followup.md' doc/RIP-0025-PQ-Signatures.md 'PQ asset follow-up design is not linked from the specification'
require_fixed 'vDeployments[Consensus::DEPLOYMENT_PQ_ASSETS].bit = 13' src/chainparams.cpp 'dependent PQ asset deployment must use bit 13'
require_fixed 'CheckTxPQAssets(tx, state, view, pqAssetActivationHeight' src/validation.cpp 'PQ asset rule must run against candidate UTXOs'
require_fixed 'feature_pq_asset_anchor.py' test/functional/test_runner.py 'PQ asset full-chain regression is not registered'
require_fixed 'feature_pq_asset_authority_selection.py' test/functional/test_runner.py 'PQ authority selection regression is not registered'
require_fixed 'feature_pq_asset_authority_selection.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits PQ authority selection regression'
require_fixed 'feature_pq_asset_authority_selection.py' .github/workflows/build-raven.yml 'release workflow omits PQ authority selection regression'
require_fixed 'feature_pq_asset_source_rpc.py' test/functional/test_runner.py 'PQ asset source RPC regression is not registered'
require_fixed 'feature_pq_asset_source_rpc.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits the PQ asset source RPC regression'
require_fixed 'feature_pq_asset_source_rpc.py' .github/workflows/build-raven.yml 'release workflow omits the PQ asset source RPC regression'
require_fixed 'feature_pq_asset_wallet_ownership.py' test/functional/test_runner.py 'PQ asset wallet ownership regression is not registered'
require_fixed 'feature_mempool_clear_reissue.py' test/functional/test_runner.py 'mempool clear reissue regression is not registered'
require_fixed 'feature_mempool_reissue_restart.py' test/functional/test_runner.py 'mempool reissue restart regression is not registered'
require_fixed "'feature_rewards_restart.py'" test/functional/test_runner.py 'reward restart regression is not registered'
require_fixed 'feature_rewards_restart.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits reward restart regression'
require_fixed 'feature_rewards_restart.py' .github/workflows/build-raven.yml 'release workflow omits reward restart regression'
require_fixed "'wallet_pq_recovery.py'" test/functional/test_runner.py 'PQ wallet recovery regression is not registered'
require_fixed 'wallet_pq_recovery.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits PQ wallet recovery regression'
require_fixed 'wallet_pq_recovery.py' .github/workflows/build-raven.yml 'release workflow omits PQ wallet recovery regression'
require_fixed "'wallet_restricted_multiwallet.py'" test/functional/test_runner.py 'restricted multiwallet regression is not registered'
require_fixed 'wallet_restricted_multiwallet.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits restricted multiwallet regression'
require_fixed 'wallet_restricted_multiwallet.py' .github/workflows/build-raven.yml 'release workflow omits restricted multiwallet regression'
require_fixed "'wallet_messaging_multiwallet.py'" test/functional/test_runner.py 'messaging multiwallet regression is not registered'
require_fixed 'wallet_messaging_multiwallet.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits messaging multiwallet regression'
require_fixed 'wallet_messaging_multiwallet.py' .github/workflows/build-raven.yml 'release workflow omits messaging multiwallet regression'
require_fixed "'feature_pq_activation_reorg_sigops.py'" test/functional/test_runner.py 'PQ activation reorg and sigop regression is not registered'
require_fixed 'feature_pq_activation_reorg_sigops.py' .github/workflows/rip25-v48-final-gate.yml 'final gate omits PQ activation reorg and sigop regression'
require_fixed 'feature_pq_activation_reorg_sigops.py' .github/workflows/build-raven.yml 'release workflow omits PQ activation reorg and sigop regression'

# GLM-003: contextual 8 -> 12 -> 16 MWU and UTXO-bound 8x discount.
require_fixed 'VersionBitsStateSinceHeight' src/validation.cpp 'deterministic RIP-25 phase boundary missing'
require_fixed 'return MAX_BLOCK_WEIGHT_RIP2;' src/validation.cpp '8 MWU pre-activation branch missing'
require_fixed 'return MAX_BLOCK_WEIGHT_RIP25_PHASE1' src/validation.cpp '12 MWU phase-1 branch missing'
require_fixed 'return MAX_BLOCK_WEIGHT_RIP25_PHASE2' src/validation.cpp '16 MWU phase-2 branch missing'
require_fixed 'IsPQWitnessV2Prevout' src/validation.cpp 'PQ discount is not bound to a witness-v2 prevout'
require_fixed 'GetContextualPQWitnessDiscount' src/validation.cpp 'UTXO-bound PQ discount calculation missing'
require_fixed 'contextualBlockWeight -= GetContextualPQWitnessDiscount(tx, view)' src/validation.cpp 'ConnectBlock does not apply the contextual PQ discount'
discount_guard="$(grep -A1 -F 'if (pqWitnessDiscountActive)' src/validation.cpp | sed 's/^[[:space:]]*//')"
[[ "$discount_guard" == $'if (pqWitnessDiscountActive)\ncontextualBlockWeight -= GetContextualPQWitnessDiscount(tx, view);' ]] || fail 'ConnectBlock does not apply the contextual PQ discount'
require_fixed 'contextualBlockWeight > activeBlockWeightLimit' src/validation.cpp 'ConnectBlock does not enforce the active contextual limit'
require_fixed 'preliminaryWeight > activeWeightLimit' src/validation.cpp 'contextual preliminary block-weight check missing'
require_fixed 'const size_t activeMaxWeight = GetMaxBlockWeightForPrev(pindexPrev, chainparams.GetConsensus())' src/miner.cpp 'miner does not query the active contextual limit'
require_fixed 'std::min<size_t>(nBlockMaxWeight, activeMaxWeight - 4000)' src/miner.cpp 'miner is not clamped below the active contextual limit'
require_fixed 'GetMaxBlockSerializedSizeForPrev(pindexPrev, chainparams.GetConsensus())' src/miner.cpp 'miner does not query the contextual serialized-size limit'
require_fixed 'GetContextualTransactionWeight(tx, view, fApplyPQDiscount)' src/miner.cpp 'miner weight is not bound to the UTXO context'
require_fixed 'pblocktemplate->vTxWeights.push_back(resources.weight)' src/miner.cpp 'miner does not retain selected contextual transaction weights'
require_fixed 'nBlockSerializedSize + resources.serializedSize' src/miner.cpp 'miner does not enforce serialized bytes while selecting packages'
gbt_function="$(sed -n '/^UniValue getblocktemplate(/,/^class submitblock_StateCatcher/p' src/rpc/mining.cpp)"
require_text "$gbt_function" 'GetMaxBlockSerializedSizeForPrev(pindexPrev, consensusParams)' 'GBT size limit is not derived from the template parent'
require_text "$gbt_function" 'GetMaxBlockWeightForPrev(pindexPrev, consensusParams)' 'GBT weight limit is not derived from the template parent'
require_text "$gbt_function" 'pblocktemplate->vTxWeights.at(index_in_template)' 'GBT transaction entries do not report selected contextual weight'
require_fixed 'gbt_entries_report_contextual_pq_weight' src/test/miner_tests.cpp 'GBT per-entry contextual weight regression is missing'
if grep -Fq 'nSizeLimit = GetMaxBlockSerializedSize()' <<<"$gbt_function" ||
   grep -Fq '"weightlimit", (int64_t)GetMaxBlockWeight()' <<<"$gbt_function"; then
  fail 'GBT advertises structural ceilings instead of contextual next-block limits'
fi

# Remediated high-risk resource paths. These checks are structural lint; the
# executable tests below are the security evidence.
require_fixed 'MAX_BLOCK_WEIGHT_RIP25_PHASE2 / MIN_TRANSACTION_INPUT_WEIGHT' src/undo.h 'undo deserialization does not use the structural 16-MWU bound'
reject_fixed 'fCheckTransferOverflowIsActive' src/consensus/consensus.h 'forbidden sticky transfer-overflow activation state'
require_fixed 'const bool fTransferOverflowActive' src/consensus/tx_verify.h 'asset overflow validation lacks an explicit contextual gate'
require_fixed 'IsTransferOverflowCheckActiveLocked(pindex->pprev' src/validation.cpp 'block validation does not derive transfer-overflow state from the candidate parent'
require_fixed 'mapOwnerUsage' src/net.h 'P2P receive accounting lacks per-owner fairness'
require_fixed 'nMaxProtectedBulkSize' src/net.h 'P2P receive accounting lacks a protected outbound class'
require_fixed 'DEFAULT_OWNER_HEADROOM = 64 * 1024' src/net.h 'P2P owners lack bounded control-message headroom'
require_fixed 'memoryBuffer.TryReserve(memoryOwner, memoryProtected' src/net.cpp 'P2P message allocations bypass owner-aware accounting'
require_fixed 'memusage::MallocUsage(sizeof(CNetMessage) + 2 * sizeof(void*))' src/net.cpp 'P2P message/list objects are not globally charged'
require_fixed 'memusage::MallocUsage(hdrbuf.capacity())' src/net.cpp 'P2P header allocations are not globally charged'
require_fixed 'memusage::MallocUsage(vRecv.capacity())' src/net.cpp 'P2P payload capacity is not charged at allocator size'
require_fixed 'memoryBuffer.Release(memoryOwner, memoryProtected, GetMemoryUsage())' src/net.cpp 'P2P message RAII ownership does not release on destruction'
require_fixed 'MoveCompletedMessagesToProcessQueue' src/net.cpp 'P2P receive-to-process ownership handoff is missing'
require_fixed 'nProcessQueueSize -= msgs.front().GetMemoryUsage()' src/net_processing.cpp 'P2P processing queue does not use the owned memory charge'
reject_fixed 'recvBuffer.Release(msg.vRecv.capacity())' src/net.cpp 'P2P payload ownership is released before processing'
reject_fixed 'nCopy + 256 * 1024' src/net.cpp 'one-byte P2P input still receives speculative 256-KiB allocation'
require_fixed 'CWitnessStack stack;' src/script/script.h 'witness parsing still uses one vector object per wire element'
require_fixed 'CHECKPOINT_INTERVAL = 256' src/script/witness.h 'compact witness representation lacks bounded random-access checkpoints'
require_fixed 'READ_CHUNK_SIZE = 64 * 1024' src/script/witness.h 'witness elements can allocate from an unreceived advertised length'
require_fixed 'parsed.m_compactSize = ReadCompactSize(stream)' src/script/witness.h 'witness stack is not parsed into an atomic compact destination'
require_fixed 'm_serializedElements.capacity() * sizeof(unsigned char)' src/script/witness.h 'compact witness memory is absent from transaction accounting'
require_fixed 'MAX_INITIAL_WITNESS_STACK' src/script/interpreter.cpp 'P2WSH can expand an unconditionally invalid compact witness count'
reject_fixed 'std::vector<std::vector<unsigned char> > stack;' src/script/script.h 'nested witness vector allocation amplification was reintroduced'
require_fixed 'MAX_BLOCK_TRANSACTION_COUNT = MAX_BLOCK_WEIGHT_RIP25_PHASE2 / MIN_TRANSACTION_WEIGHT' src/consensus/consensus.h 'block-family parser bound is not derived from the phase-2 consensus ceiling'
require_fixed 'Block transaction count exceeds structural limit' src/primitives/block.h 'full block count is not rejected before transaction allocation'
require_fixed 'BlockTransactions count exceeds structural limit' src/blockencodings.h 'BLOCKTXN count is not rejected before transaction allocation'
require_fixed 'Compact block transaction count exceeds structural limit' src/blockencodings.h 'compact block combined count is not bounded before prefilled allocation'
require_fixed 'std::vector<uint32_t> indexes;' src/blockencodings.h 'compact-block request indexes still truncate above 65,535'
require_fixed 'BlockTransactionsRequest count exceeds structural limit' src/blockencodings.h 'compact-block request count is not bounded before allocation'
require_fixed 'BlockTransactionsRequest indexes are not strictly increasing within structural limit' src/blockencodings.h 'compact-block request serialization can underflow a differential index'
require_fixed 'uint32_t index{0};' src/blockencodings.h 'prefilled compact-block indexes still truncate above 65,535'
require_fixed 'std::unordered_map<uint64_t, uint32_t> shorttxids' src/blockencodings.cpp 'compact-block short-ID positions still truncate above 65,535'
require_fixed 'size_t index_offset = 0;' src/blockencodings.cpp 'compact-block prefilled offset can wrap at 65,536'
require_fixed 'i + index_offset < txn_available.size()' src/blockencodings.cpp 'compact-block short-ID placement has no explicit vector bound'
reject_fixed 'std::unordered_map<uint64_t, uint16_t> shorttxids' src/blockencodings.cpp '16-bit compact-block short-ID position map was reintroduced'
reject_fixed 'uint16_t index_offset = 0;' src/blockencodings.cpp '16-bit compact-block prefilled offset was reintroduced'
require_fixed 'vRecv >> resp.blockhash' src/net_processing.cpp 'BLOCKTXN request ownership is not preflighted before its transaction body'
require_fixed 'TryGetMissingTxCount' src/blockencodings.cpp 'consumed compact-block state is not checked without assertions'
require_min_count 'partialBlock.reset()' src/net_processing.cpp 2 'compact-block fallback leaves partial state reachable'
orphan_function="$(sed -n '/^bool AddOrphanTx(/,/^}/p' src/net_processing.cpp)"
require_text "$orphan_function" 'GetSerializeSize(*tx, SER_NETWORK, PROTOCOL_VERSION)' 'orphan admission is not bounded by retained raw bytes'
require_text "$orphan_function" 'MAX_STANDARD_TX_WEIGHT / WITNESS_SCALE_FACTOR' 'orphan raw-byte limit is not the documented 100-kB bound'
if grep -Fq 'GetTransactionWeight(*tx)' <<<"$orphan_function"; then
  fail 'orphan admission grants an attacker-controlled structural PQ discount'
fi
reorg_function="$(sed -n '/^void CTxMemPool::removeForReorg(/,/^}/p' src/txmempool.cpp)"
require_text "$reorg_function" '!fPQHybridActive' 'mempool reorg cleanup is not gated by contextual PQ activation'
require_text "$reorg_function" 'HasPQWitnessV2Output(tx)' 'pre-activation reorg cleanup retains witness-v2 creators'
require_text "$reorg_function" 'SpendsPQWitnessV2Program(txin, prevScriptPubKey)' 'pre-activation reorg cleanup retains native or P2SH witness-v2 spends'
require_fixed 'IsPQHybridActiveLocked(chainActive.Tip(), GetParams().GetConsensus())' src/validation.cpp 'reorg cleanup does not derive PQ policy from the new active tip'
pq_spend_function="$(sed -n '/^bool SpendsPQWitnessV2Program(/,/^}/p' src/policy/policy.cpp)"
require_text "$pq_spend_function" 'txin.scriptSig != CScript() << redeemBytes' 'P2SH witness-v2 detection does not require the canonical single-push scriptSig'
if grep -Fq 'EvalScript' <<<"$pq_spend_function"; then
  fail 'mempool reorg cleanup executes attacker-controlled scriptSig while scanning'
fi

# Encrypted PQ wallet persistence: ciphertext path must return before plaintext.
wallet_pq_function="$(sed -n '/^bool CWallet::AddPQKeyPubKey(/,/^}/p' src/wallet/wallet.cpp)"
wallet_pq_with_db_function="$(sed -n '/^bool CWallet::AddPQKeyPubKeyWithDB(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$wallet_pq_function" 'AddPQKeyPubKeyWithDB' 'wallet PQ insertion bypasses its transactional helper'
require_text "$wallet_pq_with_db_function" 'CCryptoKeyStore::AddPQKeyPubKey' 'wallet PQ insertion bypasses the crypto keystore'
require_text "$wallet_pq_with_db_function" 'if (IsCrypted())' 'encrypted PQ wallet path lacks an early return'
require_text "$wallet_pq_with_db_function" 'WritePQKey' 'unencrypted PQ wallet persistence missing'
if ! grep -A1 -F 'if (IsCrypted())' <<<"$wallet_pq_with_db_function" | grep -Fq 'return true;'; then
  fail 'encrypted PQ wallet path can fall through instead of returning'
fi
encrypted_line="$(grep -nF 'if (IsCrypted())' <<<"$wallet_pq_with_db_function" | head -n1 | cut -d: -f1 || true)"
plaintext_line="$(grep -nF 'WritePQKey' <<<"$wallet_pq_with_db_function" | head -n1 | cut -d: -f1 || true)"
[[ -n "$encrypted_line" && -n "$plaintext_line" ]] || fail 'cannot locate wallet PQ persistence branches'
(( encrypted_line < plaintext_line )) || fail 'encrypted-wallet return must precede plaintext PQ persistence'
require_fixed 'wallet/test/pq_wallet_tests.cpp' src/Makefile.test.include 'PQ wallet persistence regressions are not wired into make check'
require_fixed 'encrypted_pq_keys_are_ciphertext_only_after_reload_and_backup' src/wallet/test/pq_wallet_tests.cpp 'encrypted PQ wallet reload/backup regression missing'
require_fixed 'pq_hd_derivation_kats_are_byte_exact' src/wallet/test/pq_wallet_tests.cpp 'deterministic PQ derivation KAT is missing'
require_fixed 'deterministic_pq_wallet_derivation_recovers_and_advances' src/wallet/test/pq_wallet_tests.cpp 'deterministic PQ wallet recovery regression is missing'
require_fixed 'deterministic_pq_coin_type_change_uses_own_branch' src/wallet/test/pq_wallet_tests.cpp 'PQ network derivation separation regression is missing'
require_fixed 'deterministic_pq_counter_and_key_commit_atomically' src/wallet/test/pq_wallet_tests.cpp 'PQ key and counter atomicity regression is missing'
require_fixed 'bip44_key_only_recovery_preserves_derivation_lineage' src/wallet/test/pq_wallet_tests.cpp 'key-only recovery does not prove PQ derivation state retention'
require_fixed 'std::string("pqkey")' src/wallet/test/pq_wallet_tests.cpp 'PQ wallet regression does not inspect plaintext DB records'
require_fixed 'std::string("cpqkey")' src/wallet/test/pq_wallet_tests.cpp 'PQ wallet regression does not inspect ciphertext DB records'
require_fixed 'if (!EraseIC(std::make_pair(std::string("pqkey")' src/wallet/walletdb.cpp 'encrypted PQ persistence ignores plaintext erase failure'
require_fixed 'HasPlaintextPQKeys' src/wallet/wallet.cpp 'encrypted backup does not scan for plaintext PQ records'
require_fixed 'if (!dbw->Rewrite())' src/wallet/wallet.cpp 'wallet encryption/backup does not propagate rewrite failure'
require_fixed '!mapKeys.empty() || !mapPQKeys.empty()' src/wallet/crypter.cpp 'crypted mode permits resident plaintext PQ keys'

# Wallet encryption must return immediately after any failure that follows
# live-keystore mutation. Assertions are diagnostics, never control flow.
encrypt_wallet_function="$(sed -n '/^bool CWallet::EncryptWallet(/,/^DBErrors CWallet::ReorderTransactions(/p' src/wallet/wallet.cpp)"
if grep -Fq 'assert(false)' <<<"$encrypt_wallet_function"; then
  fail 'EncryptWallet still relies on assert(false) after a recoverable failure'
fi
require_text "$encrypt_wallet_function" 'return failEncryptionAfterKeyMutation(true)' 'post-mutation wallet encryption failures do not return through centralized cleanup'
require_text "$encrypt_wallet_function" 'return failEncryptionAfterKeyMutation(false)' 'wallet transaction commit failure can fall through cleanup'
require_fixed 'pwalletdbEncryption = nullptr' src/wallet/wallet.cpp 'wallet encryption cleanup leaves a dangling database pointer'
require_fixed 'const bool wasCrypted = pwallet->IsCrypted()' src/wallet/rpcwallet.cpp 'RPC encryption failure does not snapshot the pre-call encryption state'
require_fixed '!wasCrypted && pwallet->IsCrypted()' src/wallet/rpcwallet.cpp 'RPC encryption failure can confuse an already encrypted wallet with newly mutated live state'
require_fixed 'Wallet encryption failed after the live key state changed' src/wallet/rpcwallet.cpp 'RPC encryption failure does not distinguish mutated live state'
require_fixed '!wasCrypted && !encryptedSuccessfully && wallet->IsCrypted()' src/qt/walletmodel.cpp 'Qt encryption failure does not distinguish a newly mutated live state'
require_text "$encrypt_wallet_function" '!pwalletdbEncryption->EraseBip39Words(false)' 'BIP39 words erase failure is ignored during encryption'
require_text "$encrypt_wallet_function" '!pwalletdbEncryption->EraseBip39Passphrase(false)' 'BIP39 passphrase erase failure is ignored during encryption'
require_text "$encrypt_wallet_function" '!pwalletdbEncryption->EraseBip39VchSeed(false)' 'BIP39 seed erase failure is ignored during encryption'
require_fixed 'HasPlaintextBip39(hasPlaintextBip39)' src/wallet/wallet.cpp 'encrypted backup does not scan for plaintext BIP39 records'

# A failed post-encryption compaction must stay quarantined across crashes,
# restarts, downgrade attempts, direct wallet calls, and RPC/Qt TOCTOU.
require_fixed 'WALLET_ENCRYPTION_REWRITE_MIN_VERSION = 0x7fffffff' src/wallet/walletdb.h 'wallet encryption recovery lacks a downgrade fence'
require_text "$encrypt_wallet_function" 'pwalletdbEncryption->TxnBegin(DB_TXN_SYNC)' 'wallet encryption marker transaction is not synchronous'
require_text "$encrypt_wallet_function" 'WriteEncryptionRewritePending(previousMinVersion)' 'wallet encryption does not persist its rewrite marker'
require_text "$encrypt_wallet_function" 'WALLET_ENCRYPTION_REWRITE_MIN_VERSION' 'wallet encryption does not persist its downgrade fence'
require_text "$encrypt_wallet_function" 'pwalletdbEncryption->TxnCommit(DB_TXN_SYNC)' 'wallet encryption marker commit is not synchronous'
require_text "$encrypt_wallet_function" 'NewKeyPoolInternal(true)' 'wallet encryption cannot perform its guarded post-commit keypool rotation'
marker_begin_line="$(grep -nF 'pwalletdbEncryption->TxnBegin(DB_TXN_SYNC)' <<<"$encrypt_wallet_function" | cut -d: -f1 || true)"
marker_write_line="$(grep -nF 'WriteEncryptionRewritePending(previousMinVersion)' <<<"$encrypt_wallet_function" | cut -d: -f1 || true)"
marker_commit_line="$(grep -nF 'pwalletdbEncryption->TxnCommit(DB_TXN_SYNC)' <<<"$encrypt_wallet_function" | cut -d: -f1 || true)"
rewrite_complete_line="$(grep -nF 'CompleteEncryptionRewrite()' <<<"$encrypt_wallet_function" | tail -n1 | cut -d: -f1 || true)"
[[ -n "$marker_begin_line" && -n "$marker_write_line" && -n "$marker_commit_line" && -n "$rewrite_complete_line" ]] || fail 'cannot locate wallet encryption recovery transaction boundaries'
(( marker_begin_line < marker_write_line && marker_write_line < marker_commit_line && marker_commit_line < rewrite_complete_line )) || fail 'wallet encryption rewrite state is not committed before compaction'

complete_rewrite_function="$(sed -n '/^bool CWallet::CompleteEncryptionRewrite(/,/^bool CWallet::EncryptWallet(/p' src/wallet/wallet.cpp)"
require_text "$complete_rewrite_function" 'if (!dbw->Rewrite())' 'wallet recovery clears its marker before compaction succeeds'
require_text "$complete_rewrite_function" 'walletdb.TxnBegin(DB_TXN_SYNC)' 'wallet recovery marker clearance is not synchronous'
require_text "$complete_rewrite_function" 'walletdb.WriteMinVersion(previousMinVersion)' 'wallet recovery does not restore the prior minversion'
require_text "$complete_rewrite_function" 'walletdb.EraseEncryptionRewritePending()' 'wallet recovery does not clear its marker'
require_text "$complete_rewrite_function" 'walletdb.TxnCommit(DB_TXN_SYNC)' 'wallet recovery marker-clear commit is not synchronous'
require_text "$complete_rewrite_function" 'fEncryptionRewritePending = false' 'wallet recovery clears no in-memory quarantine state'
compact_line="$(grep -nF 'if (!dbw->Rewrite())' <<<"$complete_rewrite_function" | cut -d: -f1 || true)"
clear_begin_line="$(grep -nF 'walletdb.TxnBegin(DB_TXN_SYNC)' <<<"$complete_rewrite_function" | cut -d: -f1 || true)"
clear_marker_line="$(grep -nF 'walletdb.EraseEncryptionRewritePending()' <<<"$complete_rewrite_function" | cut -d: -f1 || true)"
clear_commit_line="$(grep -nF 'walletdb.TxnCommit(DB_TXN_SYNC)' <<<"$complete_rewrite_function" | cut -d: -f1 || true)"
clear_memory_line="$(grep -nF 'fEncryptionRewritePending = false' <<<"$complete_rewrite_function" | cut -d: -f1 || true)"
[[ -n "$compact_line" && -n "$clear_begin_line" && -n "$clear_marker_line" && -n "$clear_commit_line" && -n "$clear_memory_line" ]] || fail 'cannot locate wallet encryption recovery clear boundaries'
(( compact_line < clear_begin_line && clear_begin_line < clear_marker_line && clear_marker_line < clear_commit_line && clear_commit_line < clear_memory_line )) || fail 'wallet encryption recovery clears quarantine before durable compaction'

new_keypool_function="$(sed -n '/^bool CWallet::NewKeyPoolInternal(/,/^}/p' src/wallet/wallet.cpp)"
topup_guard_function="$(sed -n '/^bool CWallet::TopUpKeyPoolInternal(/,/^}/p' src/wallet/wallet.cpp)"
reserve_key_function="$(sed -n '/^void CWallet::ReserveKeyFromKeyPool(/,/^}/p' src/wallet/wallet.cpp)"
keep_key_function="$(sed -n '/^void CWallet::KeepKey(/,/^}/p' src/wallet/wallet.cpp)"
return_key_function="$(sed -n '/^void CWallet::ReturnKey(/,/^}/p' src/wallet/wallet.cpp)"
get_pool_key_function="$(sed -n '/^bool CWallet::GetKeyFromPool(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$new_keypool_function" 'fEncryptionRewritePending && !allowEncryptionRewritePending' 'NewKeyPool can bypass encryption-rewrite quarantine'
require_text "$topup_guard_function" 'fEncryptionRewritePending && !allowEncryptionRewritePending' 'TopUpKeyPool can bypass encryption-rewrite quarantine'
require_text "$reserve_key_function" 'if (fEncryptionRewritePending)' 'ReserveKeyFromKeyPool can bypass encryption-rewrite quarantine'
require_text "$keep_key_function" 'if (fEncryptionRewritePending)' 'KeepKey can bypass encryption-rewrite quarantine'
require_text "$return_key_function" 'if (fEncryptionRewritePending)' 'ReturnKey can bypass encryption-rewrite quarantine'
require_text "$get_pool_key_function" 'if (fEncryptionRewritePending)' 'GetKeyFromPool can bypass encryption-rewrite quarantine'
require_fixed 'return NewKeyPoolInternal(false)' src/wallet/wallet.cpp 'public NewKeyPool enables the private quarantine bypass'
require_fixed 'return TopUpKeyPoolInternal(kpSize, false)' src/wallet/wallet.cpp 'public TopUpKeyPool enables the private quarantine bypass'
unlock_wallet_function="$(sed -n '/^bool CWallet::Unlock(const SecureString/,/^}/p' src/wallet/wallet.cpp)"
change_passphrase_function="$(sed -n '/^bool CWallet::ChangeWalletPassphrase(/,/^}/p' src/wallet/wallet.cpp)"
commit_wallet_function="$(sed -n '/^bool CWallet::CommitTransaction(/,/^}/p' src/wallet/wallet.cpp)"
backup_wallet_function="$(sed -n '/^bool CWallet::BackupWallet(/,/^}/p' src/wallet/wallet.cpp)"
ensure_wallet_function="$(sed -n '/^bool EnsureWalletIsAvailable(/,/^}/p' src/wallet/rpcwallet.cpp)"
require_text "$unlock_wallet_function" 'if (fEncryptionRewritePending)' 'wallet unlock can bypass encryption-rewrite quarantine'
require_text "$change_passphrase_function" 'if (fEncryptionRewritePending)' 'wallet passphrase change can bypass encryption-rewrite quarantine'
require_text "$commit_wallet_function" 'if (fEncryptionRewritePending)' 'wallet transaction commit can bypass encryption-rewrite quarantine'
require_text "$backup_wallet_function" 'if (fEncryptionRewritePending)' 'wallet backup can bypass in-memory encryption-rewrite quarantine'
require_text "$backup_wallet_function" 'ReadEncryptionRewritePending' 'wallet backup ignores on-disk encryption-rewrite state'
require_text "$ensure_wallet_function" 'IsEncryptionRewritePending()' 'wallet RPC entry points ignore encryption-rewrite quarantine'
read_rewrite_marker_function="$(sed -n '/^bool CWalletDB::ReadEncryptionRewritePending(/,/^}/p' src/wallet/walletdb.cpp)"
marker_absent_validation="$(sed -n '/if (markerExistsResult == DB_NOTFOUND)/,/std::pair<uint32_t, int> marker;/p' <<<"$read_rewrite_marker_function")"
marker_present_validation="$(sed -n '/std::pair<uint32_t, int> marker;/,/pending = true;/p' <<<"$read_rewrite_marker_function")"
require_text "$marker_absent_validation" 'storedMinVersion != WALLET_ENCRYPTION_REWRITE_MIN_VERSION' 'wallet rewrite fence without a marker is accepted'
require_text "$marker_present_validation" 'storedMinVersion != WALLET_ENCRYPTION_REWRITE_MIN_VERSION' 'wallet rewrite marker without its downgrade fence is accepted'
require_fixed 'DB_NEED_REWRITE_ENCRYPTION_NONCRITICAL' src/wallet/wallet.cpp 'wallet recovery loses noncritical load status'
require_fixed 'rewrite_failure_quarantines_until_restart_recovery' src/wallet/test/pq_wallet_tests.cpp 'wallet rewrite-failure quarantine regression is missing'
require_fixed 'wallet_database_sync_transaction_flushes_log' src/wallet/test/pq_wallet_tests.cpp 'wallet synchronous transaction regression is missing'
require_fixed 'encryption_rewrite_marker_states_fail_closed' src/wallet/test/pq_wallet_tests.cpp 'wallet rewrite marker fail-closed regression is missing'
require_fixed 'encryption_rewrite_preserves_noncritical_load_status' src/wallet/test/pq_wallet_tests.cpp 'wallet recovery noncritical-status regression is missing'
require_fixed 'wallet_encryption_rewrite.py' test/functional/test_runner.py 'wallet rewrite-failure RPC regression is not in the functional suite'
require_fixed 'Wallet encryption failed after the live key state changed' test/functional/wallet_encryption_rewrite.py 'wallet rewrite-failure RPC shutdown is untested'
require_fixed 'Wallet encryption recovery could not complete' test/functional/wallet_encryption_rewrite.py 'wallet rewrite-failure startup quarantine is untested'
verify_wallets_function="$(sed -n '/^bool VerifyWallets(/,/^bool OpenWallets(/p' src/wallet/init.cpp)"
require_text "$verify_wallets_function" 'const fs::path backup_path = GetDataDir() / backup_filename' 'explicit wallet salvage does not identify the retained original path'
require_text "$verify_wallets_function" 'may contain recoverable unencrypted private-key material' 'explicit wallet salvage does not warn that the retained original is sensitive'
salvage_recover_line="$(grep -nF 'CWalletDB::Recover(walletFile' <<<"$verify_wallets_function" | cut -d: -f1 || true)"
salvage_warning_line="$(grep -nF 'InitWarning(strprintf(' <<<"$verify_wallets_function" | head -n1 | cut -d: -f1 || true)"
[[ -n "$salvage_recover_line" && -n "$salvage_warning_line" ]] || fail 'cannot locate explicit wallet salvage warning boundaries'
(( salvage_recover_line < salvage_warning_line )) || fail 'explicit wallet salvage warning does not follow successful recovery'
require_fixed 'explicit_salvage_compacts_pending_wallet_and_retains_sensitive_original' src/wallet/test/pq_wallet_tests.cpp 'explicit salvage artifact regression is missing'
require_fixed 'extra_args=["-salvagewallet=1"], stderr=startup_stderr' test/functional/wallet_encryption_rewrite.py 'explicit salvage warning is not captured from startup'
require_fixed 'assert "may contain recoverable unencrypted private-key material" in stderr_text' test/functional/wallet_encryption_rewrite.py 'explicit salvage stderr warning is not asserted'
require_fixed 'may contain recoverable unencrypted private-key material' test/functional/wallet_encryption_rewrite.py 'explicit salvage warning text is not asserted'
require_fixed 'assert retained_backup in salvage_warning' test/functional/wallet_encryption_rewrite.py 'explicit salvage test does not bind the retained path to the warning'
require_fixed 'assert "may contain recoverable unencrypted private-key material" in salvage_warning' test/functional/wallet_encryption_rewrite.py 'explicit salvage test does not bind the plaintext risk to the warning'

# BIP39 rows are private-key material. Salvage/load must preserve a complete
# lineage, and key derivation must never substitute the deterministic empty seed.
to_seed_function="$(sed -n '/^bool CMnemonic::ToSeedWithPbkdf2(/,/^}/p' src/wallet/bip39.cpp)"
require_text "$to_seed_function" 'SecureVector derivedSeed(BIP39_SEED_SIZE)' 'BIP39 PBKDF2 does not derive into a secure temporary'
require_text "$to_seed_function" 'derivedSeed.data()) != 1' 'BIP39 PBKDF2 does not accept only the documented success return'
require_text "$to_seed_function" 'SecureVector().swap(seedRet)' 'BIP39 PBKDF2 failure does not cleanse prior output'
require_text "$to_seed_function" 'seedRet.swap(derivedSeed)' 'BIP39 PBKDF2 publishes output before full success'
require_fixed 'return ToSeedWithPbkdf2(mnemonic, passphrase, seedRet, PKCS5_PBKDF2_HMAC)' src/wallet/bip39.cpp 'production BIP39 derivation bypasses the checked PBKDF2 adapter'
set_mnemonic_function="$(sed -n '/^bool CHDChain::SetMnemonic(/,/^}/p' src/wallet/walletdb.cpp)"
require_text "$set_mnemonic_function" 'if (!CMnemonic::ToSeed' 'HD chain ignores BIP39 derivation failure'
generate_seed_function="$(sed -n '/^CPubKey CWallet::GenerateNewSeed(CWalletDB\* pwalletdb)/,/^}/p' src/wallet/wallet.cpp)"
require_text "$generate_seed_function" 'throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed")' 'wallet creation does not abort after BIP39 derivation failure'
require_text "$generate_seed_function" 'SetHDChain(newHdChain, false, pwalletdb)' 'BIP44 seed creation bypasses the caller transaction'
kdf_failure_line="$(grep -nF 'if (!newHdChain.SetMnemonic' <<<"$generate_seed_function" | cut -d: -f1 || true)"
seed_publish_line="$(grep -nF 'if (!AddVchSeed(vchSeed))' <<<"$generate_seed_function" | cut -d: -f1 || true)"
chain_persist_line="$(grep -nF 'SetHDChain(newHdChain' <<<"$generate_seed_function" | cut -d: -f1 || true)"
[[ -n "$kdf_failure_line" && -n "$seed_publish_line" && -n "$chain_persist_line" ]] || fail 'cannot locate BIP39 wallet-creation failure boundary'
(( kdf_failure_line < seed_publish_line && kdf_failure_line < chain_persist_line )) || fail 'BIP39 seed can be published or persisted before KDF failure is checked'
is_key_type_function="$(sed -n '/^bool CWalletDB::IsKeyType(/,/^}/p' src/wallet/walletdb.cpp)"
for bip39_type in bip39words bip39passphrase bip39vchseed cbip39words cbip39passphrase cbip39vchseed; do
  require_text "$is_key_type_function" "strType == \"$bip39_type\"" "BIP39 record type $bip39_type is not classified as key-critical"
done
require_text "$is_key_type_function" 'strType == "hdchain"' 'HD chain record is not classified as key-critical'
load_wallet_function="$(sed -n '/^DBErrors CWalletDB::LoadWallet(/,/^DBErrors CWalletDB::FindWalletTx(/p' src/wallet/walletdb.cpp)"
require_text "$load_wallet_function" 'incomplete or mixed BIP39 key material' 'BIP44 load lacks an end-of-scan completeness check'
recovery_filter_function="$(sed -n '/^bool CWalletDB::RecoverKeysOnlyFilter(/,/^}/p' src/wallet/walletdb.cpp)"
require_text "$recovery_filter_function" 'if (!IsKeyType(strType))' 'key-only recovery parses discarded records before classifying them'
derive_child_function="$(sed -n '/^void CWallet::DeriveNewChildKey(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$derive_child_function" 'AssertLockHeld(cs_wallet)' 'BIP44 derivation does not serialize seed access with wallet locking'
require_text "$derive_child_function" 'if (!GetBip39Seed(seed))' 'BIP44 derivation bypasses the locked, size-checked seed snapshot'
if grep -Fq 'g_vchSeed' <<<"$derive_child_function"; then
  fail 'BIP44 derivation reads mutable plaintext seed storage directly'
fi
topup_keypool_function="$(sed -n '/^bool CWallet::TopUpKeyPoolInternal(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$topup_keypool_function" 'IsBip44Enabled() && !HasValidBip39Seed()' 'keypool state can mutate before BIP39 seed validation'
first_run_function="$(sed -n '/^bool CWallet::IsFirstRun(/,/^}/p' src/wallet/wallet.cpp)"
for first_run_state in mapPQKeys mapCryptedPQKeys mapMasterKeys IsCrypted IsHDEnabled g_vchSeed vchCryptedBip39VchSeed; do
  require_text "$first_run_function" "$first_run_state" "first-run detection ignores existing $first_run_state wallet state"
done
wallet_load_function="$(sed -n '/^DBErrors CWallet::LoadWallet(bool& fFirstRunRet, bool notifyLoad)/,/^}/p' src/wallet/wallet.cpp)"
require_text "$wallet_load_function" 'fFirstRunRet = IsFirstRun()' 'wallet load duplicates an incomplete first-run predicate'
require_text "$wallet_load_function" $'if (notifyLoad)\n        uiInterface.LoadWallet(this);' 'wallet load notification is not controlled by the publication guard'
load_self_notifications="$(grep -Fc 'uiInterface.LoadWallet(this)' src/wallet/wallet.cpp || true)"
(( load_self_notifications == 1 )) || fail 'wallet load has an unguarded or duplicate observer publication'

# A factory-owned candidate remains private and under RAII ownership until all
# initialization, persistence, and rescan work has succeeded.
wallet_factory_function="$(sed -n '/^CWallet\* CWallet:: CreateWalletFromFile(/,/^std::atomic<bool> CWallet::fFlushScheduled/p' src/wallet/wallet.cpp)"
require_text "$wallet_factory_function" 'std::unique_ptr<CWallet> walletInstance' 'wallet factory does not retain RAII ownership of its unpublished candidate'
require_text "$wallet_factory_function" 'LoadWallet(fFirstRun, false)' 'wallet factory publishes the candidate during database load'
reject_fixed 'CWallet *walletInstance = new CWallet' src/wallet/wallet.cpp 'wallet factory still leaks raw ownership on failure'
require_text "$wallet_factory_function" 'RegisterValidationInterface(publishedWallet)' 'wallet factory never registers its completed candidate'
require_text "$wallet_factory_function" 'UnregisterValidationInterface(publishedWallet)' 'wallet factory leaves partial validation registration on failure'
require_text "$wallet_factory_function" 'uiInterface.LoadWallet(publishedWallet)' 'wallet factory never publishes its completed candidate'
require_text "$wallet_factory_function" 'Wallet load observer failed:' 'wallet factory lets observer exceptions destroy a retained wallet'
require_text "$wallet_factory_function" 'Wallet load observer failed with an unknown exception' 'wallet factory lets nonstandard observer exceptions destroy a retained wallet'
require_text "$wallet_factory_function" 'return walletInstance.release()' 'wallet factory releases ownership before successful publication'
require_text "$wallet_factory_function" 'CBlockIndex* failedBlock =' 'wallet factory ignores the initial rescan result'
require_text "$wallet_factory_function" 'if (failedBlock)' 'wallet factory publishes after a failed initial rescan'
require_text "$wallet_factory_function" 'bool updateBestChain = fFirstRun' 'wallet factory loses the successful first-run locator update'
require_text "$wallet_factory_function" 'updateBestChain = true' 'wallet factory loses the locator update after a successful rescan'
require_text "$wallet_factory_function" 'if (updateBestChain)' 'wallet factory rewrites the best-chain locator on every load'
factory_best_chain_count="$(grep -Fc 'walletInstance->SetBestChain(chainActive.GetLocator())' <<<"$wallet_factory_function" || true)"
(( factory_best_chain_count == 1 )) || fail 'wallet factory persists the active tip outside the successful rescan path'
factory_complete_line="$(grep -nF 'SetBroadcastTransactions' <<<"$wallet_factory_function" | tail -n1 | cut -d: -f1 || true)"
factory_rescan_line="$(grep -nF 'ScanForWalletTransactions(pindexRescan' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_rescan_failure_line="$(grep -nF 'if (failedBlock)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_best_chain_line="$(grep -nF 'walletInstance->SetBestChain(chainActive.GetLocator())' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_register_line="$(grep -nF 'RegisterValidationInterface(publishedWallet)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_unregister_line="$(grep -nF 'UnregisterValidationInterface(publishedWallet)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_notify_line="$(grep -nF 'uiInterface.LoadWallet(publishedWallet)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_release_line="$(grep -nF 'return walletInstance.release()' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
[[ -n "$factory_complete_line" && -n "$factory_rescan_line" && -n "$factory_rescan_failure_line" && -n "$factory_best_chain_line" && -n "$factory_register_line" && -n "$factory_unregister_line" && -n "$factory_notify_line" && -n "$factory_release_line" ]] || fail 'cannot locate wallet factory publication boundaries'
(( factory_rescan_line < factory_rescan_failure_line && factory_rescan_failure_line < factory_best_chain_line && factory_best_chain_line < factory_register_line && factory_complete_line < factory_register_line && factory_register_line < factory_unregister_line && factory_unregister_line < factory_notify_line && factory_notify_line < factory_release_line )) || fail 'wallet candidate is published before initialization completes'
require_fixed 'failed_wallet_creation_is_not_published' src/wallet/test/pq_wallet_tests.cpp 'failed wallet-creation publication regression is missing'
require_fixed 'throwing_load_observer_cannot_dangle_wallet' src/wallet/test/pq_wallet_tests.cpp 'throwing wallet-observer lifetime regression is missing'
require_fixed 'nonstandard_load_observer_cannot_dangle_wallet' src/wallet/test/pq_wallet_tests.cpp 'nonstandard wallet-observer lifetime regression is missing'
require_fixed 'failed-rescan-wallet.dat' src/wallet/test/wallet_tests.cpp 'failed initial-rescan publication regression is missing'
require_fixed 'successful-rescan-wallet.dat' src/wallet/test/wallet_tests.cpp 'successful initial-rescan locator regression is missing'
require_fixed 'database_environment_open_failure_renews_handle' src/wallet/test/wallet_tests.cpp 'Berkeley DB retry-handle regression is missing'
require_fixed 'database_mock_negative_open_failure_renews_handle' src/wallet/test/wallet_tests.cpp 'Berkeley DB negative-error regression is missing'
require_fixed 'ScopedWalletFactoryTestState' src/wallet/test/wallet_tests.cpp 'wallet factory test state is not restored after exceptions'
require_fixed 'gArgs.ClearArg("-rescan")' src/wallet/test/wallet_tests.cpp 'wallet factory test leaves a previously absent rescan argument set'
require_fixed 'gArgs.ClearArg("-keypool")' src/wallet/test/wallet_tests.cpp 'wallet factory test leaves a previously absent keypool argument set'

# New BIP44 wallets publish one atomic lineage: HD chain, complete BIP39
# material, derived keys, and keypool records share one synchronous transaction.
set_hd_chain_function="$(sed -n '/^bool CWallet::SetHDChain(const CHDChain& chain, bool memonly, CWalletDB\* pwalletdb)/,/^}/p' src/wallet/wallet.cpp)"
generate_seed_function="$(sed -n '/^CPubKey CWallet::GenerateNewSeed(CWalletDB\* pwalletdb)/,/^}/p' src/wallet/wallet.cpp)"
generate_key_function="$(sed -n '/^CPubKey CWallet::GenerateNewKey(/,/^}/p' src/wallet/wallet.cpp)"
derive_child_function="$(sed -n '/^void CWallet::DeriveNewChildKey(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$set_hd_chain_function" 'pwalletdb ? pwalletdb->WriteHDChain(chain)' 'HD chain persistence bypasses the caller transaction'
require_text "$set_hd_chain_function" 'if (!written)' 'HD chain persistence does not fail closed'
require_text "$generate_seed_function" 'SetHDChain(newHdChain, false, pwalletdb)' 'BIP44 seed generation discards the caller transaction'
require_text "$generate_key_function" 'SetMinVersion(FEATURE_COMPRPUBKEY, &walletdb)' 'key generation persists minversion outside the caller transaction'
require_text "$generate_key_function" 'DeriveNewChildKey(walletdb, metadata, secret' 'child derivation bypasses the caller transaction'
require_text "$generate_key_function" 'AddKeyPubKeyWithDB(walletdb, secret, pubkey)' 'key persistence bypasses the caller transaction'
require_text "$derive_child_function" 'walletdb.WriteHDChain(hdChain)' 'HD child counter bypasses the caller transaction'
require_text "$topup_keypool_function" 'if (!pwalletdb)' 'keypool generation never selects the caller database transaction'
require_text "$topup_keypool_function" 'GenerateNewKey(*pwalletdb, internal)' 'key generation bypasses the caller database transaction'
require_text "$topup_keypool_function" 'pwalletdb->WritePool(index' 'keypool records bypass the caller database transaction'
if grep -Fq 'CWalletDB walletdb(*dbw)' <<<"$topup_keypool_function"; then
  fail 'keypool generation always opens a second database handle'
fi
topup_walletdb_fallback="$(sed -n '/^[[:space:]]*if (!pwalletdb) {/,/^[[:space:]]*}/p' <<<"$topup_keypool_function")"
require_text "$topup_walletdb_fallback" 'pwalletdb = ownedWalletdb.get()' 'keypool fallback does not retain the selected database handle'
topup_walletdb_assignment_count="$(grep -Ec '^[[:space:]]*pwalletdb[[:space:]]*=' <<<"$topup_keypool_function" || true)"
(( topup_walletdb_assignment_count == 1 )) || fail 'keypool generation can rebind the caller database handle'
require_text "$wallet_factory_function" 'active(walletdb.TxnBegin(DB_TXN_SYNC))' 'BIP44 creation transaction is absent or asynchronous'
require_text "$wallet_factory_function" 'walletdb.TxnAbort()' 'BIP44 creation transaction lacks rollback cleanup'
require_text "$wallet_factory_function" 'walletdb.TxnCommit(DB_TXN_SYNC)' 'BIP44 creation commit is not synchronous'
require_text "$wallet_factory_function" 'GenerateNewSeed(&walletdb)' 'BIP44 seed lineage bypasses the creation transaction'
require_text "$wallet_factory_function" 'TopUpKeyPoolInternal(0, false, &walletdb)' 'initial keypool bypasses the creation transaction'
for record_write in WriteBip39Words WriteBip39VchSeed WriteBip39Passphrase; do
  require_text "$wallet_factory_function" "walletdb.$record_write" "BIP44 creation omits transactional $record_write"
  record_write_count="$(grep -Fc "$record_write" <<<"$wallet_factory_function" || true)"
  (( record_write_count == 1 )) || fail "BIP44 $record_write occurs outside the single creation boundary"
done
factory_mnemonic_line="$(grep -nF 'uiInterface.ShowMnemonic(CClientUIInterface::MODAL);' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_creation_begin_line="$(grep -nF 'active(walletdb.TxnBegin(DB_TXN_SYNC))' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_creation_construct_line="$(grep -nF '} transaction(walletdb);' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_seed_line="$(grep -nF 'GenerateNewSeed(&walletdb)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_words_line="$(grep -nF 'walletdb.WriteBip39Words' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_seed_record_line="$(grep -nF 'walletdb.WriteBip39VchSeed' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_passphrase_line="$(grep -nF 'walletdb.WriteBip39Passphrase' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_keypool_line="$(grep -nF 'TopUpKeyPoolInternal(0, false, &walletdb)' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_creation_commit_line="$(grep -nF 'if (!transaction.Commit())' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
factory_clear_line="$(grep -nF 'walletInstance->hdChain.ClearSensitiveData()' <<<"$wallet_factory_function" | cut -d: -f1 || true)"
[[ -n "$factory_mnemonic_line" && -n "$factory_creation_begin_line" && -n "$factory_creation_construct_line" && -n "$factory_seed_line" && -n "$factory_words_line" && -n "$factory_seed_record_line" && -n "$factory_passphrase_line" && -n "$factory_keypool_line" && -n "$factory_creation_commit_line" && -n "$factory_clear_line" ]] || fail 'cannot locate the BIP44 atomic-creation boundaries'
(( factory_mnemonic_line < factory_creation_begin_line && factory_creation_begin_line < factory_creation_construct_line && factory_creation_construct_line < factory_seed_line && factory_seed_line < factory_words_line && factory_words_line < factory_seed_record_line && factory_seed_record_line < factory_keypool_line && factory_keypool_line < factory_passphrase_line && factory_passphrase_line < factory_creation_commit_line && factory_creation_commit_line < factory_clear_line )) || fail 'BIP44 creation publishes lineage or keys outside its atomic boundary'
for unique_anchor in \
  'uiInterface.ShowMnemonic(CClientUIInterface::MODAL);' \
  'active(walletdb.TxnBegin(DB_TXN_SYNC))' \
  '} transaction(walletdb);' \
  'GenerateNewSeed(&walletdb)' \
  'TopUpKeyPoolInternal(0, false, &walletdb)' \
  'transaction.Commit()' \
  'walletInstance->hdChain.ClearSensitiveData()'; do
  unique_anchor_count="$(grep -Fc "$unique_anchor" <<<"$wallet_factory_function" || true)"
  (( unique_anchor_count == 1 )) || fail "BIP44 creation boundary anchor is absent or duplicated: $unique_anchor"
done
require_fixed 'bip44_creation_transaction_aborts_lineage_and_keypool' src/wallet/test/pq_wallet_tests.cpp 'BIP44 atomic-creation regression is missing'

# Locked encrypted wallets must not retain allocated plaintext BIP39 buffers.
for secure_field in vchWords vchPassphrase g_vchSeed; do
  require_fixed "SecureVector $secure_field;" src/keystore.h "plaintext $secure_field storage does not use the secure allocator"
done
lock_keystore_function="$(sed -n '/^bool CCryptoKeyStore::LockKeyStore(/,/^}/p' src/wallet/crypter.cpp)"
for released_field in vMasterKey vchWords vchPassphrase g_vchSeed; do
  require_text "$lock_keystore_function" "swap($released_field)" "wallet lock does not release plaintext $released_field storage"
done
wallet_lock_function="$(sed -n '/^bool CWallet::Lock(/,/^}/p' src/wallet/wallet.cpp)"
require_text "$wallet_lock_function" 'if (!LockKeyStore())' 'wallet lock bypasses centralized secret release'
require_text "$wallet_lock_function" 'hdChain.ClearSensitiveData()' 'wallet lock retains transient HD-chain BIP39 copies'
require_text "$wallet_lock_function" 'NotifyStatusChanged(this)' 'wallet lock does not publish the completed state transition'
lock_release_line="$(grep -nF 'if (!LockKeyStore())' <<<"$wallet_lock_function" | cut -d: -f1 || true)"
hd_release_line="$(grep -nF 'hdChain.ClearSensitiveData()' <<<"$wallet_lock_function" | cut -d: -f1 || true)"
lock_notify_line="$(grep -nF 'NotifyStatusChanged(this)' <<<"$wallet_lock_function" | cut -d: -f1 || true)"
[[ -n "$lock_release_line" && -n "$hd_release_line" && -n "$lock_notify_line" ]] || fail 'cannot locate wallet locked-state publication boundary'
(( lock_release_line < hd_release_line && hd_release_line < lock_notify_line )) || fail 'wallet publishes locked state before all plaintext BIP39 copies are released'
require_fixed 'SecureVector().swap(vchMnemonic)' src/wallet/walletdb.h 'HD-chain mnemonic storage is only resized, not released'
require_fixed 'SecureVector().swap(vchMnemonicPassphrase)' src/wallet/walletdb.h 'HD-chain passphrase storage is only resized, not released'
require_fixed 'SecureVector().swap(vchSeed)' src/wallet/walletdb.h 'HD-chain seed storage is only resized, not released'
require_fixed 'lock_cleanses_and_releases_plaintext_secret_storage' src/wallet/test/crypto_tests.cpp 'encrypted-wallet secret-release regression is missing'
require_fixed 'wallet_lock_releases_transient_hd_chain_secrets' src/wallet/test/pq_wallet_tests.cpp 'HD-chain secret-release regression is missing'
require_fixed 'malformed_cbc_input_releases_output' src/wallet/test/crypto_tests.cpp 'malformed wallet ciphertext cleanup regression is missing'

# Wallet salvage must build and durably close a replacement before atomically
# renaming either database. A reported failure must not publish a backup name.
recovery_function="$(sed -n '/^bool CDB::RecoverInternal(/,/^bool CDB::VerifyEnvironment(/p' src/wallet/db.cpp)"
require_text "$recovery_function" 'if (!bitdb.CloseDb(filename))' 'wallet recovery ignores source database close failure'
require_text "$recovery_function" 'bitdb.Salvage(filename, true, salvagedData)' 'wallet recovery renames the source before salvage'
require_text "$recovery_function" 'DB_CREATE | DB_EXCL | DB_AUTO_COMMIT' 'wallet recovery temporary database is not created exclusively and transactionally'
require_text "$recovery_function" 'putResult != 0' 'wallet recovery ignores a nonzero Berkeley DB row-write result'
require_text "$recovery_function" 'if (!activeTxn)' 'wallet recovery dereferences a null Berkeley DB transaction'
require_text "$recovery_function" 'activeTxn->commit(DB_TXN_SYNC)' 'wallet recovery does not request a synchronous durability boundary'
require_text "$recovery_function" 'bitdb.dbenv->dbrename(' 'wallet recovery lacks the transactional namespace installation'
require_text "$recovery_function" 'activeTxn, filename.c_str(), nullptr, backupFilename.c_str(), 0' 'wallet recovery source rename is not bound to the installation transaction'
require_text "$recovery_function" 'newFilename = backupFilename' 'wallet recovery does not publish the retained original after success'
reject_fixed 'dbrename(nullptr, filename.c_str()' src/wallet/db.cpp 'wallet recovery can still rename the source outside a transaction'
write_commit_line="$(grep -nF 'writeCommitResult = activeTxn->commit(DB_TXN_SYNC)' <<<"$recovery_function" | cut -d: -f1 || true)"
temp_close_line="$(grep -nF 'const int actualCloseResult = closeRecoveryDb()' <<<"$recovery_function" | cut -d: -f1 || true)"
source_rename_line="$(grep -nF 'const int backupRenameResult = bitdb.dbenv->dbrename(' <<<"$recovery_function" | cut -d: -f1 || true)"
install_commit_line="$(grep -nF 'installCommitResult = activeTxn->commit(DB_TXN_SYNC)' <<<"$recovery_function" | cut -d: -f1 || true)"
publish_backup_line="$(grep -nF 'newFilename = backupFilename' <<<"$recovery_function" | cut -d: -f1 || true)"
[[ -n "$write_commit_line" && -n "$temp_close_line" && -n "$source_rename_line" && -n "$install_commit_line" && -n "$publish_backup_line" ]] || fail 'cannot locate atomic wallet-recovery boundaries'
(( write_commit_line < temp_close_line && temp_close_line < source_rename_line && source_rename_line < install_commit_line && install_commit_line < publish_backup_line )) || fail 'wallet recovery publishes or renames before its durability boundaries'
require_fixed 'SalvageResult { FAILED, PARTIAL, COMPLETE }' src/wallet/db.h 'wallet recovery cannot distinguish partial salvage output'
require_fixed 'recovery_faults_preserve_original_database' src/wallet/test/pq_wallet_tests.cpp 'atomic wallet-recovery fault regression is missing'
require_fixed 'recovery_exclusive_temp_open_failure_preserves_source' src/wallet/test/pq_wallet_tests.cpp 'exclusive temporary-database regression is missing'
require_fixed 'partial_recovery_installs_atomically_and_preserves_backup' src/wallet/test/pq_wallet_tests.cpp 'partial wallet-recovery regression is missing'
require_fixed 'recovery_handles_zero_length_raw_rows' src/wallet/test/pq_wallet_tests.cpp 'zero-length Berkeley DB recovery regression is missing'

# PQ secret material must never cross a production API backed by the ordinary
# allocator. The behavioral test also proves byte-for-byte wallet compatibility.
require_fixed 'using KeyData = SecureVector' src/pqkey.h 'CPQKey secret storage lacks a secure-allocator type barrier'
reject_fixed 'SetKeyData(const std::vector<unsigned char>' src/pqkey.h 'CPQKey exposes an ordinary-heap secret import API'
reject_fixed 'SetKeyData(const std::vector<unsigned char>' src/pqkey.cpp 'CPQKey implements an ordinary-heap secret import API'
reject_fixed 'std::vector<unsigned char> keyData(key.GetKeyData()' src/keystore.h 'keystore copies a PQ secret into ordinary heap memory'
reject_fixed 'std::vector<unsigned char> keyData(vchSecret' src/wallet/crypter.cpp 'wallet decryption copies a PQ secret into ordinary heap memory'
reject_fixed 'std::vector<unsigned char> keyData(key.GetKeyData()' src/wallet/wallet.cpp 'wallet persistence copies a PQ secret into ordinary heap memory'
require_fixed 'CPQKey::KeyData pqKeyData' src/wallet/walletdb.cpp 'wallet loader deserializes PQ secrets into ordinary heap memory'
require_fixed 'const uint64_t pqKeySize = ReadCompactSize(ssValue)' src/wallet/walletdb.cpp 'wallet loader allocates a secure PQ buffer before validating its encoded size'
require_fixed 'pqKeySize != mldsa::SECRETKEY_BYTES' src/wallet/walletdb.cpp 'wallet loader does not enforce the fixed ML-DSA-44 secret-key size before allocation'
require_min_count 'Hash(pqPubKey.begin(), pqPubKey.end(),' src/wallet/walletdb.cpp 2 'wallet PQ hash compatibility is not computed without a concatenated secret buffer'
reject_fixed 'std::vector<unsigned char> pqKeyData' src/wallet/walletdb.cpp 'wallet DB uses ordinary heap memory for PQ secrets'
require_fixed 'pq_secret_material_uses_secure_allocator_and_legacy_encoding' src/test/pqkey_hardening_tests.cpp 'PQ secure-allocator/legacy-format regression is missing'
require_fixed 'oversized_plaintext_pq_record_is_rejected_before_secure_allocation' src/wallet/test/pq_wallet_tests.cpp 'oversized PQ wallet secret regression is missing'

# liboqs is consensus-critical and must be version-proven.
require_fixed 'liboqs' depends/packages/packages.mk 'liboqs missing from depends package graph'
require_fixed '$(package)_version=0.16.0' depends/packages/liboqs.mk 'pinned liboqs version must remain 0.16.0'
require_fixed '162d5b510518ee5f285f82fa1f16402a885176e818bf1b1a4c3c91c9a2f01eae' depends/packages/liboqs.mk 'pinned liboqs checksum changed'
require_fixed 'rip25_pkgconfig_provenance.patch' depends/packages/liboqs.mk 'depends does not stamp the reviewed liboqs source provenance'
require_fixed 'rip25_source_sha256=162d5b510518ee5f285f82fa1f16402a885176e818bf1b1a4c3c91c9a2f01eae' depends/patches/liboqs/rip25_pkgconfig_provenance.patch 'liboqs provenance patch does not stamp the reviewed source archive hash'
require_fixed '$(package)_config_opts_aarch64=-DCMAKE_SYSTEM_PROCESSOR=aarch64' depends/packages/liboqs.mk 'liboqs aarch64 cross-build does not identify the target processor to CMake'
require_fixed 'PKG_PROG_PKG_CONFIG' configure.ac 'configure does not require pkg-config'
require_fixed 'PKG_CHECK_MODULES([LIBOQS], [liboqs = 0.16.0]' configure.ac 'configure does not require exactly liboqs 0.16.0'
require_fixed '--variable=rip25_source_sha256 liboqs' configure.ac 'configure does not verify the reviewed liboqs source provenance'
require_fixed 'unsupported for release artifacts' configure.ac 'explicit system-liboqs override is not marked release-ineligible'
require_fixed 'refusing an unversioned system-library fallback' configure.ac 'configure does not document fail-closed liboqs behavior'
require_fixed '--without-liboqs is not supported' configure.ac 'configure permits disabling consensus-critical liboqs'
reject_fixed 'AC_CHECK_LIB([oqs]' configure.ac 'unversioned liboqs symbol fallback is forbidden'
reject_fixed 'AC_CHECK_HEADER([oqs/oqs.h]' configure.ac 'unversioned liboqs header fallback is forbidden'
reject_fixed 'LIBOQS_LIBS=-loqs' configure.ac 'manual unversioned liboqs linker fallback is forbidden'
require_fixed 'PKG_CONFIG_LIBDIR=$depends_prefix/share/pkgconfig:$depends_prefix/lib/pkgconfig' depends/config.site.in 'depends does not isolate target pkg-config metadata'
reject_fixed 'PKGCONFIG_LIBDIR' depends/config.site.in 'misspelled PKG_CONFIG_LIBDIR defeats cross-build isolation'
require_fixed '!defined(OQS_VERSION_MAJOR)' src/crypto/mldsa.cpp 'compile-time liboqs major-version guard missing'
require_fixed '!defined(OQS_VERSION_MINOR)' src/crypto/mldsa.cpp 'compile-time liboqs minor-version guard missing'
require_fixed '!defined(OQS_VERSION_PATCH)' src/crypto/mldsa.cpp 'compile-time liboqs patch-version guard missing'
require_fixed 'OQS_VERSION_MAJOR != 0 || OQS_VERSION_MINOR != 16 || OQS_VERSION_PATCH != 0' src/crypto/mldsa.cpp 'compile-time exact liboqs 0.16.0 guard missing'
require_fixed 'OQS_SIG_ml_dsa_44_length_public_key' src/crypto/mldsa.cpp 'ML-DSA-44 interface size guard missing'
require_fixed 'PQCP_MLDSA_NATIVE_MLDSA44_C_keypair_internal' src/crypto/mldsa.cpp 'deterministic keygen does not use the pinned mldsa-native backend'
reject_fixed 'OQS_randombytes_custom_algorithm' src/crypto/mldsa.cpp 'deterministic keygen still changes process-global RNG state'
reject_fixed 'OQS_randombytes_switch_algorithm' src/crypto/mldsa.cpp 'deterministic keygen still changes process-global RNG state'
require_fixed 'mldsa::SelfTest()' src/init.cpp 'node startup does not fail closed on ML-DSA backend self-test failure'
require_fixed 'mldsa_backend_compatibility_kat' src/test/pqkey_hardening_tests.cpp 'multi-seed backend compatibility KAT is missing'
require_fixed 'mldsa_acvp_keygen_kat' src/test/pqkey_hardening_tests.cpp 'independent FIPS 204 key-generation KAT is missing'
require_fixed 'OQS_SIG_sign_with_ctx_str' src/crypto/mldsa.cpp 'ML-DSA signing does not use the FIPS 204 context API'
require_fixed 'OQS_SIG_verify_with_ctx_str' src/crypto/mldsa.cpp 'ML-DSA verification does not use the FIPS 204 context API'
require_fixed 'RVN/ML-DSA-44/v1/0000006b444bc2f2ffe627be9d9e7e7a0730000870ef6eb6da46c8eae389df90' src/chainparams.cpp 'mainnet ML-DSA context changed'
require_fixed 'RVN/ML-DSA-44/v1/000000ecfc5e6324a079542221d00e10362bdc894d56500c414060eea8a3ad5a' src/chainparams.cpp 'testnet ML-DSA context changed'
require_fixed 'RVN/ML-DSA-44/v1/0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c' src/chainparams.cpp 'regtest ML-DSA context changed'
require_fixed 'pqSignatureContext.data(), pqSignatureContext.size()' src/script/interpreter.cpp 'witness-v2 verification does not use the selected network context'
require_fixed 'cacheHasher.Write(pqSignatureContext.data(), pqSignatureContext.size())' src/validation.cpp 'script execution cache is not separated by ML-DSA network context'
require_fixed 'txCreator->GetHashType() == SIGHASH_ALL' src/script/sign.cpp 'PQ producer does not reject unsupported sighash modes'
require_fixed 'witness_v2_signatures_are_bound_to_network_context' src/test/pqkey_hardening_tests.cpp 'cross-network replay regression is missing'
require_fixed 'pq_script_cache_separates_network_context' src/test/txvalidationcache_tests.cpp 'script-cache context separation regression is missing'
require_fixed 'failed_reinitialization_cleanses_prior_secret' src/test/pqkey_hardening_tests.cpp 'PQ key failure-path cleanse regression is missing'
if ! grep -A4 'libravenconsensus_la_LIBADD' src/Makefile.am | grep -Fq '$(LIBOQS_LIBS)'; then
  fail 'libravenconsensus must link LIBOQS_LIBS'
fi
if ! grep -A8 'qt_raven_qt_LDADD' src/Makefile.qt.include | grep -Fq '$(LIBOQS_LIBS)'; then
  fail 'raven-qt must link LIBOQS_LIBS'
fi

# Security regression tests must compile and execute through make check.
require_fixed 'test/bip39_tests.cpp' src/Makefile.test.include 'BIP39 vectors are not wired into make check'
require_fixed 'test/data/bip39_vectors.json' src/Makefile.test.include 'BIP39 vector data is not generated for make check'
require_fixed 'test/rip25_versionbits_tests.cpp' src/Makefile.test.include 'RIP-25 versionbits test is not wired into make check'
require_fixed 'test/kawpow_v48_hardening_tests.cpp' src/Makefile.test.include 'KAWPOW v4.8 hardening test is not wired into make check'
require_fixed 'witness_v2_active_rules_accept_valid_and_reject_invalid_mldsa' src/test/pqkey_hardening_tests.cpp 'active witness-v2 regression missing'
require_fixed '{std::string("PQ_HYBRID"),                             (unsigned int) SCRIPT_VERIFY_PQ_HYBRID}' src/test/transaction_tests.cpp 'transaction vector runner cannot enable PQ consensus rules'
require_min_count 'TransactionSignatureChecker(&tx, i, amount, txdata, mainnetPQContext)' src/test/transaction_tests.cpp 2 'PQ transaction vectors do not use the mainnet verification context in both runners'
require_fixed 'P2SH,WITNESS,PQ_HYBRID' src/test/data/tx_valid.json 'valid PQ transaction vector is missing'
require_fixed 'P2SH,WITNESS,PQ_HYBRID' src/test/data/tx_invalid.json 'invalid PQ transaction vector is missing'
require_fixed 'pq_witness_v2_tx_vector_mutations' src/test/transaction_tests.cpp 'PQ transaction vector mutation matrix is missing'
require_fixed 'ravenconsensus_verify_script_with_amount_and_network' src/script/ravenconsensus.h 'shared verifier lacks a network-aware PQ entry point'
require_fixed 'return set_error(err, ravenconsensus_ERR_INVALID_FLAGS)' src/script/ravenconsensus.cpp 'shared verifier reports unsupported flags as script success'
require_fixed '!(flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_P2SH)' src/script/ravenconsensus.cpp 'shared verifier permits WITNESS without P2SH'
require_fixed 'ravenconsensus_legacy_rejects_unverifiable_pq' src/test/script_tests.cpp 'legacy shared verifier fail-closed regression is missing'
require_fixed 'ravenconsensus_pq_network_context_and_activation' src/test/script_tests.cpp 'shared verifier cross-network regression is missing'
require_fixed 'ravenconsensus_witness_requires_p2sh_flag' src/test/script_tests.cpp 'shared verifier flag-implication regression is missing'
require_fixed 'FuzzPQWitness' src/test/test_raven_fuzzy.cpp 'real PQ witness verifier fuzz path is missing'
fuzz_link_block="$(sed -n '/^test_test_raven_fuzzy_LDADD =/,/^#$/p' src/Makefile.test.include)"
require_text "$fuzz_link_block" '$(LIBOQS_LIBS)' 'fuzz binary does not link the real ML-DSA verifier'
require_fixed 'SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH' src/test/pqkey_hardening_tests.cpp 'empty active witness-v2 rejection is untested'
require_fixed 'SCRIPT_ERR_PQ_SIGNATURE_VERIFY_FAILED' src/test/pqkey_hardening_tests.cpp 'malformed ML-DSA rejection is untested'
require_fixed 'verifyFlags |= SCRIPT_VERIFY_PQ_HYBRID' src/script/sign.cpp 'PQ transaction signing does not self-check under witness-v2 rules'
require_fixed 'secure_arg_is_single_consumption' src/test/getarg_tests.cpp 'secure argument consumption regression is missing'
require_fixed 'cancelled_mnemonic_prompt_cleans_pending_secrets' src/wallet/test/pq_wallet_tests.cpp 'cancelled mnemonic prompt cleanup regression is missing'
require_fixed 'invalid_mnemonic_error_never_discloses_phrase' src/wallet/test/pq_wallet_tests.cpp 'mnemonic diagnostic disclosure regression is missing'

# Ravencoin Core 4.8.0 security and recovery protections.
require_fixed 'nHeightHeaderCheckActivation = 4487776' src/chainparams.cpp '4.8 KAWPOW height activation missing'
require_fixed '4487775, uint256S("0x000000000002d64509e06e76ddbbe418c725291687ec62b41ecfc40386a091fd")' src/chainparams.cpp '4.8 checkpoint baseline changed'
require_fixed 'IsKAWPOWHeaderHeightValid(block, nHeight,' src/validation.cpp 'production validation bypasses the tested KAWPOW predicate'
require_fixed 'block.nHeight == static_cast<uint32_t>(actualHeight)' src/consensus/validation.h 'KAWPOW declared-height predicate changed'
require_fixed 'REJECT_INVALID, "bad-blk-height"' src/validation.cpp 'KAWPOW bad-height rejection missing'
require_fixed 'vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].bit = 11' src/chainparams.cpp 'transfer-overflow deployment must remain on bit 11'
require_fixed 'bad-txns-input-asset-totalInputs-toolarge' src/consensus/tx_verify.cpp 'asset input overflow protection missing'
require_fixed 'bad-txns-transfer-asset-totalOutputs-toolarge' src/consensus/tx_verify.cpp 'asset output overflow protection missing'
require_fixed 'fRetryWithChainStateRebuild' src/init.cpp 'chainstate-ahead automatic rebuild handling missing'
chainstate_recovery="$(sed -n '/^[[:space:]]*if (!LoadChainTip(chainparams)) {/,/^[[:space:]]*strLoadError = _("Error initializing block database");/p' src/init.cpp | sed 's/^[[:space:]]*//')"
[[ "$chainstate_recovery" == *$'LOCK(cs_main);\nconst auto tip = mapBlockIndex.find(pcoinsTip->GetBestBlock());\nfInvalidIndexedTip = tip != mapBlockIndex.end() &&\n(tip->second->nStatus & BLOCK_FAILED_MASK);\nfCoinsAheadOfIndex = tip == mapBlockIndex.end() || fInvalidIndexedTip;'* ]] || fail 'chainstate-ahead detection omits invalid indexed tips'
chainstate_ahead_guard="$(sed -n '/^if (fCoinsAheadOfIndex) {/,/^fRetryWithChainStateRebuild = true;/p' <<<"$chainstate_recovery")"
[[ "$chainstate_ahead_guard" == *$'if (fInvalidIndexedTip) {\nreturn InitError(_("The coins database references an invalid block.'* ]] || fail 'invalid indexed chainstate tip does not fail closed'
[[ "$chainstate_ahead_guard" == *$'if (fHavePruned) {\nreturn InitError(_("The coins database references an unknown block '* ]] || fail 'pruned unknown chainstate tip does not fail closed'
require_text "$chainstate_ahead_guard" 'fRetryWithChainStateRebuild = true;' 'chainstate-ahead automatic rebuild handling missing'
require_fixed 'passetsdb = new CAssetsDB(nBlockTreeDBCache, false, fReset || fReindexChainState)' src/init.cpp 'asset DB is not wiped on chainstate rebuild'
require_fixed 'prestricteddb = new CRestrictedDB(nBlockTreeDBCache, false, fReset || fReindexChainState)' src/init.cpp 'restricted-asset DB is not wiped on chainstate rebuild'
require_fixed "'feature_chainstate_ahead.py'" test/functional/test_runner.py 'chainstate-ahead restart regression is not registered'
require_fixed 'shutil.copytree(checkpoint_dir, index_dir)' test/functional/feature_chainstate_ahead.py 'chainstate-ahead regression does not restore the older block index'
require_fixed 'observer.checkaddresstag(tagged_address, qualifier_name), False' test/functional/feature_chainstate_ahead.py 'chainstate-ahead regression does not inspect restricted-state rollback'

# GLM-001 and CI supply-chain integrity: checkout commit is the tested tree.
final_gate=.github/workflows/rip25-v48-final-gate.yml
if grep -Eq '^    (if|continue-on-error):' "$final_gate"; then
  fail 'final gate contains a conditional or fail-open job'
fi
if grep -Eq '^[[:space:]]+(exit|return)([[:space:]]+0+([[:space:]]|$)|[[:space:]]*$)' "$final_gate"; then
  fail 'final gate contains an early successful shell exit'
fi
require_min_count 'actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5' "$final_gate" 2 'final gate checkout is not immutably pinned in every job'
require_fixed '  pull_request:' "$final_gate" 'final gate does not run on pull requests'
require_min_count 'persist-credentials: false' "$final_gate" 2 'final gate checkout credentials are not disabled'
require_min_count 'test "$(git rev-parse HEAD)" = "$GITHUB_SHA"' "$final_gate" 4 'final gate does not bind checked and tested source to the reported SHA'
require_min_count 'test -z "$(git status --porcelain)"' "$final_gate" 2 'final gate does not assert a pristine checkout'
require_min_count 'id: prebuild_integrity' "$final_gate" 2 'final gate does not recheck tracked source before compilation'
require_min_count 'run: ./contrib/devtools/check-rip25-v48-invariants.sh --structural-only' "$final_gate" 2 'final gate does not run structural lint in every job'
require_fixed 'run: ./contrib/devtools/check-rip25-v48-invariants.sh --run-tests' "$final_gate" 'final gate does not run the behavioral invariant suite'
require_fixed 'id: required_functional' "$final_gate" 'final gate does not require functional security tests'
require_fixed 'python3 contrib/devtools/test-required-functional-gate.py' "$final_gate" 'functional gate negative controls are not run'
require_fixed 'python3 test/functional/test_runner.py --require-tests' "$final_gate" 'functional gate permits absent or skipped security tests'
require_fixed 'feature_mempool_clear_reissue.py' "$final_gate" 'final gate omits inherited mempool clear regression'
require_fixed 'feature_mempool_reissue_restart.py' "$final_gate" 'final gate omits inherited reissue restart regression'
require_fixed 'wallet_encryption_rewrite.py wallet_asset_multiwallet.py feature_rewards_restart.py wallet_pq_recovery.py wallet_restricted_multiwallet.py wallet_messaging_multiwallet.py rpc_assettransfer.py feature_chainstate_ahead.py' "$final_gate" 'required wallet, rewards, PQ recovery, restricted multiwallet, messaging multiwallet, asset-scope, and chainstate-ahead functional tests are missing'
require_fixed 'wallet_asset_multiwallet.py' .github/workflows/build-raven.yml 'release security job omits the multiwallet asset regression'
require_fixed "'wallet_asset_multiwallet.py'" test/functional/test_runner.py 'multiwallet asset regression is not in the functional suite'
require_fixed 'id: posttest_integrity' "$final_gate" 'final gate does not recheck source after security tests'
final_security_job="$(sed -n '/^  security-tests:/,/^  build:/p' "$final_gate")"
require_text "$final_security_job" 'test/test_raven_fuzzy --pq-smoke' 'final security job does not run the PQ verifier fuzz smoke test'
mapfile -t final_security_sha_lines < <(grep -nFx '          test "$(git rev-parse HEAD)" = "$GITHUB_SHA"' <<<"$final_security_job" | cut -d: -f1)
(( ${#final_security_sha_lines[@]} == 2 )) || fail 'final security job must execute exact-SHA checks before and after tests'
final_make_check_line="$(grep -nFx '        run: make check' <<<"$final_security_job" | cut -d: -f1 || true)"
[[ "$final_make_check_line" =~ ^[0-9]+$ ]] || fail 'final security job does not execute make check'
(( final_security_sha_lines[0] < final_make_check_line && final_make_check_line < final_security_sha_lines[1] )) || fail 'final security job does not bracket make check with exact-SHA checks'
final_behavior_line="$(grep -nFx '        run: ./contrib/devtools/check-rip25-v48-invariants.sh --run-tests' <<<"$final_security_job" | cut -d: -f1 || true)"
final_mutation_line="$(grep -nFx '        run: python3 contrib/devtools/test-rip25-source-mutation-sensitivity.py --full' <<<"$final_security_job" | cut -d: -f1 || true)"
final_functional_line="$(grep -nFx '          python3 test/functional/test_runner.py --require-tests --jobs=2 \' <<<"$final_security_job" | cut -d: -f1 || true)"
[[ "$final_behavior_line" =~ ^[0-9]+$ && "$final_mutation_line" =~ ^[0-9]+$ && "$final_functional_line" =~ ^[0-9]+$ ]] || fail 'final security job must execute behavioral, mutation, and required functional tests'
(( final_make_check_line < final_behavior_line && final_behavior_line < final_mutation_line && final_mutation_line < final_functional_line && final_functional_line < final_security_sha_lines[1] )) || fail 'final security test steps are not in the required order'
if grep -Eq '^        (if|continue-on-error):' <<<"$final_security_job"; then
  fail 'final security job contains a conditional or fail-open step'
fi
final_required_step="$(sed -n '/^      - id: required_functional$/,/^      - id: posttest_integrity$/p' <<<"$final_security_job")"
[[ -n "$final_required_step" ]] || fail 'final required functional step is missing'
if grep -Fq '<<' <<<"$final_required_step"; then
  fail 'final security job must not hide required commands in a here document'
fi
require_fixed 'id: postbuild_integrity' "$final_gate" 'final gate does not recheck source after cross-builds'
final_build_matrix="$(sed -n '/^      matrix:/,/^    steps:/p' "$final_gate")"
require_text "$final_build_matrix" $'          - name: aarch64-disable-wallet\n            host: aarch64-linux-gnu\n            packages: g++-aarch64-linux-gnu\n            configure_flags: --without-gui --disable-wallet --disable-bench\n            run_tests: false' 'final gate lacks the aarch64 no-wallet cross-build'
require_text "$final_build_matrix" $'          - name: aarch64\n            host: aarch64-linux-gnu\n            packages: g++-aarch64-linux-gnu\n            configure_flags: --without-gui --disable-bench\n            run_tests: false' 'final gate lacks the aarch64 wallet cross-build'
reject_fixed 'statuses: write' "$final_gate" 'final gate has unnecessary status write permission'
reject_fixed 'pull_request_target' "$final_gate" 'final gate must not execute branch code via pull_request_target'
reject_fixed 'secrets.' "$final_gate" 'final gate must not expose repository secrets'
reject_fixed 'apply-rip25-v48-port' "$final_gate" 'final gate must not materialize source'
reject_fixed 'materializ' "$final_gate" 'final gate still describes source materialization'

if grep -RFn -- 'apply-rip25-v48-port' .github/workflows; then
  fail 'a workflow still invokes or references the RIP-25 materializer'
fi
if grep -RFin -- 'materializ' .github/workflows; then
  fail 'a workflow still contains remediation materialization machinery'
fi
if grep -ERn --include='*.yml' --include='*.yaml' 'uses:[[:space:]]+[^[:space:]#]+@(master|main|v[0-9]+)([[:space:]#]|$)' .github/workflows; then
  fail 'a workflow action still uses a mutable branch or version tag'
fi
reject_fixed 'fkirc/skip-duplicate-actions' .github/workflows/build-raven.yml 'redundant third-party duplicate-skip action remains'
reject_fixed 'actions/cache@' .github/workflows/build-raven.yml 'release builds must not restore unauthenticated dependency caches'
reject_fixed 'Cache Dependencies' .github/workflows/build-raven.yml 'release dependency cache step was reintroduced'
reject_fixed 'depends/built' .github/workflows/build-raven.yml 'release workflow restores compiled depends artifacts'
reject_fixed 'depends/work' .github/workflows/build-raven.yml 'release workflow restores unverified depends work state'
reject_fixed 'restore-keys:' .github/workflows/build-raven.yml 'release workflow permits fallback to an unrelated cache key'
require_fixed 'actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02' .github/workflows/build-raven.yml 'actions/upload-artifact pin changed'
require_fixed 'permissions:' .github/workflows/build-raven.yml 'build workflow lacks explicit permissions'
require_fixed '  contents: read' .github/workflows/build-raven.yml 'build workflow permissions are not read-only'
release_workflow=.github/workflows/build-raven.yml
if grep -Eq '^    (if|continue-on-error):' "$release_workflow"; then
  fail 'release workflow contains a conditional or fail-open job'
fi
if grep -Eq '^[[:space:]]+(exit|return)([[:space:]]+0+([[:space:]]|$)|[[:space:]]*$)' "$release_workflow"; then
  fail 'release workflow contains an early successful shell exit'
fi
require_fixed '      - fix/rip25-v48-glm-remediation' "$release_workflow" 'release workflow does not build remediation-branch pushes'
require_fixed '  pull_request:' "$release_workflow" 'release workflow does not build integration pull requests'
require_fixed 'runs-on: ubuntu-22.04' "$release_workflow" 'release workflow uses an unsupported runner'
release_security_job="$(sed -n '/^  security-tests:/,/^  build:/p' "$release_workflow")"
release_build_job="$(sed -n '/^  build:/,/^    strategy:/p' "$release_workflow")"
require_text "$release_security_job" 'test/test_raven_fuzzy --pq-smoke' 'release security job does not run the PQ verifier fuzz smoke test'
require_text "$release_security_job" 'make check' 'release artifact workflow does not run unit security tests before packaging'
require_text "$release_security_job" 'check-rip25-v48-invariants.sh --run-tests' 'release artifact workflow does not run behavioral security tests before packaging'
require_text "$release_security_job" 'test_runner.py --require-tests' 'release artifact workflow does not run required functional tests before packaging'
require_text "$release_security_job" 'feature_mempool_clear_reissue.py' 'release artifact workflow omits inherited mempool clear regression'
require_text "$release_security_job" 'feature_mempool_reissue_restart.py' 'release artifact workflow omits inherited reissue restart regression'
require_text "$release_security_job" 'feature_chainstate_ahead.py' 'release artifact workflow does not test chainstate-ahead recovery'
require_text "$release_security_job" 'test "$(git rev-parse HEAD)" = "$GITHUB_SHA"' 'release security job does not bind tests to the artifact SHA'
grep -Fxq '    needs: security-tests' <<<"$release_build_job" || fail 'release artifact matrix can package without passing security tests'
mapfile -t release_security_sha_lines < <(grep -nFx '          test "$(git rev-parse HEAD)" = "$GITHUB_SHA"' <<<"$release_security_job" | cut -d: -f1)
(( ${#release_security_sha_lines[@]} == 2 )) || fail 'release security job must execute exact-SHA checks before and after tests'
release_make_check_line="$(grep -nFx '          if ! make check; then' <<<"$release_security_job" | cut -d: -f1 || true)"
[[ "$release_make_check_line" =~ ^[0-9]+$ ]] || fail 'release security job does not execute make check'
(( release_security_sha_lines[0] < release_make_check_line && release_make_check_line < release_security_sha_lines[1] )) || fail 'release security job does not bracket make check with exact-SHA checks'
release_behavior_line="$(grep -nFx '        run: ./contrib/devtools/check-rip25-v48-invariants.sh --run-tests' <<<"$release_security_job" | cut -d: -f1 || true)"
release_mutation_line="$(grep -nFx '        run: python3 contrib/devtools/test-rip25-source-mutation-sensitivity.py --full' <<<"$release_security_job" | cut -d: -f1 || true)"
release_functional_line="$(grep -nFx '          python3 test/functional/test_runner.py --require-tests --jobs=2 \' <<<"$release_security_job" | cut -d: -f1 || true)"
[[ "$release_behavior_line" =~ ^[0-9]+$ && "$release_mutation_line" =~ ^[0-9]+$ && "$release_functional_line" =~ ^[0-9]+$ ]] || fail 'release security job must execute behavioral, mutation, and required functional tests'
(( release_make_check_line < release_behavior_line && release_behavior_line < release_mutation_line && release_mutation_line < release_functional_line && release_functional_line < release_security_sha_lines[1] )) || fail 'release security test steps are not in the required order'
if grep -Eq '^        (if|continue-on-error):' <<<"$release_security_job"; then
  fail 'release security job contains a conditional or fail-open step'
fi
release_required_step="$(sed -n '/^      - name: Run required functional security tests$/,/^      - name: Verify tested source and commit$/p' <<<"$release_security_job")"
[[ -n "$release_required_step" ]] || fail 'release required functional step is missing'
if grep -Fq '<<' <<<"$release_required_step"; then
  fail 'release security job must not hide required commands in a here document'
fi
release_matrix="$(sed -n '/^      matrix:/,/^    steps:/p' "$release_workflow")"
release_axes="$(grep -E '^        [A-Za-z_][A-Za-z_0-9]*:' <<<"$release_matrix" || true)"
[[ "$release_axes" == '        include:' ]] || fail 'release workflow has unexpected matrix axes'
release_target_count="$(grep -Ec '^          - OS: ' <<<"$release_matrix" || true)"
(( release_target_count == 5 )) || fail 'release workflow must build exactly five supported targets'
for target_host in windows:x86_64-w64-mingw32 osx:x86_64-apple-darwin14 linux:x86_64-linux-gnu arm32v7:arm-linux-gnueabihf aarch64:aarch64-linux-gnu; do
  target="${target_host%%:*}"
  host="${target_host#*:}"
  require_text "$release_matrix" "$(printf '          - OS: %s\n            host: %s' "$target" "$host")" "release workflow lacks the $target/$host target"
done
require_min_count 'test "$(git rev-parse HEAD)" = "$GITHUB_SHA"' "$release_workflow" 2 'release workflow does not bind checkout and artifact source to the reported SHA'
require_fixed 'test -z "$(git status --porcelain)"' "$release_workflow" 'release workflow does not require a pristine checkout'
require_min_count 'bash -Eeuo pipefail' "$release_workflow" 4 'release helper scripts are not invoked fail closed'
require_fixed 'if-no-files-found: error' "$release_workflow" 'release artifact upload permits missing output'
require_fixed 'native_bin="$GITHUB_WORKSPACE/depends/${{ matrix.host }}/native/bin"' "$release_workflow" 'release workflow uses the wrong target native tools'
require_fixed 'test -d "$native_bin"' "$release_workflow" 'release workflow accepts missing target native tools'
for artifact_suffix in win64.zip win64-setup-unsigned.exe win64-unsigned.tar.gz osx64.tar.gz osx-unsigned.dmg osx-unsigned.tar.gz x86_64-linux-gnu.tar.gz arm-linux-gnueabihf.tar.gz aarch64-linux-gnu.tar.gz; do
  require_fixed "\${distname}-${artifact_suffix}" "$release_workflow" "release workflow does not require the $artifact_suffix artifact"
done
require_fixed 'test -s "$artifact"' "$release_workflow" 'release workflow accepts empty target artifacts'
require_fixed 'git archive --format=tar --prefix="${distname}/" HEAD' "$release_workflow" 'release workflow lacks an exact-commit source archive'
require_fixed 'git get-tar-commit-id < "$source_tar"' "$release_workflow" 'release workflow does not verify embedded source-archive commit provenance'
require_fixed 'test -s "release/${source_archive}"' "$release_workflow" 'release workflow accepts an empty source archive'
require_fixed 'gzip -t "release/${source_archive}"' "$release_workflow" 'release workflow does not verify compressed source-archive integrity'
require_fixed 'sha256sum -- "${artifacts[@]}" "$source_archive" PROVENANCE.txt > SHA256SUMS' "$release_workflow" 'release workflow does not hash target, source, and provenance artifacts'
require_fixed 'test -s SHA256SUMS' "$release_workflow" 'release workflow accepts an empty checksum manifest'
require_fixed 'sha256sum --check SHA256SUMS' "$release_workflow" 'release workflow does not verify artifact hashes'
require_fixed 'test -s PROVENANCE.txt' "$release_workflow" 'release workflow accepts empty provenance'
require_fixed 'grep -Fx "commit=$GITHUB_SHA" PROVENANCE.txt' "$release_workflow" 'release workflow does not bind provenance to the checkout SHA'
require_fixed 'echo "target=${{ matrix.OS }}"' "$release_workflow" 'release provenance omits the target'
require_fixed 'echo "host=${{ matrix.host }}"' "$release_workflow" 'release provenance omits the cross-build host'
require_fixed 'name: raven-unsigned-${{ matrix.OS }}-${{ github.sha }}' "$release_workflow" 'release workflow labels unsigned artifacts as releases'
require_fixed 'status=unsigned CI build; Windows and macOS outputs are not signed final releases' "$release_workflow" 'release provenance claims unsigned outputs are signed releases'
release_package_step="$(sed -n '/^    - name: Verify Packaged Source and Artifacts$/,/^    - name: Upload Artifacts to Job$/p' "$release_workflow")"
[[ -n "$release_package_step" ]] || fail 'release package attestation step is missing'
grep -Fxq '      run: |' <<<"$release_package_step" || fail 'release package attestation is not an executable shell block'
if grep -Eq '^      (if|continue-on-error):' <<<"$release_package_step" ||
    grep -Fq '<<' <<<"$release_package_step" ||
    grep -Eq '^[[:space:]]+(exit|return)([[:space:]]|$)' <<<"$release_package_step"; then
  fail 'release package attestation can be skipped or hidden'
fi
for command in \
  '        git archive --format=tar --prefix="${distname}/" HEAD > "$source_tar"' \
  '        test "$(git get-tar-commit-id < "$source_tar")" = "$GITHUB_SHA"' \
  '          test -s "$artifact"' \
  '        grep -Fx "commit=$GITHUB_SHA" PROVENANCE.txt' \
  '        sha256sum -- "${artifacts[@]}" "$source_archive" PROVENANCE.txt > SHA256SUMS' \
  '        sha256sum --check SHA256SUMS'; do
  grep -Fxq -- "$command" <<<"$release_package_step" ||
    fail "release package attestation does not execute: $command"
done
release_upload_step="$(sed -n '/^    - name: Upload Artifacts to Job$/,/^        retention-days: /p' "$release_workflow")"
[[ -n "$release_upload_step" ]] || fail 'release artifact upload step is missing'
if grep -Eq '^      (if|continue-on-error):' <<<"$release_upload_step"; then
  fail 'release artifact upload can be skipped or fail open'
fi
release_artifact_job="$(sed -n '/^  verify-artifacts:$/,$p' "$release_workflow")"
[[ -n "$release_artifact_job" ]] || fail 'independent release artifact verification job is missing'
grep -Fxq '    needs: build' <<<"$release_artifact_job" ||
  fail 'independent release artifact verification must depend on every build target'
if grep -Eq '^    (if|continue-on-error):|^        (if|continue-on-error):' <<<"$release_artifact_job" ||
    grep -Fq '<<' <<<"$release_artifact_job" ||
    grep -Eq '^[[:space:]]+(exit|return)([[:space:]]|$)' <<<"$release_artifact_job"; then
  fail 'independent release artifact verification can be skipped or hidden'
fi
for command in \
  '        run: python3 -m unittest discover -s contrib/devtools -p test_build_raven_artifacts.py -v' \
  '        uses: actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093 # v4.3.0' \
  '          pattern: raven-unsigned-*' \
  '          merge-multiple: false' \
  '          python3 contrib/devtools/verify_build_raven_artifacts.py \' \
  '            "$RUNNER_TEMP/rip25-artifacts" "$GITHUB_SHA" \' \
  '            --repository "$GITHUB_WORKSPACE" --package-ref "$PACKAGE_REF" \' ; do
  grep -Fxq -- "$command" <<<"$release_artifact_job" ||
    fail "independent release artifact verification does not execute: $command"
done
require_fixed 'EXTRA_OPTS=()' .github/scripts/04-configure-build.sh 'release configure helper fails under nounset without wallet flags'
require_fixed '436df6dfc7073365d12f8ef6c1fdb060777c720602cc67c2dcf9a59d94290e38' .github/scripts/02-copy-build-dependencies.sh 'macOS SDK checksum pin changed'
require_fixed 'sha256sum --check' .github/scripts/02-copy-build-dependencies.sh 'macOS SDK is not verified before extraction'
reject_fixed 'pip3 install ds-store' .github/scripts/00-install-deps.sh 'release workflow uses an unpinned PyPI ds-store package'

temporary_paths=(
  .glm-remediation-trigger
  contrib/devtools/one-shot-glm-remediation.sh
  contrib/devtools/remediate-glm-rip25-v48.py
  .github/workflows/rip25-glm-remediation.yml
  .github/workflows/rip25-glm-remediate-once.yml
  .github/workflows/rip25-glm-remediate-pr.yml
)
for path in "${temporary_paths[@]}"; do
  [[ ! -e "$path" ]] || fail "temporary remediation infrastructure remains: $path"
done

if [[ "$mode" == '--structural-only' ]]; then
  echo 'RIP-25/v4.8 structural lint: OK (behavior not certified)'
  exit 0
fi

python3 contrib/devtools/test-rip25-invariant-gate-negative-controls.py

test_binary=src/test/test_raven
[[ -x "$test_binary" ]] || fail "behavioral test binary is missing or not executable: $test_binary"
# Qt and benchmark translation units are not linked into the headless
# behavioral test binary.
newer_source="$(find src \( -path src/qt -o -path src/bench \) -prune -o -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$test_binary" -print -quit)"
[[ -z "$newer_source" ]] || fail "behavioral test binary is stale relative to: $newer_source"

behavioral_tests=(
  sigopcount_tests/rip25_v2_sigops_activation_gated
  rip25_versionbits_tests
  asset_tx_tests/transfer_overflow_checks_follow_explicit_context
  asset_tx_tests/asset_destination_scope_test
  asset_tx_tests/pq_asset_envelope_is_not_witness_v2_test
  asset_tx_tests/pq_asset_creation_height_and_anchor_rule_test
  asset_tx_tests/pq_asset_history_destination_uses_origin_height_test
  coins_tests/txundo_large_roundtrip_test
  coins_tests/txundo_deserialization_limit_test
  rip25_miner_tests
  net_tests/incomplete_message_buffer_concurrent_global_limit
  net_tests/incomplete_message_buffer_releases_reservations
  net_tests/maximum_message_completes_with_global_buffer_limit
  net_tests/header_only_messages_are_globally_accounted
  transaction_tests/compact_witness_empty_element_amplification
  transaction_tests/compact_witness_block_empty_element_amplification
  transaction_tests/compact_witness_truncated_element_is_atomic_and_chunked
  transaction_tests/compact_witness_move_leaves_valid_source
  transaction_tests/compact_witness_preserves_compactsize_boundaries
  transaction_tests/tx_valid_test
  transaction_tests/tx_invalid_test
  transaction_tests/pq_witness_v2_tx_vector_mutations
  script_tests/ravenconsensus_legacy_rejects_unverifiable_pq
  script_tests/ravenconsensus_pq_network_context_and_activation
  script_tests/ravenconsensus_witness_requires_p2sh_flag
  amount_tests/Fee_Arithmetic_Boundaries_Test
  amount_tests/Fee_Rate_Addition_Boundaries_Test
  blockencodings_tests/block_family_counts_reject_before_element_read
  blockencodings_tests/block_family_transaction_count_boundary_roundtrips
  blockencodings_tests/block_family_count_bounds_are_atomic_and_apply_on_write
  blockencodings_tests/consumed_partial_block_fails_closed_after_fallback
  blockencodings_tests/compact_request_wide_index_wire_and_bounds
  blockencodings_tests/compact_prefilled_wide_indexes_and_positions
  blockencodings_tests/compact_prefilled_offset_wrap_completes
  DoS_tests/unexpected_blocktxn_is_rejected_before_body_parse
  DoS_tests/process_messages_releases_receive_queue_ownership
  DoS_tests/orphan_pq_shape_uses_raw_size_limit
  rpc_tests/rip25_gbt_reports_contextual_resource_limits
  mempool_tests/rip25_reorg_purges_preactivation_policy_transactions
  pqkey_hardening_tests
  tx_validationcache_tests/pq_script_cache_separates_network_context
  kawpow_v48_hardening_tests
  bip39_tests
  wallet_crypto/lock_cleanses_and_releases_plaintext_secret_storage
  wallet_crypto/corrupt_bip39_unlock_is_atomic
  wallet_crypto/corrupt_classical_key_cannot_publish_bip39_plaintext
  wallet_crypto/bip39_cbc_mutation_in_every_block_rejects
  wallet_crypto/malformed_cbc_input_releases_output
  wallet_tests/database_environment_open_failure_renews_handle
  wallet_tests/database_mock_negative_open_failure_renews_handle
  pq_wallet_tests
)

for test_filter in "${behavioral_tests[@]}"; do
  echo "RIP-25/v4.8 behavioral invariant: $test_filter"
  if [[ "$test_filter" == 'blockencodings_tests/compact_prefilled_offset_wrap_completes' ]] &&
      command -v timeout >/dev/null 2>&1; then
    timeout 30s "$test_binary" --run_test="$test_filter" --log_level=test_suite
  else
    "$test_binary" --run_test="$test_filter" --log_level=test_suite
  fi
done

fuzz_binary=src/test/test_raven_fuzzy
[[ -x "$fuzz_binary" ]] || fail "PQ fuzz smoke binary is missing or not executable: $fuzz_binary"
for fuzz_source in src/test/test_raven_fuzzy.cpp src/crypto/mldsa.cpp src/script/interpreter.cpp src/Makefile.test.include; do
  [[ ! "$fuzz_source" -nt "$fuzz_binary" ]] || fail "PQ fuzz smoke binary is stale relative to: $fuzz_source"
done
echo 'RIP-25/v4.8 behavioral invariant: real PQ verifier fuzz smoke'
fuzz_smoke_output="$("$fuzz_binary" --pq-smoke)" || fail 'real PQ verifier fuzz smoke failed'
[[ "$fuzz_smoke_output" == 'PQ witness fuzz smoke: 1 valid, 7 invalid, 256 mutations; 1 serialized valid, 7 serialized invalid, 256 wire mutations' ]] || fail 'PQ fuzz smoke did not execute the verifier-specific cases'
echo "$fuzz_smoke_output"
"$fuzz_binary" --pq-seed-tx | "$fuzz_binary"

python3 contrib/devtools/test-required-functional-gate.py
PYTHONPATH=test/functional python3 -m unittest test_framework.test_cache_peers
python3 test/functional/test_runner.py --require-tests --jobs=2 \
  wallet_encryption_rewrite.py feature_rewards_restart.py wallet_pq_recovery.py wallet_restricted_multiwallet.py wallet_messaging_multiwallet.py rpc_assettransfer.py feature_chainstate_ahead.py feature_pq_activation_reorg_sigops.py feature_pq_asset_anchor.py feature_pq_asset_authority_selection.py feature_pq_asset_source_rpc.py feature_pq_asset_wallet_ownership.py feature_mempool_clear_reissue.py feature_mempool_reissue_restart.py

echo 'RIP-25/v4.8 structural + behavioral invariants: OK'
