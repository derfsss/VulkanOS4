# Vulkan Textured Quad Example -- AmigaOS 4

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/../..:/work" -w /work $(DOCKER_IMAGE)

TARGET = ../../build/11_textured

.PHONY: all clean

all: $(TARGET)

$(TARGET):
	$(DOCKER_RUN) sh -c ' \
		SDK=/opt/ppc-amigaos/ppc-amigaos/SDK && \
		cp -r include/vulkan $$SDK/include/include_h/ && \
		cp include/interfaces/vulkan.h $$SDK/include/include_h/interfaces/ && \
		cp include/inline4/vulkan.h $$SDK/include/include_h/inline4/ && \
		cp include/proto/vulkan.h $$SDK/include/include_h/proto/ && \
		cp include/clib/vulkan_protos.h $$SDK/include/include_h/clib/ && \
		cp build/libvulkan_loader.a $$SDK/newlib/lib/ && \
		mkdir -p build && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__USE_INLINE__ \
			-o build/11_textured \
			examples/11_textured/11_textured.c \
			-lvulkan_loader -lauto \
	'

clean:
	rm -f ../../build/11_textured
