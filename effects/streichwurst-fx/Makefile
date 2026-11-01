# Streichwurst FX component: host tests only (no standalone firmware in this version; the
# component is embedded by the product instruments/streichwurst).
.DEFAULT_GOAL := test
.PHONY: test test-sanitize version
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
version:
	@sed -n 's/.*STREICHWURST_FX_VERSION "\(.*\)".*/\1/p' include/streichwurst_fx/version.h
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -D_USE_MATH_DEFINES -Iinclude tests/test_fx.cpp -o build/host/test_fx
	./build/host/test_fx
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
