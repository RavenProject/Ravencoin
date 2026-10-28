package=fontconfig
$(package)_version=2.12.1
$(package)_download_path=https://mirrors.mit.edu/macports/distfiles/fontconfig/
$(package)_fallback_download_path=https://distfiles.macports.org/fontconfig
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=b449a3e10c47e1d1c7a6ec6e2016cca73d3bd68fbbd4f0ae5cc6b573f7d6c7f3
$(package)_dependencies=freetype expat

define $(package)_fetch_cmds
( test -f $(fontconfig_source) || \
  $(call fetch_file_inner,fontconfig,$(fontconfig_download_path),$(fontconfig_download_file),$(fontconfig_file_name),$(fontconfig_sha256_hash)) || \
  $(call fetch_file_inner,fontconfig,$(fontconfig_fallback_download_path),$(fontconfig_download_file),$(fontconfig_file_name),$(fontconfig_sha256_hash)) )
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-docs --disable-static
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

# 2.12.1 uses CHAR_WIDTH which is reserved and clashes with some glibc versions, but newer versions of fontconfig
# have broken makefiles which needlessly attempt to re-generate headers with gperf.
# Instead, change all uses of CHAR_WIDTH, and disable the rule that forces header re-generation.
# This can be removed once the upstream build is fixed.
define $(package)_build_cmds
  sed -i 's/CHAR_WIDTH/CHARWIDTH/g' fontconfig/fontconfig.h src/fcobjshash.gperf src/fcobjs.h src/fcobjshash.h && \
  sed -i 's/fcobjshash.h: fcobjshash.gperf/fcobjshash.h:/' src/Makefile && \
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
