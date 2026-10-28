package=libXau
$(package)_version=1.0.8
$(package)_download_path=http://xorg.freedesktop.org/releases/individual/lib/
$(package)_fallback_download_path=https://download.bitcoinabc.org/depends-sources
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=fdd477320aeb5cdd67272838722d6b7d544887dfe7de46e1e7cc0c27c2bea4f2
$(package)_dependencies=xproto

define $(package)_fetch_cmds
( test -f $(libXau_source) || \
  $(call fetch_file_inner,libXau,$(libXau_download_path),$(libXau_download_file),$(libXau_file_name),$(libXau_sha256_hash)) || \
  $(call fetch_file_inner,libXau,$(libXau_fallback_download_path),$(libXau_download_file),$(libXau_file_name),$(libXau_sha256_hash)) )
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-shared
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
