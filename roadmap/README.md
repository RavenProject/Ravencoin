# Ravencoin Roadmap

### Phase 1 - (Complete)

Ravencoin (RVN) is a Proof of Work coin built on the Bitcoin UTXO model. As with other Bitcoin derivatives, RVN coins are distributed to persons augmenting the Raven network by mining Raven.
*  x1000 coin distribution (21 Billion Total)
*  10x faster blocks (1 per minute)
*  In app CPU verification, with GPU specific PoW decentralised mining
*  Dark Gravity Wave difficulty adjustment (180 block average)
*  Addresses start with R... for regular addresses, or r... for multisig
*  Network Port: 8767
*  RPC Port: 8766

### Phase 2 - Assets (Complete)

#### ASIC Resistance

ASIC Resistance - A published commitment to continual attempts at ASIC resistance. If ASICs are created for x16r, then we will, at a specific block number, modify one of the algorithms to add some varients of Equihash or similar efforts to increase the resistance to ASIC miners for Raven. ASIC's have been developed for X16R (and X16RV2) and the community has forked to KAWPOW (a variant of ethash and progpow) to maximise the ASIC resistance by reducing the potential efficiency increase of ASICs by requiring the feature set and capabilities within over the counter consumer graphics cards. We are not anticipating future forks to change the algorithm as the current algorithm allows a fair distribution of RVN via PoW to the community.

#### Asset Support

Ravencoin will be a hard fork that extends Raven to include the ability to issue and transfer assets. The expected release of asset capabilities will be approximately seven months after the release of RVN. Raven will be extended to allow issuing, reissuing, and transfer of assets. Assets can be reissuable or limited to a set supply at the point of issuance. The cost to create assets will be 500 RVN to create any qty of an asset. Each asset name must be unique. Asset names will be limited to A-Z and 0-9, '_' and '.' and must be at least three characters long. The '.' and the '_' cannot be the first, or the last character, or be consecutive.  

Examples of valid assets:  
THE_GAME  
A.TOKEN  
123  

Examples of invalid assets:  
_TOKEN  
THEEND.  
A..B (consecutive punctuation)  
AB  
12  
.FIRST
apple

The RVN used to issue assets will be sent to a burn address, which will reduce the amount of RVN available. 

Asset transfers require the standard RVN transaction fees for transfer from one address to another.

#### Metadata (Complete)

Metadata about the token can be stored in IPFS.

#### Rewards (Complete)

Reward capabilities will be added to allow payment (in RVN) to all holders of an asset. Payments of RVN would be distributed to all asset holders pro rata. This is useful for paying dividends, dividing payments, or rewarding a group of token holders.

Example: A small software company issues an asset GAMECO that represents a share of the project. GAMECO tokens can be traded with others. Once the software company profits, those profits can be distributed to all holders of GAMECO by sending the profits (via RVN) to all holders of GAMECO.

#### Block Size

Raven may increase the blocksize from 2 MB to X MB to allow for more on-chain transactions.

### Phase 3 - Rewards (Complete)

Rewards allow payment in RVN to asset holders.

[More on rewards...](./rewards/README.md)

### Phase 4 - Unique Assets (Complete)

Once created, assets can be made unique for a cost of 5 RVN. Only non-divisible assets can be made unique. This moves an asset to a UTXO and associates a unique identifier with the txid. From this point the asset can be moved from one address to another and can be traced back to its origin. Only the issuer of the original asset can make an asset unique.  
The costs to make unique assets will be sent to a burn address.  

Some examples of unique assets:  
*  Imagine that an art dealer issues the asset named ART. The dealer can then make unique ART assets by attaching a name or a serialized number to each piece of art. These unique tokens can be transferred to the new owner along with the artwork as a proof of authenticity. The tokens ART#MonaLisa and ART#VenusDeMilo are not fungible and represent distinct pieces of art.
*  A software developer can issue the asset with the name of their software ABCGAME, and then assign each ABCGAME token a unique id or license key. The game tokens could be transferred as the license transfers. Each token ABCGAME#398222 and ABCGAME#398223 are unique tokens.
*  In game assets. A game ZYX_GAME could create unique limited edition in-game assets that are owned and used by the game player. Example: ZYX_GAME#Sword005 and ZYX_GAME#Purse
*  RVN based unique assets can be tied to real world assets. Create an asset named GOLDVAULT. Each gold coin or gold bar in a vault can be serialized and audited. Associated unique assets GOLDVAULT#444322 and GOLDVAULT#555994 can be created to represent the specific assets in the physical gold vault. The public nature of the chain allows for full transparency.

### Phase 5 - Messaging (Complete - in consensus)

Messaging to token holders by authorized senders will be layered on top of the Phase 4 unique assets. See [KAAAWWW Protocol](https://medium.com/@tronblack/ravencoin-kaaawww-2f72077aece) for additional information.

[More on messaging...](./messaging/README.md)  
[More on preventing message spam...](./messaging-antispam/README.md)  
[More on IPFS...](./ipfs/README.md)  

### Phase 6 - Voting (Available now using non-expiring UTXO based tokens)

Voting will be accomplished by creating and distributing parallel tokens to token holders. These tokens can be sent to RVN addresses to record a vote.

[More on voting...](./voting/README.md)

### Phase 7 - Compatibility Mode

Allows newly created assets to appear exactly like RVN, LTC, or Bitcoin for easy integration into exchanges, wallets, explorers, etc.
Speeds adoption into the larger crypto ecosystem.

[More on compatibility mode...](./compatibility-mode/README.md)


### Phase 8 - Mobile Wallet compatible Mnemonic Seed (Complete)

Switches to a default of generating a 128 bit seed from which the master key is generated.  This allows easy backup for anyone that doesn't import private keys.  Warnings added to back up wallet.dat when importing private keys.

[More on Mnemonic Seed...](./mnemonic-seed/README.md)

### Phase 9 - Restricted Assets (Complete)

* Tags
* Restricted assets with rules honoring tags


### Appendix A - RPC commands for assets

`issue (asset_name, qty, to_address, change_address, units, reissuable, has_ipfs, ipfs_hash)`  
Issue an asset with unique name. Unit as 1 for whole units, or 0.00000001 for satoshi-like units. Qty should be whole number. Reissuable is true/false for whether additional units can be issued by the
original issuer.  

`issueunique (root_name, asset_tags, ipfs_hash, to_address, change_address) `  
Creates a unique asset from a pool of assets with a specific name. Example: If the asset name is SOFTLICENSE, then this could make unique assets like SOFTLICENSE#38293 and SOFTLICENSE#48382 This would be called once per unique asset needed.  

`reissue (reissue asset_name, qty, to_address, change_address, reissuable, new_unit, new_ipfs )`
Issue more of a specific asset. This is only allowed by the original issuer of the asset and if the reissuable flag was set to true at the time of original issuance.

`transfer (asset_name, qty, to_address)`  
This sends assets from one asset holder to another.

`listassets (assets, verbose, count, start)`  
This lists assets that have already been created. 
  
`listmyassets ( asset_name, verbose, count, start )`
Lists your assets.

`listassetbalancesbyaddress (address)`
Lists asset balance by address.

`listaddressesbyasset (asset_name)` 
Lists addresses by asset.

`getassetdata (asset_name)`
Lists asset data of an asset.

