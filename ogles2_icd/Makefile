# ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
# Builds as an AmigaOS shared library using -nostartfiles
# Requires Warp3DNova.library headers from external/Warp3DNova
# Requires SPIRV-Cross source in external/spirv-cross/

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/..:/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 $(DEBUG) -Wall \
           -D__NOLIBBASE__ -D__NOGLOBALIFACE__
LDFLAGS  = -mcrt=newlib -nostartfiles
DEBUG	:=

TARGET   = ../build/ogles2_vk.library

# SPIRV-Cross source files (C++ compiled with g++)
SPVC_DIR = external/spirv-cross
SPVC_SRCS = spirv_cfg.cpp spirv_cross_parsed_ir.cpp spirv_cross.cpp \
            spirv_glsl.cpp spirv_parser.cpp spirv_cross_c.cpp
SPVC_CXX = ppc-amigaos-g++ -std=c++11 -O2 -mcrt=newlib \
           -fno-exceptions -fno-rtti \
           -DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS -DNDEBUG \
           -DSPIRV_CROSS_C_API_GLSL=1

.PHONY: all clean

all: $(TARGET)

$(TARGET):
	$(DOCKER_RUN) sh -c ' \
		SDK=/opt/ppc-amigaos/ppc-amigaos/SDK/include/include_h && \
		cp -r include/vulkan $$SDK/ && \
		cp include/interfaces/vulkan.h $$SDK/interfaces/ && \
		cp include/inline4/vulkan.h $$SDK/inline4/ && \
		cp include/proto/vulkan.h $$SDK/proto/ && \
		cp include/clib/vulkan_protos.h $$SDK/clib/ && \
		cp -r external/Warp3DNova/Warp3DNova $$SDK/ && \
		cp external/Warp3DNova/Warp3DNova.h $$SDK/inline4/ && \
		cp external/OGLES2/ogles2.h $$SDK/interfaces/ && \
		mkdir -p $$SDK/ogles2 && \
		cp external/OGLES2/ogles2/ogles2_defs.h $$SDK/ogles2/ && \
		mkdir -p $$SDK/GLES2 && \
		cp external/OGLES2/GLES2/*.h $$SDK/GLES2/ && \
		mkdir -p build && \
		echo "--- Patching SPIRV-Cross for AmigaOS shared library ---" && \
		sed -i "s/static const unordered_set<string> keywords/const unordered_set<string> keywords/" \
			$(SPVC_DIR)/spirv_glsl.cpp && \
		echo "--- Building SPIRV-Cross static library ---" && \
		for src in $(SPVC_SRCS); do \
			obj=$$(echo $$src | sed "s/.cpp/.o/"); \
			echo "  CXX $$src"; \
			$(SPVC_CXX) -I$(SPVC_DIR) \
				-c $(SPVC_DIR)/$$src -o build/spvc_$$obj; \
		done && \
		ppc-amigaos-ar rcs build/libspirv-cross.a \
			build/spvc_spirv_cfg.o \
			build/spvc_spirv_cross_parsed_ir.o \
			build/spvc_spirv_cross.o \
			build/spvc_spirv_glsl.o \
			build/spvc_spirv_parser.o \
			build/spvc_spirv_cross_c.o && \
		echo "--- Building gthread stubs ---" && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) \
			-c loader/gthread_stubs.c -o build/gthread_stubs.o && \
		echo "--- Building ICD C objects ---" && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_main.c -o build/ogles2vk_main.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_device.c -o build/ogles2vk_device.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_shader.c -o build/ogles2vk_shader.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_cmdbuf.c -o build/ogles2vk_cmdbuf.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_sync.c -o build/ogles2vk_sync.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include -Iexternal/spirv-cross \
			-c ogles2_icd/src/ogles2vk_exec.c -o build/ogles2vk_exec.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include -Iexternal/spirv-cross \
			-c ogles2_icd/src/ogles2vk_spirv2glsl.c -o build/ogles2vk_spirv2glsl.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 $(DEBUG) -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iogles2_icd/include \
			-c ogles2_icd/src/ogles2vk_wsi.c -o build/ogles2vk_wsi.o && \
		echo "--- Linking ogles2_vk.library (g++ for C++ runtime) ---" && \
		ppc-amigaos-g++ -mcrt=newlib -nostartfiles \
			-o build/ogles2_vk.library \
			build/ogles2vk_main.o build/ogles2vk_device.o build/ogles2vk_shader.o \
			build/ogles2vk_cmdbuf.o build/ogles2vk_sync.o build/ogles2vk_exec.o \
			build/ogles2vk_spirv2glsl.o build/ogles2vk_wsi.o \
			build/libspirv-cross.a build/gthread_stubs.o \
			-latomic \
		'

clean:
	rm -f ../build/ogles2vk_*.o ../build/ogles2_vk.library \
	      ../build/spvc_*.o ../build/libspirv-cross.a \
	      ../build/gthread_stubs.o
