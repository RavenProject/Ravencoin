Shared Libraries
================

## ravenconsensus

The purpose of this library is to make the verification functionality that is critical to Raven's consensus available to other applications, e.g. to language bindings.

### API

The interface is defined in the C header `ravenconsensus.h` located in  `src/script/ravenconsensus.h`.

#### Version

`ravenconsensus_version` returns an `unsigned int` with the API version (currently `2`).

#### Script Validation

`ravenconsensus_verify_script` returns an `int` with the status of the verification. It will be `1` if the input script correctly spends the previous output `scriptPubKey`.

For witness spends, use `ravenconsensus_verify_script_with_amount`. Both legacy entry points deliberately return `0` for a native or P2SH-wrapped witness-v2 prevout. The amount-aware entry point reports `ravenconsensus_ERR_PQ_CONTEXT_REQUIRED`. The entry point without an amount reports `ravenconsensus_ERR_AMOUNT_REQUIRED` first when WITNESS is requested, or `ravenconsensus_ERR_PQ_CONTEXT_REQUIRED` when it is not. Neither can safely select a RIP-25 network signature context or provide a contextual activation state. They cannot be used to reproduce pre-activation future-witness acceptance for version 2. Other script types retain their prior behavior.

For RIP-25, use `ravenconsensus_verify_script_with_amount_and_network`. It takes the same transaction, prevout, amount, input index and flags as the amount-aware entry point, followed by a `ravenconsensus_network` and error pointer. The network must be `ravenconsensus_NETWORK_MAIN`, `ravenconsensus_NETWORK_TEST` or `ravenconsensus_NETWORK_REGTEST`. The library uses the corresponding canonical `RVN/ML-DSA-44/v1/` context with the lowercase 64-character genesis hash. Unknown network values return `0` with `ravenconsensus_ERR_INVALID_NETWORK`.

The exact context bytes are the ASCII prefix followed by these hashes, with no trailing NUL:

- Mainnet: `0000006b444bc2f2ffe627be9d9e7e7a0730000870ef6eb6da46c8eae389df90`
- Testnet: `000000ecfc5e6324a079542221d00e10362bdc894d56500c414060eea8a3ad5a`
- Regtest: `0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c`

The caller must derive script flags from the block's contextual activation state. Once RIP-25 is active, supply `ravenconsensus_SCRIPT_FLAGS_VERIFY_P2SH`, `ravenconsensus_SCRIPT_FLAGS_VERIFY_WITNESS` and `ravenconsensus_SCRIPT_FLAGS_VERIFY_PQ_HYBRID` (bit 16). Before activation, omit the PQ flag to retain future-witness semantics. This API does not determine BIP9 state or validate a whole transaction or block. WITNESS without P2SH and PQ without WITNESS are invalid flag combinations. Never omit the PQ flag when verifying an activated witness-v2 spend.

All entry points return exactly `1` for valid and `0` for invalid. Unknown flags return `0` with `ravenconsensus_ERR_INVALID_FLAGS`; an error enum value is never returned as the verification result.

##### Parameters
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `ravenconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

##### Script Flags
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_NONE`
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_P2SH` - Evaluate P2SH ([BIP16](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki)) subscripts
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_DERSIG` - Enforce strict DER ([BIP66](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki)) compliance
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY` - Enforce NULLDUMMY ([BIP147](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki))
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY` - Enable CHECKLOCKTIMEVERIFY ([BIP65](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki))
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY` - Enable CHECKSEQUENCEVERIFY ([BIP112](https://github.com/bitcoin/bips/blob/master/bip-0112.mediawiki))
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_WITNESS` - Enable WITNESS ([BIP141](https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki))
- `ravenconsensus_SCRIPT_FLAGS_VERIFY_PQ_HYBRID` - Enforce RIP-25 ML-DSA-44 witness-v2 validation after contextual activation

##### Errors
- `ravenconsensus_ERR_OK` - No errors with input parameters *(see the return value of `ravenconsensus_verify_script` for the verification status)*
- `ravenconsensus_ERR_TX_INDEX` - An invalid index for `txTo`
- `ravenconsensus_ERR_TX_SIZE_MISMATCH` - `txToLen` did not match with the size of `txTo`
- `ravenconsensus_ERR_TX_DESERIALIZE` - An error deserializing `txTo`
- `ravenconsensus_ERR_AMOUNT_REQUIRED` - Input amount is required if WITNESS is used
- `ravenconsensus_ERR_INVALID_FLAGS` - Unsupported or inconsistent verification flags
- `ravenconsensus_ERR_PQ_CONTEXT_REQUIRED` - Legacy entry point cannot validate a witness-v2 prevout
- `ravenconsensus_ERR_INVALID_NETWORK` - Unknown network selector for the RIP-25 entry point

### Example Implementations
- [NRaven](https://github.com/NicolasDorier/NRaven/blob/master/NRaven/Script.cs#L814) (.NET Bindings)
- [node-libravenconsensus](https://github.com/bitpay/node-libravenconsensus) (Node.js Bindings)
- [java-libravenconsensus](https://github.com/dexX7/java-libravenconsensus) (Java Bindings)
- [ravenconsensus-php](https://github.com/Bit-Wasp/ravenconsensus-php) (PHP Bindings)
