Raven Core version *2.2.2* is now available!!
==============

  <https://github.com/RavenProject/Ravencoin/releases/tag/v2.2.2>


This is a major release containing bug fixes and enhancements for 2.2.0/2.2.1.  It is highly recommended that users 
upgrade to this version.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/RavenProject/Ravencoin/issues>

To receive security and update notifications, please subscribe to:

  <https://ravencoin.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the 
installer (on Windows) or just copy over `/Applications/Raven-Qt` (on Mac)
or `ravend`/`raven-qt` (on Linux).

The first time you run version 2.1.0 or higher, your chainstate database may
be converted to a new format, which will take anywhere from a few minutes to
half an hour, depending on the speed of your machine.

Downgrading warning
==============

If you are upgrading to 2.2.2 from a version before 2.1.0 the chainstate database for this release is 
not compatible.  If you run 2.1.0 or newer and then decide to switch back to any
older version, you will need to run the old release with the `-reindex-chainstate`
option to rebuild the chainstate data structures in the old format.

If your node has pruning enabled, this will entail re-downloading and
processing the entire blockchain.

It is not recommended that users downgrade their version.  Version 2.1.0 and later contain
changes that *will* fork the chain, users not running 2.1.0 (or later) will be not
be able to participate in this fork process and will be left on the old chain which 
will not be valid.

Note: There are no consensus-rule changes between versions 2.1.0 and 2.2.2 - running versions in this range
will not fork the chain. 

Compatibility
==============

Raven Core is extensively tested on multiple operating systems using
the Linux kernel, macOS 10.8+, and Windows 10(x64) and later. 32-bit versions of Windows,
and Windows XP are not supported.

Raven Core should also work on most other Unix-like systems but is not
frequently tested on them.

Raven Core has been tested with macOS 10.14 Mojave, but it is recommended that *developers*
do not update to Mojave.  There is an incompatibility with Berkeley-db 4.8.30 that causes
the binaries to seg-fault.  There is a workaround, but as of this release users should
not update to Mojave (see build-OSX.md for current status of this issue).  There are no
known issues running the release binaries on Mojave.

Notable changes
==============
*This list includes changes from all releases between 2.1.0 and 2.2.2*
- Reduction in memory usage
- Chain synchronization speed
- New QT interface
- QT dark mode (not using OSX Mojave dark mode)
- Chain split attack vector fix
- Better asset caching
- Enhancements for Ravencoin Dev Kit and Mobile Wallet Support
- Limit listaddressesbyasset RPC call to only return 5,000 asset-addresses per call
- Updates and fixes to the functional and unit tests for better asset coverage and stability


2.2.2 Change log
==============

Changelog available here: <https://github.com/RavenProject/Ravencoin/commits/release_2.2.2>

Credits
==============

Thanks to everyone who directly contributed to this release:

- Most importantly - The Raven Community!
- Tron Black
- Jesse Empey
- Jeremy Anderson
- Corbin Fox
- Daben Steele
- Cade Call
- @Roshii
- @underdarkskies
- Mark Ney
