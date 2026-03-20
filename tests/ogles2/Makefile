# W3D Nova Standalone Tests
# These test the W3D Nova API directly (not through Vulkan) to verify
# GPU functionality on the X5000/RX560 before building the Vulkan ICD.

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/../..:/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall -D__USE_INLINE__
LDFLAGS  = -mcrt=newlib
LIBS     = -lauto

BUILD    = ../../build

TESTS    = $(BUILD)/test_w3dn_init \
           $(BUILD)/test_w3dn_triangle

.PHONY: all clean

all: $(TESTS)

$(BUILD)/test_w3dn_init:
	$(DOCKER_RUN) sh -c ' \
		SDK=/opt/ppc-amigaos/ppc-amigaos/SDK/include/include_h && \
		cp -r external/Warp3DNova/Warp3DNova $$SDK/ && \
		mkdir -p build && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall -D__USE_INLINE__ \
			-o build/test_w3dn_init \
			tests/w3dnova/test_w3dn_init.c \
			-lauto \
	'

$(BUILD)/test_w3dn_triangle:
	$(DOCKER_RUN) sh -c ' \
		SDK=/opt/ppc-amigaos/ppc-amigaos/SDK/include/include_h && \
		cp -r external/Warp3DNova/Warp3DNova $$SDK/ && \
		mkdir -p build && \
		ppc-amigaos-gcc -mcrt=newlib -O2 -Wall -D__USE_INLINE__ \
			-Itests/w3dnova \
			-o build/test_w3dn_triangle \
			tests/w3dnova/test_w3dn_triangle.c \
			-lauto \
	'

clean:
	rm -f $(BUILD)/test_w3dn_init $(BUILD)/test_w3dn_triangle
