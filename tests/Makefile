# Host unit tests for the RngBuffer library
# Usage: make -C tests [CC=clang] [CXX=clang++]
#
# Variants of test_rngbuffer.c:
#   c99, c11        fake lock, AddressSanitizer and UndefinedBehaviorSanitizer
#   c99_O2          fake lock, -O2, no sanitizers
#   default_lock    the library's built-in weak lock hooks
#   custom_timeout  RNG_BUFFER_LOCK_TIMEOUT set by the build
# plus cpp (test_rngbuffer_cpp.cpp: lock hooks defined in C++, coexistence with a class
# RingBuffer) and a check that library.json, library.properties and RngBuffer.h carry the
# same version.

OUT  = _build
SRC  = ../src
LIB  = $(SRC)/RngBuffer.c $(SRC)/RngBuffer.h
WARN = -Wall -Wextra -Wpedantic -Wshadow -Werror
SAN  = -fsanitize=address,undefined -fno-sanitize-recover=all
CBASE = -g $(WARN) -Wstrict-prototypes -I$(SRC)

BINS = $(OUT)/c99 $(OUT)/c11 $(OUT)/c99_O2 $(OUT)/default_lock $(OUT)/custom_timeout $(OUT)/cpp

test: version $(BINS)
	@for t in $(BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "HOST TESTS PASSED"

$(OUT):
	mkdir -p $(OUT)

$(OUT)/c99: test_rngbuffer.c $(LIB) | $(OUT)
	$(CC) -std=c99 -O1 $(CBASE) $(SAN) test_rngbuffer.c $(SRC)/RngBuffer.c -o $@

$(OUT)/c11: test_rngbuffer.c $(LIB) | $(OUT)
	$(CC) -std=c11 -O1 $(CBASE) $(SAN) test_rngbuffer.c $(SRC)/RngBuffer.c -o $@

$(OUT)/c99_O2: test_rngbuffer.c $(LIB) | $(OUT)
	$(CC) -std=c99 -O2 $(CBASE) test_rngbuffer.c $(SRC)/RngBuffer.c -o $@

$(OUT)/default_lock: test_rngbuffer.c $(LIB) | $(OUT)
	$(CC) -std=c99 -O1 $(CBASE) $(SAN) -DTEST_DEFAULT_LOCK test_rngbuffer.c $(SRC)/RngBuffer.c -o $@

$(OUT)/custom_timeout: test_rngbuffer.c $(LIB) | $(OUT)
	$(CC) -std=c99 -O1 $(CBASE) $(SAN) -DRNG_BUFFER_LOCK_TIMEOUT=7 -DTEST_EXPECTED_TIMEOUT=7 \
	    test_rngbuffer.c $(SRC)/RngBuffer.c -o $@

$(OUT)/RngBuffer_san.o: $(LIB) | $(OUT)
	$(CC) -std=c99 -O1 $(CBASE) $(SAN) -c $(SRC)/RngBuffer.c -o $@

$(OUT)/cpp: test_rngbuffer_cpp.cpp $(OUT)/RngBuffer_san.o | $(OUT)
	$(CXX) -std=c++11 -O1 -g $(WARN) $(SAN) -I$(SRC) test_rngbuffer_cpp.cpp $(OUT)/RngBuffer_san.o -o $@

version:
	@j=$$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' ../library.json); \
	 p=$$(sed -n 's/^version=//p' ../library.properties | tr -d '\r'); \
	 h=$$(sed -n 's/.*RNG_BUFFER_VERSION_STRING *"\([^"]*\)".*/\1/p' $(SRC)/RngBuffer.h); \
	 if [ -z "$$h" ] || [ "$$j" != "$$h" ] || [ "$$p" != "$$h" ]; then \
	     echo "FAIL version: library.json '$$j', library.properties '$$p', RngBuffer.h '$$h'"; exit 1; fi; \
	 echo "== version $$h (library.json, library.properties and RngBuffer.h agree)"

clean:
	rm -rf $(OUT)

.PHONY: test version clean
