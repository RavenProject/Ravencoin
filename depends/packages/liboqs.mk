package=liboqs
$(package)_version=0.16.0
$(package)_download_path=https://github.com/open-quantum-safe/liboqs/archive/refs/tags/
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=162d5b510518ee5f285f82fa1f16402a885176e818bf1b1a4c3c91c9a2f01eae
$(package)_dependencies=
$(package)_patches=rip25_pkgconfig_provenance.patch
$(package)_build_subdir=build

define $(package)_set_vars
  $(package)_config_opts=-DOQS_BUILD_ONLY_LIB=ON
  $(package)_config_opts+=-DOQS_MINIMAL_BUILD="SIG_ml_dsa_44"
  $(package)_config_opts+=-DOQS_USE_OPENSSL=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DOQS_DIST_BUILD=ON
  $(package)_config_opts_arm=-DCMAKE_SYSTEM_PROCESSOR=armv7
  $(package)_config_opts_aarch64=-DCMAKE_SYSTEM_PROCESSOR=aarch64
  $(package)_config_opts_x86_64=-DCMAKE_SYSTEM_PROCESSOR=x86_64
  $(package)_config_opts_mingw32=-DOQS_DIST_BUILD=OFF
  # The darwin CC wrapper starts with 'env', which CMake's ASM detection
  # misreads as the compiler itself; name a real assembler driver for XKCP.
  $(package)_config_opts_darwin=-DCMAKE_ASM_COMPILER=$(clang_prog) -DCMAKE_ASM_COMPILER_TARGET=$(host)
endef

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/rip25_pkgconfig_provenance.patch && \
  mkdir -p build
endef

define $(package)_config_cmds
  $($(package)_cmake) .. $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
