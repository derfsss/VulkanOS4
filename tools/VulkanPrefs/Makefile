# VulkanPrefs -- Vulkan ICD Preferences Tool

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/../..:/work" -w /work $(DOCKER_IMAGE)

TARGET = ../../build/VulkanPrefs

.PHONY: all clean

all: $(TARGET)

$(TARGET):
	$(DOCKER_RUN) sh -c ' \
		mkdir -p build && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall \
			-D__USE_INLINE__ \
			-o build/VulkanPrefs \
			tools/VulkanPrefs/VulkanPrefs.c \
			-lauto \
	'

clean:
	rm -f ../../build/VulkanPrefs
