# VulkanOS4 - Top-level Makefile
# Builds all components via Docker cross-compilation
#
# Targets:
#   make test-build   - Verify Docker cross-compilation works
#   make sdk          - Generate SDK headers and link library
#   make loader       - Build vulkan.library
#   make icd          - Build software_vk.library
#   make gpu-icd      - Build ogles2_vk.library (GPU ICD)
#   make examples     - Build example programs
#   make tools        - Build tools (vulkaninfo, VulkanPrefs, AmigaMark)
#   make tests        - Build and run unit tests
#   make all          - Build everything
#   make dist         - Create distribution for AmigaOS 4
#   make dist-lha     - Create LHA archive
#   make clean        - Remove all build artifacts
#   make help         - Show this help

DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_RUN   = docker run --rm -v "$(shell pwd):/work" -w /work $(DOCKER_IMAGE)

# Compiler (inside Docker container)
CC       = ppc-amigaos-gcc
CFLAGS   = -mcrt=newlib -O2 -Wall
LDFLAGS  = -mcrt=newlib
LIBS     = -lauto

.PHONY: all test-build sdk loader icd examples tests dist clean help gpu-test check

all: sdk loader icd gpu-icd examples tools

# Verify Docker cross-compilation works
test-build:
	@echo "=== Testing Docker cross-compilation ==="
	@docker --version >/dev/null 2>&1 || \
		(echo "ERROR: Docker not found" && exit 1)
	@echo "Docker available."
	@mkdir -p build
	$(DOCKER_RUN) sh -c 'echo "int main(){return 0;}" > /tmp/t.c && ppc-amigaos-gcc -mcrt=newlib -o /tmp/t /tmp/t.c -lauto && echo "Cross-compiler works."'

# SDK headers (already hand-written in include/)
sdk:
	@echo "=== SDK headers (hand-written, nothing to build) ==="

# Loader + link library
loader:
	@echo "=== Building vulkan.library + libvulkan_loader.a ==="
	$(MAKE) -C loader all

# Software ICD
icd:
	@echo "=== Building software_vk.library ==="
	$(MAKE) -C software_icd all

# GPU ICD (OGLES2 backend)
gpu-icd:
	@echo "=== Building ogles2_vk.library ==="
	$(MAKE) -C ogles2_icd all

# Examples
examples: loader
	@echo "=== Building examples ==="
	$(MAKE) -C examples/01_enumerate all
	$(MAKE) -C examples/02_clear all
	$(MAKE) -C examples/03_gradient all
	$(MAKE) -C examples/04_checkerboard all
	$(MAKE) -C examples/05_plasma all
	$(MAKE) -C examples/06_rings all
	$(MAKE) -C examples/07_waves all
	$(MAKE) -C examples/08_triangle all
	$(MAKE) -C examples/09_rotating all
	$(MAKE) -C examples/10_depth all
	$(MAKE) -C examples/11_textured all
	$(MAKE) -C examples/12_wireframe_cube all
	$(MAKE) -C examples/13_solid_cube all
	$(MAKE) -C examples/14_instanced_cubes all
	$(MAKE) -C examples/15_render_pass all
	$(MAKE) -C examples/16_transfer_ops all
	$(MAKE) -C examples/17_events_sync all
	$(MAKE) -C examples/18_query_demo all
	$(MAKE) -C examples/19_indirect_draw all
	$(MAKE) -C examples/20_torus all
	$(MAKE) -C examples/21_image_texture all
	$(MAKE) -C examples/22_gltf_viewer all

# Tools
tools: loader
	@echo "=== Building tools ==="
	$(MAKE) -C tools/vulkaninfo all
	$(MAKE) -C tools/VulkanPrefs all
	$(MAKE) -C tools/AmigaMark all

# Tests
tests: gpu-icd
	@echo "=== Tests ==="
	$(MAKE) -C tests/w3dnova all

# Create distribution archive with all files needed to run on AmigaOS 4
dist:
	@echo "=== Creating distribution ==="
	rm -rf dist/
	mkdir -p dist/VulkanOS4
	mkdir -p dist/VulkanOS4/Libs
	mkdir -p dist/VulkanOS4/Libs/Vulkan
	mkdir -p dist/VulkanOS4/Devs/Vulkan/icd.d
	mkdir -p dist/VulkanOS4/SDK/include/include_h/vulkan
	mkdir -p dist/VulkanOS4/SDK/include/include_h/interfaces
	mkdir -p dist/VulkanOS4/SDK/include/include_h/inline4
	mkdir -p dist/VulkanOS4/SDK/include/include_h/proto
	mkdir -p dist/VulkanOS4/SDK/include/include_h/clib
	mkdir -p dist/VulkanOS4/SDK/newlib/lib
	mkdir -p dist/VulkanOS4/Examples/08_triangle
	mkdir -p dist/VulkanOS4/Examples/01_enumerate
	mkdir -p dist/VulkanOS4/Examples/02_clear
	mkdir -p dist/VulkanOS4/Examples/03_gradient
	mkdir -p dist/VulkanOS4/Examples/04_checkerboard
	mkdir -p dist/VulkanOS4/Examples/07_waves
	mkdir -p dist/VulkanOS4/Examples/05_plasma
	mkdir -p dist/VulkanOS4/Examples/06_rings
	mkdir -p dist/VulkanOS4/Examples/09_rotating
	mkdir -p dist/VulkanOS4/Examples/10_depth
	mkdir -p dist/VulkanOS4/Examples/11_textured
	mkdir -p dist/VulkanOS4/Examples/12_wireframe_cube
	mkdir -p dist/VulkanOS4/Examples/13_solid_cube
	mkdir -p dist/VulkanOS4/Examples/14_instanced_cubes
	mkdir -p dist/VulkanOS4/Examples/15_render_pass
	mkdir -p dist/VulkanOS4/Examples/18_query_demo
	mkdir -p dist/VulkanOS4/Examples/19_indirect_draw
	mkdir -p dist/VulkanOS4/Examples/16_transfer_ops
	mkdir -p dist/VulkanOS4/Examples/17_events_sync
	mkdir -p dist/VulkanOS4/Tools
	mkdir -p dist/VulkanOS4/Tests
	mkdir -p dist/VulkanOS4/Docs
	mkdir -p dist/VulkanOS4/Template
	@# Runtime libraries
	cp build/vulkan.library           dist/VulkanOS4/Libs/
	cp build/software_vk.library      dist/VulkanOS4/Libs/Vulkan/
	-cp build/ogles2_vk.library      dist/VulkanOS4/Libs/Vulkan/ 2>/dev/null || true
	@# ICD manifests
	cp software_icd/software_vk.json  dist/VulkanOS4/Devs/Vulkan/icd.d/
	cp ogles2_icd/ogles2_vk.json    dist/VulkanOS4/Devs/Vulkan/icd.d/
	@# SDK headers
	cp -r include/vulkan/*            dist/VulkanOS4/SDK/include/include_h/vulkan/
	cp include/interfaces/vulkan.h    dist/VulkanOS4/SDK/include/include_h/interfaces/
	cp include/inline4/vulkan.h       dist/VulkanOS4/SDK/include/include_h/inline4/
	cp include/proto/vulkan.h         dist/VulkanOS4/SDK/include/include_h/proto/
	cp include/clib/vulkan_protos.h   dist/VulkanOS4/SDK/include/include_h/clib/
	@# Link library
	cp build/libvulkan_loader.a       dist/VulkanOS4/SDK/newlib/lib/
	@# Examples (pre-built binaries + source)
	cp build/08_triangle                 dist/VulkanOS4/Examples/08_triangle/
	cp examples/08_triangle/08_triangle.c   dist/VulkanOS4/Examples/08_triangle/
	cp -r examples/08_triangle/shaders   dist/VulkanOS4/Examples/08_triangle/
	cp build/01_enumerate                dist/VulkanOS4/Examples/01_enumerate/
	cp examples/01_enumerate/01_enumerate.c dist/VulkanOS4/Examples/01_enumerate/
	cp build/02_clear                    dist/VulkanOS4/Examples/02_clear/
	cp examples/02_clear/02_clear.c         dist/VulkanOS4/Examples/02_clear/
	cp build/03_gradient                 dist/VulkanOS4/Examples/03_gradient/
	cp examples/03_gradient/03_gradient.c   dist/VulkanOS4/Examples/03_gradient/
	cp -r examples/03_gradient/shaders   dist/VulkanOS4/Examples/03_gradient/
	cp build/04_checkerboard             dist/VulkanOS4/Examples/04_checkerboard/
	cp examples/04_checkerboard/04_checkerboard.c dist/VulkanOS4/Examples/04_checkerboard/
	cp -r examples/04_checkerboard/shaders dist/VulkanOS4/Examples/04_checkerboard/
	cp build/07_waves                    dist/VulkanOS4/Examples/07_waves/
	cp examples/07_waves/07_waves.c         dist/VulkanOS4/Examples/07_waves/
	cp -r examples/07_waves/shaders      dist/VulkanOS4/Examples/07_waves/
	cp build/05_plasma                   dist/VulkanOS4/Examples/05_plasma/
	cp examples/05_plasma/05_plasma.c       dist/VulkanOS4/Examples/05_plasma/
	cp -r examples/05_plasma/shaders     dist/VulkanOS4/Examples/05_plasma/
	cp build/06_rings                    dist/VulkanOS4/Examples/06_rings/
	cp examples/06_rings/06_rings.c         dist/VulkanOS4/Examples/06_rings/
	cp -r examples/06_rings/shaders      dist/VulkanOS4/Examples/06_rings/
	cp build/09_rotating                 dist/VulkanOS4/Examples/09_rotating/
	cp examples/09_rotating/09_rotating.c   dist/VulkanOS4/Examples/09_rotating/
	cp -r examples/09_rotating/shaders   dist/VulkanOS4/Examples/09_rotating/
	cp build/10_depth                    dist/VulkanOS4/Examples/10_depth/
	cp examples/10_depth/10_depth.c         dist/VulkanOS4/Examples/10_depth/
	cp -r examples/10_depth/shaders      dist/VulkanOS4/Examples/10_depth/
	cp build/11_textured                 dist/VulkanOS4/Examples/11_textured/
	cp examples/11_textured/11_textured.c   dist/VulkanOS4/Examples/11_textured/
	cp -r examples/11_textured/shaders   dist/VulkanOS4/Examples/11_textured/
	cp build/12_wireframe_cube           dist/VulkanOS4/Examples/12_wireframe_cube/
	cp examples/12_wireframe_cube/12_wireframe_cube.c dist/VulkanOS4/Examples/12_wireframe_cube/
	cp -r examples/12_wireframe_cube/shaders dist/VulkanOS4/Examples/12_wireframe_cube/
	cp build/13_solid_cube               dist/VulkanOS4/Examples/13_solid_cube/
	cp examples/13_solid_cube/13_solid_cube.c dist/VulkanOS4/Examples/13_solid_cube/
	cp -r examples/13_solid_cube/shaders dist/VulkanOS4/Examples/13_solid_cube/
	cp build/14_instanced_cubes          dist/VulkanOS4/Examples/14_instanced_cubes/
	cp examples/14_instanced_cubes/14_instanced_cubes.c dist/VulkanOS4/Examples/14_instanced_cubes/
	cp -r examples/14_instanced_cubes/shaders dist/VulkanOS4/Examples/14_instanced_cubes/
	cp build/15_render_pass              dist/VulkanOS4/Examples/15_render_pass/
	cp examples/15_render_pass/15_render_pass.c dist/VulkanOS4/Examples/15_render_pass/
	cp build/18_query_demo               dist/VulkanOS4/Examples/18_query_demo/
	cp examples/18_query_demo/18_query_demo.c dist/VulkanOS4/Examples/18_query_demo/
	cp build/19_indirect_draw            dist/VulkanOS4/Examples/19_indirect_draw/
	cp examples/19_indirect_draw/19_indirect_draw.c dist/VulkanOS4/Examples/19_indirect_draw/
	cp build/16_transfer_ops             dist/VulkanOS4/Examples/16_transfer_ops/
	cp examples/16_transfer_ops/16_transfer_ops.c dist/VulkanOS4/Examples/16_transfer_ops/
	cp build/17_events_sync              dist/VulkanOS4/Examples/17_events_sync/
	cp examples/17_events_sync/17_events_sync.c dist/VulkanOS4/Examples/17_events_sync/
	mkdir -p dist/VulkanOS4/Examples/20_torus
	cp build/20_torus                    dist/VulkanOS4/Examples/20_torus/
	cp examples/20_torus/20_torus.c      dist/VulkanOS4/Examples/20_torus/
	cp -r examples/20_torus/shaders      dist/VulkanOS4/Examples/20_torus/
	mkdir -p dist/VulkanOS4/Examples/21_image_texture
	cp build/21_image_texture            dist/VulkanOS4/Examples/21_image_texture/
	cp examples/21_image_texture/21_image_texture.c dist/VulkanOS4/Examples/21_image_texture/
	cp -r examples/21_image_texture/shaders dist/VulkanOS4/Examples/21_image_texture/
	mkdir -p dist/VulkanOS4/Examples/22_gltf_viewer
	cp build/22_gltf_viewer              dist/VulkanOS4/Examples/22_gltf_viewer/
	cp examples/22_gltf_viewer/22_gltf_viewer.c dist/VulkanOS4/Examples/22_gltf_viewer/
	cp -r examples/22_gltf_viewer/shaders dist/VulkanOS4/Examples/22_gltf_viewer/
	@# Tools
	cp build/vulkaninfo               dist/VulkanOS4/Tools/
	cp tools/vulkaninfo/vulkaninfo.c  dist/VulkanOS4/Tools/
	cp build/VulkanPrefs              dist/VulkanOS4/Tools/
	cp tools/VulkanPrefs/VulkanPrefs.c dist/VulkanOS4/Tools/
	cp build/AmigaMark               dist/VulkanOS4/Tools/
	cp tools/AmigaMark/AmigaMark.c   dist/VulkanOS4/Tools/
	@# Documentation
	cp docs/vulkan_guide.txt          dist/VulkanOS4/Docs/
	cp docs/wsi_amiga.txt             dist/VulkanOS4/Docs/
	@# Project template
	cp template/myapp.c               dist/VulkanOS4/Template/
	cp template/Makefile              dist/VulkanOS4/Template/
	@# Tests (W3D Nova GPU tests)
	-cp build/test_w3dn_init          dist/VulkanOS4/Tests/ 2>/dev/null || true
	-cp build/test_w3dn_triangle      dist/VulkanOS4/Tests/ 2>/dev/null || true
	@# Install script
	cp Autoinstall                    dist/VulkanOS4/
	cp Autoinstall.info               dist/VulkanOS4/
	@echo "Distribution created in dist/VulkanOS4/"
	@echo "Contents:"
	@find dist/VulkanOS4 -type f | sort

# Create LHA archive from distribution
dist-lha: dist
	@echo "=== Creating LHA archive ==="
	$(DOCKER_RUN) sh -c 'cd dist && lha ao5q /work/dist/VulkanOS4.lha VulkanOS4'
	@ls -la dist/VulkanOS4.lha
	@echo "Archive created: dist/VulkanOS4.lha"

# Fast GPU iteration target (builds GPU ICD + 2 examples only)
gpu-test: loader gpu-icd
	@echo "=== GPU Test Build ==="
	$(MAKE) -C examples/02_clear all
	$(MAKE) -C examples/09_rotating all

# Basic validation (file existence + ELF headers)
check: all
	@echo "=== Validation Check ==="
	@echo "Checking library files..."
	@file build/vulkan.library | grep "ELF 32-bit MSB" || (echo "FAIL: vulkan.library" && exit 1)
	@file build/software_vk.library | grep "ELF 32-bit MSB" || (echo "FAIL: software_vk.library" && exit 1)
	@file build/ogles2_vk.library | grep "ELF 32-bit MSB" || (echo "FAIL: ogles2_vk.library" && exit 1)
	@echo "Checking example binaries..."
	@for ex in 01_enumerate 02_clear 03_gradient 04_checkerboard 05_plasma 06_rings 07_waves 08_triangle 09_rotating 10_depth 11_textured 12_wireframe_cube 13_solid_cube 14_instanced_cubes 15_render_pass 16_transfer_ops 17_events_sync 18_query_demo 19_indirect_draw 20_torus 21_image_texture 22_gltf_viewer; do \
		file build/$$ex | grep "ELF 32-bit MSB" > /dev/null || (echo "FAIL: $$ex" && exit 1); \
	done
	@echo "Checking tools..."
	@file build/vulkaninfo | grep "ELF 32-bit MSB" || (echo "FAIL: vulkaninfo" && exit 1)
	@file build/VulkanPrefs | grep "ELF 32-bit MSB" || (echo "FAIL: VulkanPrefs" && exit 1)
	@echo "All checks passed."

clean:
	rm -rf build/ dist/
	@echo "Clean complete."

help:
	@echo "VulkanOS4 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  test-build  - Verify Docker cross-compilation"
	@echo "  sdk         - Generate SDK headers and link library"
	@echo "  loader      - Build vulkan.library"
	@echo "  icd         - Build software_vk.library"
	@echo "  gpu-icd     - Build ogles2_vk.library (GPU ICD)"
	@echo "  examples    - Build example programs"
	@echo "  tools       - Build tools (vulkaninfo, VulkanPrefs, AmigaMark)"
	@echo "  tests       - Build and run unit tests"
	@echo "  all         - Build everything"
	@echo "  dist        - Create distribution for AmigaOS 4"
	@echo "  dist-lha    - Create LHA archive (dist/VulkanOS4.lha)"
	@echo "  check       - Validate build output (ELF headers)"
	@echo "  clean       - Remove all build artifacts"
	@echo ""
	@echo "Requirements:"
	@echo "  - Docker (walkero/amigagccondocker:os4-gcc11)"
