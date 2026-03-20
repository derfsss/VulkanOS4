# vulkan.library -- Vulkan Loader for AmigaOS 4
# Builds as an AmigaOS shared library using -nostartfiles

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/..:/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall -Iinclude -Iinclude/include_h \
           -D__NOLIBBASE__ -D__NOGLOBALIFACE__
LDFLAGS  = -mcrt=newlib -nostartfiles

SOURCES  = src/loader_main.c src/loader_dispatch.c src/loader_icd.c src/loader_wsi.c
TARGET   = ../build/vulkan.library

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
		mkdir -p build && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iloader/include \
			-c loader/src/loader_main.c -o build/loader_main.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iloader/include \
			-c loader/src/loader_dispatch.c -o build/loader_dispatch.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iloader/include \
			-c loader/src/loader_icd.c -o build/loader_icd.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Iloader/include \
			-c loader/src/loader_wsi.c -o build/loader_wsi.o && \
		ppc-amigaos-gcc -mcrt=newlib -nostartfiles \
			-o build/vulkan.library \
			build/loader_main.o build/loader_dispatch.o \
			build/loader_icd.o build/loader_wsi.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-c loader/src/vulkan_autoinit.c -o build/vulkan_autoinit.o && \
		ppc-amigaos-ar rcs build/libvulkan_loader.a build/vulkan_autoinit.o \
		'

clean:
	rm -f ../build/loader_*.o ../build/vulkan.library ../build/vulkan_autoinit.o ../build/libvulkan_loader.a
