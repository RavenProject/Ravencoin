Release Process
====================

Before every release candidate:

* Update translations see [translation_process.md](https://github.com/RavenProject/Ravencoin/blob/master/doc/translation_process.md#synchronising-translations).

* Update manpages, see [gen-manpages.sh](https://github.com/RavenProject/Ravencoin/blob/master/contrib/devtools/README.md#gen-manpagessh).

Before every minor and major release:

* Update [bips.md](bips.md) to account for changes since the last release.
* Update version in `configure.ac` (don't forget to set `CLIENT_VERSION_IS_RELEASE` to `true`)
* Write release notes (see below)
* Update `src/chainparams.cpp` nMinimumChainWork with information from the getblockchaininfo rpc.
* Update `src/chainparams.cpp` defaultAssumeValid  with information from the getblockhash rpc.
  - The selected value must not be orphaned so it may be useful to set the value two blocks back from the tip.
  - Testnet should be set some tens of thousands back from the tip due to reorgs there.
  - This update should be reviewed with a reindex-chainstate with assumevalid=0 to catch any defect
     that causes rejection of blocks in the past history.

Before every major release:

* Update hardcoded [seeds](/contrib/seeds/README.md), see [this pull request](https://github.com/bitcoin/bitcoin/pull/7415) for an example.
* Update [`BLOCK_CHAIN_SIZE`](/src/qt/intro.cpp) to the current size plus some overhead.
* Update `src/chainparams.cpp` chainTxData with statistics about the transaction count and rate.

### Core 4.8.1 pre-tag qualification

For the RIP-25 4.8.1 release, the tag instruction is step 5 below.
Complete this sequence before signing or publishing. Keep the evidence
tied to one full final source SHA. Any source change creates a new
candidate and restarts these gates.

1. Freeze the final source SHA after all code and documentation edits.
   Run `python3 contrib/devtools/test-rip25-release-contract.py` and the
   mandatory `make check` on that final SHA; record green results.
2. Run Build Raven and RIP-25 v4.8 Final Gate on the same final SHA.
   Both workflows must be green, and their reported checkout SHAs must
   match the source SHA from step 1.
3. Download and review artifacts from all five Build Raven targets:
   windows, osx, linux, arm32v7, and aarch64. Check each package against
   its SHA256 checksum and review its provenance, including the recorded
   source SHA. Record the five target results and artifact names before
   proceeding.
4. Present the recorded qualification and operator readiness evidence to
   the owner. Obtain a separate, explicit owner approval to create the
   4.8.1 tag. The rollout go/no-go decision and permission to publish are
   separate decisions. Native Windows and macOS GUI exercise is assigned
   to post-publication community testing; disclose that limitation in the
   release notes and do not describe the native GUIs as verified.
5. Create the 4.8.1 tag on the exact qualified final SHA. Verify the tag
   resolves to that SHA before signing or publishing release material.
   Publication requires its own owner decision.

### First time / New builders


    git clone https://github.com/RavenProject/Ravencoin.git

### Raven maintainers/release engineers, suggestion for writing release notes

Write release notes. git shortlog helps a lot, for example:

    git shortlog --no-merges v(current version, e.g. 0.7.2)..v(new version, e.g. 0.8.0)

Generate list of authors:

    git log --format='%aN' "$*" | sort -ui | sed -e 's/^/- /'

For releases other than 4.8.1, tag the version (or release candidate) in git:

    git tag -s v(new version, e.g. 0.8.0)


### Build binaries. 

TODO: describe process.


Codesigner only: Create Windows/OS X detached signatures:
- Only one person handles codesigning. Everyone else should skip to the next step.
- Only once the Windows/OS X builds each have 3 matching signatures may they be signed with their respective release keys.

Codesigner only: Sign the osx binary:

    transfer raven-osx-unsigned.tar.gz to osx for signing
    tar xf raven-osx-unsigned.tar.gz
    ./detached-sig-create.sh -s "Key ID"
    Enter the keychain password and authorize the signature
    Move signature-osx.tar.gz back to the gitian host

Codesigner only: Sign the windows binaries:

    tar xf raven-win-unsigned.tar.gz
    ./detached-sig-create.sh -key /path/to/codesign.key
    Enter the passphrase for the key when prompted
    signature-win.tar.gz will be created

### After binaries are built:

- Create `SHA256SUMS.asc` for the builds, and GPG-sign it:

```bash
sha256sum * > SHA256SUMS
```

The list of files should be:
```
raven-${VERSION}-aarch64-linux-gnu.tar.gz
raven-${VERSION}-arm-linux-gnueabihf.tar.gz
raven-${VERSION}-x86_64-linux-gnu.tar.gz
raven-${VERSION}-osx64.tar.gz
raven-${VERSION}-osx.dmg
raven-${VERSION}.tar.gz
raven-${VERSION}-win64-setup.exe
raven-${VERSION}-win64.zip

- GPG-sign it, delete the unsigned file:
```
gpg --digest-algo sha256 --clearsign SHA256SUMS # outputs SHA256SUMS.asc
rm SHA256SUMS
```
(the digest algorithm is forced to sha256 to avoid confusion of the `Hash:` header that GPG adds with the SHA256 used for the files)
Note: check that SHA256SUMS itself doesn't end up in SHA256SUMS, which is a spurious/nonsensical entry.

- Upload zips and installers, as well as `SHA256SUMS.asc` from last step, to the github-release page.

- Update ravencoin.org version

- Announce the release:

  - ravencore.org blog post

  - Optionally twitter, reddit /r/Ravencoin, ... but this will usually sort out itself

  - Archive release notes for the new version to `doc/release-notes/` (branch `master` and branch of the release)

  - Create a [new GitHub release](https://github.com/RavenProject/Ravencoin/releases/new) with a link to the archived release notes.

  - Celebrate
