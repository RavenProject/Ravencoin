// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_VERSION_H
#define RAVEN_VERSION_H

/**
 * network protocol versioning
 */

static const int PROTOCOL_VERSION = 70028;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
static const int GETHEADERS_VERSION = 31800;

//! assetdata network request is allowed for this version
static const int ASSETDATA_VERSION = 70017;

//! getassetdata reutrn asstnotfound, and assetdata doesn't have blockhash in the data
static const int X16RV2_VERSION = 70025;

//! getassetdata reutrn asstnotfound, and assetdata doesn't have blockhash in the data
static const int KAWPOW_VERSION = 70027;

//! disconnect from peers older than this proto version
//!!! Anytime this value is changed please also update the "MY_VERSION" value to match in the
//!!! ./test/functional/test_framework/mininode.py file. Not doing so will cause verack to fail!
static const int MIN_PEER_PROTO_VERSION = X16RV2_VERSION;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

//! "filter*" commands are disabled without NODE_BLOOM after and including this version
static const int NO_BLOOM_VERSION = 70011;

//! "sendheaders" command and announcing blocks with headers starts with this version
static const int SENDHEADERS_VERSION = 70012;

//! "feefilter" tells peers to filter invs to you by fee starts with this version
static const int FEEFILTER_VERSION = 70013;

//! short-id-based block download starts with this version
static const int SHORT_IDS_BLOCKS_VERSION = 70014;

//! not banning for invalid compact blocks starts with this version
static const int INVALID_CB_NO_BAN_VERSION = 70015;

//! getassetdata reutrn asstnotfound, and assetdata doesn't have blockhash in the data
static const int ASSETDATA_VERSION_UPDATED = 70020;

//! In this version, 'rip5 (messaging and restricted assets)' was introduced
static const int MESSAGING_RESTRICTED_ASSETS_VERSION = 70026;


#endif // RAVEN_VERSION_H
