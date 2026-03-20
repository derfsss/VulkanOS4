# VulkanOS4 Unit Tests
# Tests are added per-phase as components are implemented

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd)/..:/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall
LDFLAGS  = -mcrt=newlib
LIBS     = -lauto

.PHONY: all clean

all:
	@echo "=== Unit Tests ==="
	@echo "(No tests implemented yet -- tests added per-phase)"

clean:
	rm -f ../build/test_*
