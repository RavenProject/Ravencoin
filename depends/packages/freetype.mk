package=freetype
$(package)_version=2.7.1
$(package)_download_path=https://download-mirror.savannah.gnu.org/releases/$(package)
$(package)_fallback_download_path=https://downloads.sourceforge.net/project/freetype/freetype2/$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=3a3bb2c4e15ffb433f2032f50a5b5a92558206822e22bfe8cbe339af4aa82f88

define $(package)_fetch_cmds
( test -f $(freetype_source) || \
  $(call fetch_file_inner,freetype,$(freetype_download_path),$(freetype_download_file),$(freetype_file_name),$(freetype_sha256_hash)) || \
  $(call fetch_file_inner,freetype,$(freetype_fallback_download_path),$(freetype_download_file),$(freetype_file_name),$(freetype_sha256_hash)) )
endef

define $(package)_set_vars
  $(package)_config_opts=--without-zlib --without-png --disable-static
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
