package=dbus
$(package)_version=1.10.18
$(package)_download_path=https://dbus.freedesktop.org/releases/dbus
$(package)_fallback_download_path=https://download.bitcoinabc.org/depends-sources
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=6049ddd5f3f3e2618f615f1faeda0a115104423a7996b7aa73e2f36e38cc514a
$(package)_dependencies=expat

define $(package)_fetch_cmds
( test -f $(dbus_source) || \
  $(call fetch_file_inner,dbus,$(dbus_download_path),$(dbus_download_file),$(dbus_file_name),$(dbus_sha256_hash)) || \
  $(call fetch_file_inner,dbus,$(dbus_fallback_download_path),$(dbus_download_file),$(dbus_file_name),$(dbus_sha256_hash)) )
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-tests --disable-doxygen-docs --disable-xml-docs --disable-static --without-x
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C dbus libdbus-1.la
endef

define $(package)_stage_cmds
  $(MAKE) -C dbus DESTDIR=$($(package)_staging_dir) install-libLTLIBRARIES install-dbusincludeHEADERS install-nodist_dbusarchincludeHEADERS && \
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-pkgconfigDATA
endef
