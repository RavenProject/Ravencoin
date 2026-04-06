package=liboqs
$(package)_version=0.12.0
$(package)_download_path=https://github.com/open-quantum-safe/liboqs/archive/refs/tags/
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=TODO_REPLACE_WITH_ACTUAL_HASH
$(package)_dependencies=
$(package)_patches=

define $(package)_set_vars
  $(package)_config_opts=-DOQS_BUILD_ONLY_LIB=ON
  $(package)_config_opts+=-DOQS_MINIMAL_BUILD="SIG_ml_dsa_44"
  $(package)_config_opts+=-DOQS_USE_OPENSSL=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DOQS_DIST_BUILD=ON
endef

define $(package)_config_cmds
  cmake -S . -B build $($(package)_config_opts)
endef

define $(package)_build_cmds
  cmake --build build --parallel
endef

define $(package)_stage_cmds
  cmake --install build --prefix $($(package)_staging_prefix_dir)
endef
