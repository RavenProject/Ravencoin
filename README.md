Raven Core integration/staging tree
=====================================

https://ravencoin.org

---

## RIP-25: Post-Quantum Signatures (This Fork)

This fork implements [RIP-25](doc/RIP-0025-PQ-Signatures.md) ([GitHub Issue #1280](https://github.com/RavenProject/Ravencoin/issues/1280)), a proposal to add **quantum-resistant transaction signing** to Ravencoin using ML-DSA-44 (FIPS 204).

### What it does

New **witness v2** addresses use ML-DSA-44 (a NIST-standardized post-quantum signature algorithm) exclusively. Existing ECDSA addresses (witness v0) continue working unchanged. Users gradually migrate funds from ECDSA to ML-DSA-44 addresses, making the system quantum-resistant before quantum computers can break ECDSA.

- **Old addresses (witness v0):** ECDSA/secp256k1, unchanged
- **New addresses (witness v2):** ML-DSA-44 only, quantum-resistant
- **Migration:** Users send funds from old to new addresses at their own pace

### Key changes

| Area | Change |
|------|--------|
| **Consensus** | BIP9 soft-fork deployment (bit 11, 85% threshold), phased block weight increase (8 → 12 → 16 MWU) |
| **Script** | Witness version 2 validation: 2-element witness stack [mldsa_sig, mldsa_pk], SHA256(pk) == program |
| **Policy** | `TX_WITNESS_V2_PQ_KEYHASH` standard type, PQ witness discount (8x), PQ-aware dust threshold |
| **Addresses** | Bech32m encoding for witness v2 (HRP: `rvn` mainnet, `trvn` testnet, `rcrt` regtest) |
| **Network** | `NODE_PQ_HYBRID` service flag (bit 5), 16 MB protocol message limit |
| **Crypto** | `src/crypto/mldsa.h/cpp` — ML-DSA-44 via [liboqs](https://github.com/open-quantum-safe/liboqs) (FIPS 204 compliant) |
| **Keys** | `src/pqkey.h/cpp` — `CPQKey` / `CPQPubKey` for ML-DSA-44 key management |
| **Wallet** | `getnewpqaddress` RPC, PQ keystore integration, `IsMine` for witness v2 |
| **Signing** | ML-DSA-44 signing in `sign.cpp` via `TransactionSignatureCreator` |
| **Build** | liboqs added as dependency (`depends/packages/liboqs.mk`, `configure.ac --with-liboqs`) |
| **Tests** | `src/test/pqkey_tests.cpp` — unit tests for ML-DSA-44 keygen, sign/verify, witness programs |

### Branch

All work is on [`feature/rip25-pq-hybrid`](https://github.com/ALENOC/Ravencoin/tree/feature/rip25-pq-hybrid).

### Building with liboqs

```bash
# Install liboqs (Ubuntu/Debian)
sudo apt install cmake ninja-build
git clone https://github.com/open-quantum-safe/liboqs.git
cd liboqs && mkdir build && cd build
cmake -DOQS_MINIMAL_BUILD="SIG_ml_dsa_44" -DBUILD_SHARED_LIBS=ON ..
make -j$(nproc) && sudo make install
sudo ldconfig

# Build Ravencoin with PQ support
cd /path/to/Ravencoin
./autogen.sh
./configure --with-liboqs
make -j$(nproc)
```

Or using the depends system:
```bash
cd depends && make
cd .. && ./autogen.sh
./configure --prefix=$(pwd)/depends/x86_64-pc-linux-gnu
make -j$(nproc)
```

### Status

**Complete implementation** — All consensus rules, script validation, policy, network, wallet, signing, address encoding, and ML-DSA-44 cryptographic integration via liboqs are implemented. The build system detects liboqs automatically via pkg-config or `--with-liboqs`.

For the full specification see [`doc/RIP-0025-PQ-Signatures.md`](doc/RIP-0025-PQ-Signatures.md).

---

To see how to run Ravencoin, please read the respective files in [the doc folder](doc)


What is Ravencoin?
----------------

Ravencoin is an experimental digital currency that enables instant payments to
anyone, anywhere in the world. The Ravencoin platform also lets anyone create assets (tokens) on the Ravencoin network. 
Assets can be used for NFTs, STOs, Gift Cards, and fractional ownership of anything of value.
Ravencoin uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. 



License
-------

Raven Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/RavenProject/Ravencoin/tags) are created
regularly to indicate new official, stable release versions of Raven Core.

Active development is done in the `develop` branch. 

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Developer IRC is inactive please join us on discord in #development. https://discord.gg/fndp4NBGct

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

Testnet is up and running and available to use during development.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python, that are run automatically on the build server.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`


### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.


About Ravencoin
----------------
A digital peer to peer network for the facilitation of asset transfer.



In the fictional world of Westeros, ravens are used as messengers who carry statements of truth. Ravencoin is a use case specific blockchain designed to carry statements of truth about who owns what assets. 



Thank you to the Bitcoin developers. 

The Ravencoin project is launched based on the hard work and continuous effort of over 400 Bitcoin developers who made over 14,000 commits over the life to date of the Bitcoin project. We are eternally grateful to you for your efforts and diligence in making a secure network and for their support of free and open source software development.  The Ravencoin experiment is made on the foundation you built.


Abstract
----------------
Ravencoin aims to implement a blockchain which is optimized specifically for the use case of transferring assets such as securities from one holder to another. Based on the extensive development and testing of Bitcoin, Ravencoin is built on a fork of the Bitcoin code. Key changes include a faster block reward time and a change in the number, but not weighed distribution schedule, of coins. Ravencoin is free and open source and will be issued and mined transparently with no pre-mine, developer allocation or any other similar set aside. Ravencoin is intended to prioritize user control, privacy and censorship resistance and be jurisdiction agnostic while allowing simple optional additional features for users based on need.



A blockchain is a ledger showing the value of something and allowing it to be transferred to someone else. Of all the possible uses for blockchains, the reporting of who owns what is one of the core uses of the technology.  This is why the first and most successful use case for blockchain technology to date has been Bitcoin.

The success of the Ethereum ERC 20 token shows the demand for tokenized assets that use another blockchain.  Tokens offer many advantages to traditional shares or other participation mechanisms such as faster transfer, possibly increased user control and censorship resistance and reduction or elimination of the need for trusted third parties.

Bitcoin also has the capability of serving as the rails for tokens by using projects such as Omnilayer, RSK or Counterparty. However, neither Bitcoin nor Ethereum was specifically designed for facilitating ownership of other assets. 

Ravencoin is designed to be a use case specific blockchain designed to efficiently handle one specific function: the transfer of assets from one party to another.

Bitcoin is and always should be focused on its goals of being a better form of money. Bitcoin developers will unlikely prioritize improvements or features which are specifically beneficial to the facilitation of token transfers.  One goal of the Ravencoin project is to see if a use case specific blockchain and development effort can create code which can either improve existing structures like Bitcoin or provide advantages for specific use cases.

In the new global economy, borders and jurisdictions will be less relevant as more assets are tradable and trade across borders is increasingly frictionless. In an age where people can move significant amounts of wealth instantly using Bitcoin, global consumers will likely demand the same efficiency for their securities and similar asset holdings.

For such a global system to work it will need to be independent of regulatory jurisdictions.  This is not due to ideological belief but practicality: if the rails for blockchain asset transfer are not censorship resistance and jurisdiction agnostic, any given jurisdiction may be in conflict with another.  In legacy systems, wealth was generally confined in the jurisdiction of the holder and therefore easy to control based on the policies of that jurisdiction. Because of the global nature of blockchain technology any protocol level ability to control wealth would potentially place jurisdictions in conflict and will not be able to operate fairly.  

