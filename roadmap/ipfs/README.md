# IPFS - Integration

IPFS (Interplanetary File System) is used by Ravencoin for issuance meta-data, messaging, and transaction meta-data. 

IPFS hashes are stored on-chain.
* Issuance transaction for issuance meta-data.
* Send channel token or ownership token to the same address to "broadcast" a message.
* Any other transaction can optionally include transaction meta-data. 

Ravencoin must interact with IPFS in order to show messages because the message content is stored on IPFS.  IPFS access should be on by default, but should be able to be turned off with a flag.  -noipfs  Seed nodes, back-end systems, etc. will not need IPFS.

### Phased approach to IPFS integration

The two phase approach allows Ravencoin to use IPFS natively, but also use existing IPFS proxies.

Add a class of IPFS functions for init, get, and add, where these functions will either get from ifps daemon, ipfs proxies, or NOOP if -noipfs is set.


#### Phase 1
* Use https://cloudflare-ipfs.com/ipfs/<IPFS hash> for download
	* Allow this to be configured in the Network tab under Preferences.
* Use https://globalupload.io/ for IPFS upload (meta-data, message send, transaction meta-data)


#### Phase 2
* A button that downloads and installs IPFS for the given platform (Mac, Windows, Linux)
* Ravencoin client detects when ipfs daemon is installed.
* Use ipfs get for download
* Use ipfs add for upload
* Option to act as a IPFS pinning node for Ravencoin assets.  -ipfsnode

### Mobile Wallet

The mobile wallet will use IPFS proxies.

### Web Wallet

The web wallet will use IPFS proxies.

### IPFS Node
If -ipfsnode is set, then all ipfs hashes for Ravencoin that are less than 16000 characters and are valid JSON are pinned.

If -ipfsnofilter is also set, then all ipfs hashes for Ravencoin are pinned.  This may take a lot of disk space.  This removes the 16000 character and json data type restriction.

