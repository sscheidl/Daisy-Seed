.DEFAULT_GOAL := test
CXX ?= g++
FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-local-typedefs -Wno-unused-parameter
INCLUDES = -DTEST -I../../instruments/physical-modeling/torus -I../../instruments/physical-modeling/third_party
SOURCES = test_fx.cpp ../../effects/reverb/src/ambient_reverb.cpp ../../effects/reverb/src/wet_path.cpp ../../instruments/physical-modeling/third_party/stmlib/utils/random.cc
.PHONY: test test-sanitize
test:
	mkdir -p build
	$(CXX) $(FLAGS) $(INCLUDES) $(SOURCES) -o build/test_fx
	./build/test_fx
test-sanitize:
	$(MAKE) test FLAGS='-std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-local-typedefs -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
