# Messaging

Messaging is described here in the [KAAAWWWW! protocol](https://medium.com/@tronblack/ravencoin-kaaawww-2f72077aece).

### Messaging Components
#### Protocol
This requires only the addition of an IPFS for each transaction.  

A message is "broadcast" if an owner token or channel token is sent in a transaction to the same address with the addition of an IPFS hash and an optional expiration date.  The message isn't really broadcast in the sense of being transmitted to nodes, but rather each node will independently detect the special transaction type and display the message.  Message display is subject to some heuristic anti-spam rules.

#### Channels
Channels are special Ravencoin unique asset tokens that are created by asset owners.  The channel tokens are similar to unique assets in that there is only one with a given name.  They can be uniquely identified by having a ~ (tilde) in the name.  They are limited to twelve characters, and can use uppercase, lowercase, and digits. Example: TRONCO~Alert

Sending these special channel tokens from one address to the same address will "broadcast" a message on the channel, which is named the same as the token.

Users can mute channels.  Some channels will be automatically muted by the anti-spam system.

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

The ZMQ message adds an additional queue for messaging 'pubmessage'

Only broadcast messages should be published via zmq.

Transaction messages can be obtained by watching zmq message queue 'pubrawtx' and decoding to get the ipfshash.

The ZMQ will return a json reponse.
```
{
  'asset': 'TRONCO',
  'block_height':483294,
  'txid': '8851dcf27271721f7eb5712eb49e092acfc4866e76a8e85fe6a33bb237501f9a',
  'vout': 2
  'ipfs_hash':'QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E',
  'expires': 1545343682
}
```

#### DevKit

The DevKit adds messaging support by including a ipfs_hash field for every transaction that includes an IPFS hash.  The ipfs_hash can be used with 

https://cloudflare-ipfs.com/ipfs/QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E

Replace the hash with your hash to look up the message on cloudflare.
If the IPFS daemon is installed, you can also use ipfs get [ipfs_hash]








