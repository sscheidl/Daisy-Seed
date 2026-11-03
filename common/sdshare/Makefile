# Host tests of the shared SD management component. Board code (sd_management.cpp) is
# compiled by the firmwares that use it.
HOST_CXX ?= g++
HOST_FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror
B = build/host
READER = ../sd/reader
DDP = ../../diagnostics/protocol
RING = ../../diagnostics/firmware/src
.PHONY: test test-sanitize clean
test:
	mkdir -p $(B)
	$(HOST_CXX) $(HOST_FLAGS) -I. tests/test_sdshare.cpp -o $(B)/test_sdshare
	./$(B)/test_sdshare
	$(HOST_CXX) $(HOST_FLAGS) -I. -I$(READER) -I$(DDP) -I$(RING) tests/test_admin.cpp admin.cpp \
		$(READER)/card_reader.cpp $(DDP)/ddp.cpp -o $(B)/test_admin
	./$(B)/test_admin
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
clean:
	rm -rf build
