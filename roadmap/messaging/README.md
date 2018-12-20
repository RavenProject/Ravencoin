# Messaging

Messaging is described here in the [KAAAWWWW! protocol](https://medium.com/@tronblack/ravencoin-kaaawww-2f72077aece).

### Messaging Components
#### Protocol
This requires only the addition of an IPFS for each transaction.  

A message is "broadcast" if an owner token or channel token is sent in a transaction to the same address with the addition of an IPFS hash and an optional expiration date.  The message isn't really broadcast in the sense of being transmitted to nodes, but rather each node will independently detect the special transaction type and display the message.  Message display is subject to some heuristic anti-spam rules.

#### Channels
Channels are special tokens that are created by asset owners.  The channel tokens are similar to unique assets in that there is only one with a given name.  They can be uniquely identified by having a ~ (tilde) in the name.

Sending these tokens to the same address will "broadcast" a message on the channel, which is named the same as the token.

#### GUI - Desktop

Create a broadcast message with optional expiration date.
* Phase 1 - Enter IPFS hash of message
* Phase 2 - Enter message, submit via POST to publish IPFS hash, submit message transaction by sending owner or channel token to same address.

#### GUI - Mobile

Create a broadcast message with optional expiration date.
* Phase 1 - Enter IPFS hash of message
* Phase 2 - Enter message, submit via POST to publish IPFS hash, submit message transaction by sending owner or channel token to same address.

#### RPC

These rpc calls are added in support of messaging:

issue_channel TOKEN CHANNEL_NAME
send_message TOKEN IPFS_HASH

You must hold the owner token for TOKEN.


#### Message Queue

The ZMQ message adds an additional queue for messaging.

The ZMQ will return the hash of the message.

#### DevKit

The DevKit adds messaging support by including a ipfs_hash field for every transaction that includes an IPFS hash.  The ipfs_hash can be used with 

https://cloudflare-ipfs.com/ipfs/QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E

Replace the hash with your hash to look up the message on cloudflare.
If the IPFS daemon is installed, you can also use ipfs get [ipfs_hash]








