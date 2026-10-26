LITERT_RUNTIME_VERSION = 2.3.0
LITERT_RUNTIME_SITE = $(call github,google-ai-edge,LiteRT,v$(LITERT_RUNTIME_VERSION))
LITERT_RUNTIME_LICENSE = Apache-2.0
LITERT_RUNTIME_LICENSE_FILES = LICENSE
LITERT_RUNTIME_INSTALL_STAGING = YES
LITERT_RUNTIME_SUBDIR = litert
LITERT_RUNTIME_SUPPORTS_IN_SOURCE_BUILD = NO
LITERT_RUNTIME_EXTRA_DOWNLOADS = \
 https://github.com/tensorflow/tensorflow/archive/refs/tags/v2.21.0-rc0.tar.gz \
 https://storage.googleapis.com/mirror.tensorflow.org/gitlab.com/libeigen/eigen/-/archive/ea13a98decd497a8c5588fb5de71b57bcf10d864/eigen-ea13a98decd497a8c5588fb5de71b57bcf10d864.tar.gz \
 https://github.com/jax-ml/ml_dtypes/archive/00d98cd92ade342fef589c0470379abb27baebe9.tar.gz \
 https://github.com/google/XNNPACK/archive/d89ef6669a14db203b3b7935b1b3862cb63fb6df.tar.gz \
 https://github.com/protocolbuffers/protobuf/archive/90b73ac3f0b10320315c2ca0d03a5a9b095d2f66.tar.gz

define LITERT_RUNTIME_UNPACK_SOURCES
	mkdir -p $(@D)/deps
	set -e; $(foreach archive,$(notdir $(LITERT_RUNTIME_EXTRA_DOWNLOADS)),tar -xf $(LITERT_RUNTIME_DL_DIR)/$(archive) -C $(@D)/deps;)
endef
LITERT_RUNTIME_POST_EXTRACT_HOOKS += LITERT_RUNTIME_UNPACK_SOURCES
LITERT_RUNTIME_CONF_OPTS = \
 -DCMAKE_CXX_COMPILER=$(TARGET_CXX) \
 -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,noexecstack \
 -DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -D_GNU_SOURCE" \
 -DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -DFLATBUFFERS_LOCALE_INDEPENDENT=0" \
 -DBUILD_SHARED_LIBS=OFF \
 -DLITERT_ENABLE_GPU=OFF -DLITERT_ENABLE_NPU=OFF \
 -DLITERT_DISABLE_KLEIDIAI=ON -DLITERT_BUILD_TESTS=OFF \
 -DTFLITE_ENABLE_GPU=OFF -DTFLITE_ENABLE_NNAPI=OFF -DTFLITE_ENABLE_XNNPACK=ON \
 -DLITERT_HOST_C_COMPILER=$(HOSTCC) -DLITERT_HOST_CXX_COMPILER=$(HOSTCXX) \
 -DTENSORFLOW_SOURCE_DIR=$(@D)/deps/tensorflow-2.21.0-rc0 \
 -DFETCHCONTENT_SOURCE_DIR_EIGEN=$(@D)/deps/eigen-ea13a98decd497a8c5588fb5de71b57bcf10d864 \
 -DFETCHCONTENT_SOURCE_DIR_ML_DTYPES=$(@D)/deps/ml_dtypes-00d98cd92ade342fef589c0470379abb27baebe9 \
 -DFETCHCONTENT_SOURCE_DIR_XNNPACK=$(@D)/deps/XNNPACK-d89ef6669a14db203b3b7935b1b3862cb63fb6df \
 -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$(@D)/deps/protobuf-90b73ac3f0b10320315c2ca0d03a5a9b095d2f66
LITERT_RUNTIME_BUILD_OPTS = --target litert_runtime_c_api_shared_lib

define LITERT_RUNTIME_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 755 $(LITERT_RUNTIME_BUILDDIR)/c/libLiteRt.so $(STAGING_DIR)/usr/lib/libLiteRt.so
	mkdir -p $(STAGING_DIR)/usr/include/litert
	cp -a $(@D)/litert/c $(@D)/litert/build_common $(STAGING_DIR)/usr/include/litert/
	cp -a $(LITERT_RUNTIME_BUILDDIR)/include/litert/. $(STAGING_DIR)/usr/include/litert/
endef

define LITERT_RUNTIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(LITERT_RUNTIME_BUILDDIR)/c/libLiteRt.so $(TARGET_DIR)/usr/lib/libLiteRt.so
	$(INSTALL) -D -m 644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/litert-runtime/LICENSE
	for source in $(@D)/deps/*/LICENSE* $(@D)/deps/*/COPYING* \
	 $(LITERT_RUNTIME_BUILDDIR)/*/LICENSE* $(LITERT_RUNTIME_BUILDDIR)/*/COPYING* \
	 $(LITERT_RUNTIME_BUILDDIR)/_deps/*-src/LICENSE*; do \
	 [ -f "$$source" ] || continue; \
	 name=$$(basename $$(dirname "$$source"))-$$(basename "$$source"); \
	 $(INSTALL) -m 644 "$$source" "$(TARGET_DIR)/usr/share/licenses/litert-runtime/$$name"; \
	done
endef
$(eval $(cmake-package))
