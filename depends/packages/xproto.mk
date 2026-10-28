package=xproto
$(package)_version=7.0.26
$(package)_download_path=http://xorg.freedesktop.org/releases/individual/proto
$(package)_fallback_download_path=https://download.bitcoinabc.org/depends-sources
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=636162c1759805a5a0114a369dffdeccb8af8c859ef6e1445f26a4e6e046514f

define $(package)_fetch_cmds
( test -f $(xproto_source) || \
  $(call fetch_file_inner,xproto,$(xproto_download_path),$(xproto_download_file),$(xproto_file_name),$(xproto_sha256_hash)) || \
  $(call fetch_file_inner,xproto,$(xproto_fallback_download_path),$(xproto_download_file),$(xproto_file_name),$(xproto_sha256_hash)) )
endef

define $(package)_set_vars
$(package)_config_opts=--disable-shared
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
