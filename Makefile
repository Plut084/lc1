.PHONY: install-debug-deps build-debug run-debug

install-debug-deps:
	conan install . -pr:h profiles/linux -pr:b default --build=missing -s build_type=Debug

build-debug: install-debug-deps
	@echo "Building debug..."
	@cmake --build --preset=conan-debug

run-debug: build-debug
	@echo "Running in debug mode..."
	@source ./build/Debug/generators/conanrunenv-debug-x86_64.sh && cd ./build/Debug/ && ./lc1
