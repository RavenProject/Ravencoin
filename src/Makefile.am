# Copyright (c) 2013-2016 The Bitcoin Core developers
# Copyright (c) 2017-2019 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_UNIVALUE
LIBUNIVALUE = univalue/libunivalue.la

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
else
LIBUNIVALUE = $(UNIVALUE_LIBS)
endif

YOTTAFLUX_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

YOTTAFLUX_INCLUDES += -I$(srcdir)/secp256k1/include
YOTTAFLUX_INCLUDES += $(UNIVALUE_CFLAGS)

LIBYOTTAFLUX_SERVER=libyottaflux_server.a
LIBYOTTAFLUX_COMMON=libyottaflux_common.a
LIBYOTTAFLUX_CONSENSUS=libyottaflux_consensus.a
LIBYOTTAFLUX_CLI=libyottaflux_cli.a
LIBYOTTAFLUX_UTIL=libyottaflux_util.a
LIBYOTTAFLUX_CRYPTO=crypto/libyottaflux_crypto.a
LIBYOTTAFLUXQT=qt/libyottafluxqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBYOTTAFLUX_ZMQ=libyottaflux_zmq.a
endif
if BUILD_YOTTAFLUX_LIBS
LIBYOTTAFLUXCONSENSUS=libyottafluxconsensus.la
endif
if ENABLE_WALLET
LIBYOTTAFLUX_WALLET=libyottaflux_wallet.a
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBYOTTAFLUX_CRYPTO) \
  $(LIBYOTTAFLUX_UTIL) \
  $(LIBYOTTAFLUX_COMMON) \
  $(LIBYOTTAFLUX_CONSENSUS) \
  $(LIBYOTTAFLUX_SERVER) \
  $(LIBYOTTAFLUX_CLI) \
  $(LIBYOTTAFLUX_WALLET) \
  $(LIBYOTTAFLUX_ZMQ)

lib_LTLIBRARIES = $(LIBYOTTAFLUXCONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_YOTTAFLUXD
  bin_PROGRAMS += yottafluxd
endif

if BUILD_YOTTAFLUX_CLI
  bin_PROGRAMS += yottaflux-cli
endif

if BUILD_YOTTAFLUX_TX
  bin_PROGRAMS += yottaflux-tx
endif

.PHONY: FORCE check-symbols check-security
# yottaflux core #
YOTTAFLUX_CORE_H = \
  addrdb.h \
  addressindex.h \
  algo/sph_groestl.h \
  algo/sph_types.h \
  algo/sph_blake.h \
  algo/sph_bmw.h \
  algo/sph_jh.h \
  algo/sph_keccak.h \
  algo/sph_skein.h \
  algo/sph_luffa.h \
  algo/sph_cubehash.h \
  algo/sph_simd.h \
  algo/sph_echo.h \
  algo/sph_shavite.h \
  algo/sph_sha2.h \
  algo/sph_haval.h \
  algo/sph_tiger.h \
  algo/lyra2.h \
  algo/sponge.h \
  algo/gost_streebog.h \
  algo/groestl.c \
  algo/blake.c \
  algo/bmw.c \
  algo/cubehash.c \
  algo/echo.c \
  algo/jh.c \
  algo/keccak.c \
  algo/luffa.c \
  algo/shavite.c \
  algo/simd.c \
  algo/skein.c \
  algo/hamsi.c \
  algo/whirlpool.c \
  algo/shabal.c \
  algo/fugue.c \
  algo/sha2.c \
  algo/extra.c \
  algo/extra.h \
  algo/sph_sha2big.c \
  algo/haval.c \
  algo/tiger.cpp \
  algo/lyra2.cpp \
  algo/sponge.cpp \
  algo/sph_sha2.c \
  algo/gost_streebog.c \
  spentindex.h \
  timestampindex.h \
  addrman.h \
  assets/assets.h \
  assets/assetdb.h \
  assets/assettypes.h \
  assets/messages.h \
  assets/myassetsdb.h \
  assets/restricteddb.h \
  assets/snapshotrequestdb.h \
  assets/assetsnapshotdb.h \
  assets/rewards.h \
  base58.h \
  bloom.h \
  blockencodings.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  indirectmap.h \
  init.h \
  key.h \
  keystore.h \
  dbwrapper.h \
  limitedmap.h \
  memusage.h \
  merkleblock.h \
  miner.h \
  net.h \
  net_processing.h \
  netaddress.h \
  netbase.h \
  netmessagemaker.h \
  noui.h \
  policy/feerate.h \
  policy/fees.h \
  policy/policy.h \
  policy/rbf.h \
  pow.h \
  protocol.h \
  random.h \
  reverse_iterator.h \
  reverselock.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/mining.h \
  rpc/protocol.h \
  rpc/safemode.h \
  rpc/server.h \
  rpc/register.h \
  scheduler.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  script/ismine.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  undo.h \
  util.h \
  utilmoneystr.h \
  utiltime.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  wallet/coincontrol.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/feebumper.h \
  wallet/fees.h \
  wallet/init.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  wallet/bip39.h \
  wallet/bip39_english.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libyottaflux_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between yottafluxd and yottaflux-qt
libyottaflux_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libyottaflux_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_server_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  bloom.cpp \
  blockencodings.cpp \
  chain.cpp \
  checkpoints.cpp \
  consensus/consensus.cpp \
  consensus/tx_verify.cpp \
  httprpc.cpp \
  httpserver.cpp \
  init.cpp \
  dbwrapper.cpp \
  merkleblock.cpp \
  miner.cpp \
  net.cpp \
  net_processing.cpp \
  noui.cpp \
  assets/assets.cpp \
  assets/assetdb.cpp \
  assets/assettypes.cpp \
  assets/messages.cpp \
  assets/myassetsdb.cpp \
  assets/restricteddb.cpp \
  assets/snapshotrequestdb.cpp \
  assets/assetsnapshotdb.cpp \
  assets/rewards.cpp \
  policy/fees.cpp \
  policy/policy.cpp \
  policy/rbf.cpp \
  pow.cpp \
  rest.cpp \
  rpc/assets.cpp \
  rpc/blockchain.cpp \
  rpc/messages.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/rewards.cpp \
  rpc/safemode.cpp \
  rpc/server.cpp \
  script/sigcache.cpp \
  script/ismine.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  ui_interface.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  $(YOTTAFLUX_CORE_H)

if ENABLE_ZMQ
libyottaflux_zmq_a_CPPFLAGS = $(YOTTAFLUX_INCLUDES) $(ZMQ_CFLAGS)
libyottaflux_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif


# wallet: shared between yottafluxd and yottaflux-qt, but only linked
# when wallet enabled
libyottaflux_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
libyottaflux_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_wallet_a_SOURCES = \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/feebumper.cpp \
  wallet/fees.cpp \
  wallet/init.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  wallet/bip39.cpp \
  $(YOTTAFLUX_CORE_H)

# crypto primitives library
crypto_libyottaflux_crypto_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libyottaflux_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libyottaflux_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.h \
  crypto/sha512.cpp \
  crypto/ethash/include/ethash/ethash.h \
  crypto/ethash/include/ethash/ethash.hpp \
  crypto/ethash/include/ethash/hash_types.h \
  crypto/ethash/include/ethash/hash_types.hpp \
  crypto/ethash/include/ethash/keccak.h \
  crypto/ethash/include/ethash/keccak.hpp \
  crypto/ethash/include/ethash/progpow.hpp \
  crypto/ethash/include/ethash/version.h \
  crypto/ethash/lib/ethash/bit_manipulation.h \
  crypto/ethash/lib/ethash/builtins.h \
  crypto/ethash/lib/ethash/endianness.hpp \
  crypto/ethash/lib/ethash/ethash.cpp \
  crypto/ethash/lib/ethash/ethash-internal.hpp \
  crypto/ethash/lib/ethash/kiss99.hpp \
  crypto/ethash/lib/ethash/managed.cpp \
  crypto/ethash/lib/ethash/primes.c \
  crypto/ethash/lib/ethash/primes.h \
  crypto/ethash/lib/ethash/progpow.cpp \
  crypto/ethash/lib/keccak/keccak.c \
  crypto/ethash/lib/keccak/keccakf1600.c \
  crypto/ethash/lib/keccak/keccakf800.c \
  crypto/ethash/lib/support/attributes.h \
  crypto/ethash/helpers.hpp \
  crypto/ethash/progpow_test_vectors.hpp

if USE_ASM
crypto_libyottaflux_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

# consensus: shared between all executables that validate any consensus rules.
libyottaflux_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
libyottaflux_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_consensus_a_SOURCES = \
  amount.h \
  arith_uint256.cpp \
  arith_uint256.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/validation.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/yottafluxconsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  support/cleanse.h \
  support/cleanse.cpp \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  utilstrencodings.cpp \
  utilstrencodings.h \
  version.h \
  LibBoolEE.cpp \
  LibBoolEE.h \
  $(YOTTAFLUX_CORE_H)

# common: shared between yottafluxd, and yottaflux-qt and non-server tools
libyottaflux_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
libyottaflux_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_common_a_SOURCES = \
  base58.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  key.cpp \
  keystore.cpp \
  netaddress.cpp \
  netbase.cpp \
  policy/feerate.cpp \
  protocol.cpp \
  scheduler.cpp \
  script/sign.cpp \
  script/standard.cpp \
  warnings.cpp \
  $(YOTTAFLUX_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libyottaflux_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
libyottaflux_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  fs.cpp \
  random.cpp \
  rpc/protocol.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  util.cpp \
  utilmoneystr.cpp \
  utilstrencodings.cpp \
  utiltime.cpp \
  $(YOTTAFLUX_CORE_H)

if GLIBC_BACK_COMPAT
libyottaflux_util_a_SOURCES += compat/glibc_compat.cpp
AM_LDFLAGS += $(COMPAT_LDFLAGS)
endif

# cli: shared between yottaflux-cli and yottaflux-qt
libyottaflux_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
libyottaflux_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libyottaflux_cli_a_SOURCES = \
  rpc/client.cpp \
  $(YOTTAFLUX_CORE_H)

nodist_libyottaflux_util_a_SOURCES = $(srcdir)/obj/build.h
#

# yottafluxd binary #
yottafluxd_SOURCES = yottafluxd.cpp
yottafluxd_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
yottafluxd_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
yottafluxd_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
yottafluxd_SOURCES += yottafluxd-res.rc
endif

yottafluxd_LDADD = \
  $(LIBYOTTAFLUX_SERVER) \
  $(LIBYOTTAFLUX_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBYOTTAFLUX_UTIL) \
  $(LIBYOTTAFLUX_WALLET) \
  $(LIBYOTTAFLUX_ZMQ) \
  $(LIBYOTTAFLUX_CONSENSUS) \
  $(LIBYOTTAFLUX_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

yottafluxd_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS)

# yottaflux-cli binary #
yottaflux_cli_SOURCES = yottaflux-cli.cpp
yottaflux_cli_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES) $(EVENT_CFLAGS)
yottaflux_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
yottaflux_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
yottaflux_cli_SOURCES += yottaflux-cli-res.rc
endif

yottaflux_cli_LDADD = \
  $(LIBYOTTAFLUX_CLI) \
  $(LIBUNIVALUE) \
  $(LIBYOTTAFLUX_UTIL) \
  $(LIBYOTTAFLUX_CRYPTO)

yottaflux_cli_LDADD += $(BOOST_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(EVENT_LIBS)
#

# yottaflux-tx binary #
yottaflux_tx_SOURCES = yottaflux-tx.cpp
yottaflux_tx_CPPFLAGS = $(AM_CPPFLAGS) $(YOTTAFLUX_INCLUDES)
yottaflux_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
yottaflux_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
yottaflux_tx_SOURCES += yottaflux-tx-res.rc
endif

yottaflux_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBYOTTAFLUX_COMMON) \
  $(LIBYOTTAFLUX_UTIL) \
  $(LIBYOTTAFLUX_CONSENSUS) \
  $(LIBYOTTAFLUX_CRYPTO) \
  $(LIBSECP256K1)

yottaflux_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)
#

# yottafluxconsensus library #
if BUILD_YOTTAFLUX_LIBS
include_HEADERS = script/yottafluxconsensus.h
libyottafluxconsensus_la_SOURCES = $(crypto_libyottaflux_crypto_a_SOURCES) $(libyottaflux_consensus_a_SOURCES)

if GLIBC_BACK_COMPAT
  libyottafluxconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libyottafluxconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libyottafluxconsensus_la_LIBADD = $(LIBSECP256K1) $(BOOST_LIBS)
libyottafluxconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_YOTTAFLUX_INTERNAL
libyottafluxconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno
CLEANFILES += obj/build.h

EXTRA_DIST = $(CTAES_DIST)


config/yottaflux-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/yottaflux-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/yottaflux-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/yottaflux-config.h.in

clean-local:
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C univalue clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py < $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif
