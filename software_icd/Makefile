# software_vk.library -- Software Vulkan ICD for AmigaOS 4
# Builds as an AmigaOS shared library using -nostartfiles

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/..:/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall \
           -D__NOLIBBASE__ -D__NOGLOBALIFACE__
LDFLAGS  = -mcrt=newlib -nostartfiles
DEBUG	:=

SOURCES  = src/swvk_main.c src/swvk_instance.c src/swvk_device.c \
           src/swvk_memory.c src/swvk_buffer.c \
           src/swvk_pipeline.c src/swvk_spirv.c src/swvk_cmdbuf.c \
           src/swvk_draw.c src/swvk_sync.c src/swvk_wsi.c \
           src/swvk_descriptor.c
TARGET   = ../build/software_vk.library

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
			-Isoftware_icd/include \
			-c software_icd/src/swvk_main.c -o build/swvk_main.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_instance.c -o build/swvk_instance.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_device.c -o build/swvk_device.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_memory.c -o build/swvk_memory.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_buffer.c -o build/swvk_buffer.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_pipeline.c -o build/swvk_pipeline.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_spirv.c -o build/swvk_spirv.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_cmdbuf.c -o build/swvk_cmdbuf.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_draw.c -o build/swvk_draw.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_sync.c -o build/swvk_sync.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_wsi.c -o build/swvk_wsi.o && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__NOLIBBASE__ -D__NOGLOBALIFACE__ \
			-Isoftware_icd/include \
			-c software_icd/src/swvk_descriptor.c -o build/swvk_descriptor.o && \
		ppc-amigaos-gcc -mcrt=newlib -nostartfiles \
			-o build/software_vk.library \
			build/swvk_main.o build/swvk_instance.o \
			build/swvk_device.o build/swvk_memory.o \
			build/swvk_buffer.o build/swvk_pipeline.o \
			build/swvk_spirv.o build/swvk_cmdbuf.o \
			build/swvk_draw.o build/swvk_sync.o \
			build/swvk_wsi.o build/swvk_descriptor.o \
		'

clean:
	rm -f ../build/swvk_*.o ../build/software_vk.library


