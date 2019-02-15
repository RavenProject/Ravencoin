# Mnemonic Seed

For all cryptocurrencies and crypto-assets, the greatest difficulty is securing your private keys.  

There have been evolutions over the last ten years and it is about as close to the final solution as it can get provided that crypto owner holds the keys.

A marvelous solution is to create a random seed from which all other keys can be generated.  For an overview of how this works read Seeds of Freedom:  
(https://medium.com/@tronblack/ravencoin-seeds-of-freedom-a3a3ff0fa1)

In an effort to bring ease-of-use and interoperability between the core wallet and the mobile wallet, the Ravencoin Core wallet will default to generating a 12-word mnemonic seed.  A mnemonic seed is a 128 bit random number that is run through HMAC-512 hashing algorithm to produce a master key.  The main advantage of starting with a 12-word seed is the ease of backing up the wallet.

The core wallet, by default, will generate a 12-word seed and calculated master key for  derivation.  When using the 12-word seed, the derivation path will use the BIP44 standard.

A command-line option (-noseed) can be passed when starting ravend and/or raven-qt for the first time to create a wallet using the original key system that existed in Bitcoin and Ravencoin on Jan 3, 2019.

When using the 12-word seed which will be the new default, the address derivation will change to be compatible with the BIP32/BIP39/BIP44 standards.  Since this is already implemented in the Ravencoin mobile wallet, the 12-words will be cross-compatible.  The derivation path is m/44'/175'/0'/0 for the first non-change address.

The advantage to this method of key generation is that a 12-word seed can be written down, or stamped into stainless steel and safely stored offline without the risk of a thumb-drive or hard drive backup failure.

Also, the same 12-word seed can be entered into a online or offline webwallet and transactions signed using the derived keys -- allowing a simple online web wallet for assets combined with the security of having the keys held by each owner, and not held centrally by a custodian.

### Technical
The seed will be stored in the wallet.dat as either 12 words, or 132 bits.  It will be encrypted when wallet.dat is encrypted and the master key derived on-the-fly.

For those the never import a private key into the wallet.dat, safely storing the 12-words is sufficient backup.

For the case when a private key is imported in the core client's wallet.dat, a warning should be presented that the 12-word mnemonic backup is now insufficient, and the wallet.dat should be backed up.  The reason is that the imported key is added to a list of keys and cannot be derived from the 12-word seed, so any funds sent to the address(es) for the imported key(s) would be lost in the case of wallet.dat being lost or corrupted.

When creating the wallet.dat for the first time, the default should be generating 128 bits of entropy, and storing either the words, or the 132 bits (which includes a 4 bit checkum).  If, however, the -noseed option is set via command-line or raven.conf, then the orginal method of creating a master key should be used, and the original path derivation should be used.

The path derivation should be dependent on the way the master key is generated: 
* Original Master Key:  BIP32 m/0'/0' (external) or m/0'/1' (internal)
* Seed-based master key (BIP39): BIP32/BIP44  m/44'/175'/0'/0

This change does not require a hard fork (upgrade), but it does require maintaining 100% compatibility with the old derivation path when the original master key is in the wallet.dat or it will appear to users that funds are lost.  Only new users, or those that start with a new wallet.dat will be switched over to the 12-word seed.

Optional: In order to back up the master key and chaincode, it requires 48 words.

### Compatibility
Other wallets like Jaxx and Coinomi use a 12-word seed.  For wallets that use BIP39/BIP32/BIP44 and the correct coinid of 175 for Ravencoin, the 12-words should be compatible with external wallets.

Because the amount of RVN in the asset UTXO is 0, and because the Ravencoin transaction will be invalid if the asset outputs don't match the asset inputs, this prevents external wallets from being able to lose assets even though the external wallets are completely unaware of assets.
