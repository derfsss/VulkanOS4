# MyApp -- Vulkan Application for AmigaOS 4
# Copy this template and modify to build your own Vulkan program.
#
# Requirements:
#   - VulkanOS4 SDK installed (headers + libvulkan_loader.a)
#   - Docker with walkero/amigagccondocker:os4-gcc11
#
# Targets:
#   make        - Build the application
#   make clean  - Remove build artifacts

CC     = ppc-amigaos-gcc
CFLAGS = -mcrt=newlib -O2 -Wall -D__USE_INLINE__
LIBS   = -lvulkan_loader -lauto

TARGET = myapp
SOURCE = myapp.c

# If cross-compiling via Docker:
DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$$(pwd):/work" -w /work $(DOCKER_IMAGE)

.PHONY: all docker clean

# Native compile (on AmigaOS)
all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

# Cross-compile via Docker (from Linux/Mac/Windows)
docker:
	$(DOCKER_RUN) $(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	-delete $(TARGET) QUIET
