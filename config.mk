# VulkanOS4 -- Shared build configuration
# Included by sub-Makefiles to avoid duplicating Docker/compiler settings.

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd):/work" -w /work $(DOCKER_IMAGE)

CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall
LDFLAGS  = -mcrt=newlib
LIBS     = -lauto

# SDK paths inside Docker container
SDK_BASE = /opt/ppc-amigaos/ppc-amigaos/SDK
SDK_INC  = $(SDK_BASE)/include/include_h
SDK_LIB  = $(SDK_BASE)/newlib/lib
