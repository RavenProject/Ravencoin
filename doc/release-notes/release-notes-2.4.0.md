Raven Core version *2.4.0* is now available!!
==============

  <https://github.com/RavenProject/Ravencoin/releases/tag/v2.4.0>


This is a major release containing bug fixes and enhancements for all builds before it.  It is highly recommended that users 
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

Downgrading warning
==============

You may downgrade at any time if needed.

Note: There are no consensus-rule changes between versions v2.2.3/v2.2.2 and 2.4.0 - running versions in this range
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

Raven Core has not been tested with macOS Catalina(beta). Please use at your own risk.

Notable changes
==============
*This list includes changes from all releases between 2.2.2/2.2.3 and 2.4.0*
- Backport of Bitcoin v0.15.1 networking updates
- Reorganization of pointer access objects in functions


2.4.0 Change log
==============

Changelog available here: <https://github.com/RavenProject/Ravencoin/commits/release_2.4.0>

Credits
==============

Thanks to everyone who directly contributed to this release:

- Most importantly - The Raven Community!
- @blondfrogs (Jeremy Anderson) - [77f5a7838082669dbb7c21b4a93ce65e2a447963](https://github.com/RavenProject/Ravencoin/pull/608/commits/77f5a7838082669dbb7c21b4a93ce65e2a447963)
- @blondfrogs (Jeremy Anderson) - [fbbc40d22646c31465fa04a8a55f4734c3f75a9c](https://github.com/RavenProject/Ravencoin/pull/608/commits/fbbc40d22646c31465fa04a8a55f4734c3f75a9c)
- @practicalswift - [ba4d362a2b4ba814bba2aa2589231ba470f20e3f](https://github.com/RavenProject/Ravencoin/pull/608/commits/ba4d362a2b4ba814bba2aa2589231ba470f20e3f)
- @sdaftuar - [3027dd58001624f388dc522ec9c73eb5fc9aee70](https://github.com/RavenProject/Ravencoin/pull/608/commits/3027dd58001624f388dc522ec9c73eb5fc9aee70)
- @sdaftuar - [dc5cc1e5980b30a21427f75a7d4085f4755d4085](https://github.com/RavenProject/Ravencoin/pull/608/commits/dc5cc1e5980b30a21427f75a7d4085f4755d4085)
- @sdaftuar - [a743e754fd90ce3299b237700082e12c8cbcf3b4](https://github.com/RavenProject/Ravencoin/pull/608/commits/a743e754fd90ce3299b237700082e12c8cbcf3b4)
- @sdaftuar - [08efce585df53f86f13ec5d1dff689237c0ce4cc](https://github.com/RavenProject/Ravencoin/pull/608/commits/08efce585df53f86f13ec5d1dff689237c0ce4cc)
- @sdaftuar - [2d4826beee72e1a3749c1eb090a37139f3099a22](https://github.com/RavenProject/Ravencoin/pull/608/commits/2d4826beee72e1a3749c1eb090a37139f3099a22)
- @TheBlueMatt - [ffeb6ee6b27e778fdee188d12032b6ac7d2d1eb4](https://github.com/RavenProject/Ravencoin/pull/608/commits/ffeb6ee6b27e778fdee188d12032b6ac7d2d1eb4)
- @TheBlueMatt - [c27081c79eb6dca48e457d8dd5b09090771a75b7](https://github.com/RavenProject/Ravencoin/pull/608/commits/c27081c79eb6dca48e457d8dd5b09090771a75b7)

