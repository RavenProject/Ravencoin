#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Utilities for manipulating blocks and transactions."""

from .mininode import (CBlock, uint256_from_str, ser_uint256, hash256, CTxInWitness, CTxOut, CTxIn,
                       CTransaction, COutPoint, ser_string, COIN)
from .script import CScript, OP_TRUE, OP_CHECKSIG, OP_RETURN

# Create a block (with regtest difficulty)
def create_block(hash_prev, coinbase, n_time=None):
    block = CBlock()
    if n_time is None:
        import time
        block.nTime = int(time.time()+600)
    else:
        block.nTime = n_time
    block.hashPrevBlock = hash_prev
    block.nBits = 0x207fffff # Will break after a difficulty adjustment...
    block.vtx.append(coinbase)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_x16r()
    return block

# Genesis block time (regtest)
REGTEST_GENISIS_BLOCK_TIME = 1537466400

# From BIP141
WITNESS_COMMITMENT_HEADER = b"\xaa\x21\xa9\xed"


def get_witness_script(witness_root, witness_nonce):
    witness_commitment = uint256_from_str(hash256(ser_uint256(witness_root)+ser_uint256(witness_nonce)))
    output_data = WITNESS_COMMITMENT_HEADER + ser_uint256(witness_commitment)
    return CScript([OP_RETURN, output_data])


# According to BIP141, blocks with witness rules active must commit to the
# hash of all in-block transactions including witness.
def add_witness_commitment(block, nonce=0):
    # First calculate the merkle root of the block's
    # transactions, with witnesses.
    witness_nonce = nonce
    witness_root = block.calc_witness_merkle_root()
    # witness_nonce should go to coinbase witness.
    block.vtx[0].wit.vtxinwit = [CTxInWitness()]
    block.vtx[0].wit.vtxinwit[0].scriptWitness.stack = [ser_uint256(witness_nonce)]

    # witness commitment is the last OP_RETURN output in coinbase
    block.vtx[0].vout.append(CTxOut(0, get_witness_script(witness_root, witness_nonce)))
    block.vtx[0].rehash()
    block.hashMerkleRoot = block.calc_merkle_root()
    block.rehash()


def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    abs_value = -value if neg else value
    while abs_value:
        r.append(int(abs_value & 0xff))
        abs_value >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r

# Create a coinbase transaction, assuming no miner fees.
# If pubkey is passed in, the coinbase output will be a P2PK output;
# otherwise an anyone-can-spend output.
def create_coinbase(height, pubkey = None):
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), 
                ser_string(serialize_script_num(height)), 0xffffffff))
    coin_base_output = CTxOut()
    coin_base_output.nValue = 5000 * COIN
    halvings = int(height/150) # regtest
    coin_base_output.nValue >>= halvings
    if pubkey is not None:
        coin_base_output.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        coin_base_output.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [ coin_base_output ]
    coinbase.calc_x16r()
    return coinbase

# Create a transaction.
# If the scriptPubKey is not specified, make it anyone-can-spend.
def create_transaction(prev_tx, n, sig, value, script_pub_key=CScript()):
    tx = CTransaction()
    assert(n < len(prev_tx.vout))
    tx.vin.append(CTxIn(COutPoint(prev_tx.sha256, n), sig, 0xffffffff))
    tx.vout.append(CTxOut(value, script_pub_key))
    tx.calc_x16r()
    return tx

def get_legacy_sigopcount_block(block, f_accurate=True):
    count = 0
    for tx in block.vtx:
        count += get_legacy_sigopcount_tx(tx, f_accurate)
    return count

def get_legacy_sigopcount_tx(tx, f_accurate=True):
    count = 0
    for i in tx.vout:
        count += i.scriptPubKey.get_sig_op_count(f_accurate)
    for j in tx.vin:
        # scriptSig might be of type bytes, so convert to CScript for the moment
        count += CScript(j.scriptSig).get_sig_op_count(f_accurate)
    return count
