BUILD_DIR := build
VCPKG_ROOT ?= $(HOME)/code/sindarin/sindarin-pkg-libs/vcpkg

CMAKE_FLAGS := -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake \
               -DCMAKE_BUILD_TYPE=Debug

.PHONY: build test clean deps

deps:
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Error: vcpkg not found at $(VCPKG_ROOT)"; \
		echo "Set VCPKG_ROOT to your vcpkg installation"; \
		exit 1; \
	fi
	$(VCPKG_ROOT)/vcpkg install

build: deps
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
