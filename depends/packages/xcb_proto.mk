package=xcb_proto
$(package)_version=1.10
$(package)_download_path=http://xcb.freedesktop.org/dist
$(package)_fallback_download_path=https://download.bitcoinabc.org/depends-sources
$(package)_file_name=xcb-proto-$($(package)_version).tar.bz2
$(package)_sha256_hash=7ef40ddd855b750bc597d2a435da21e55e502a0fefa85b274f2c922800baaf05

define $(package)_fetch_cmds
( test -f $(xcb_proto_source) || \
  $(call fetch_file_inner,xcb_proto,$(xcb_proto_download_path),$(xcb_proto_download_file),$(xcb_proto_file_name),$(xcb_proto_sha256_hash)) || \
  $(call fetch_file_inner,xcb_proto,$(xcb_proto_fallback_download_path),$(xcb_proto_download_file),$(xcb_proto_file_name),$(xcb_proto_sha256_hash)) )
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

define $(package)_postprocess_cmds
  find -name "*.pyc" -delete && \
  find -name "*.pyo" -delete
endef
