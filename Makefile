#
# Copyright (c) 2012-2022, Jyri J. Virkki
# All rights reserved.
#
# This file is under BSD license. See LICENSE file.
#
# Requires GNU Make, so invoke appropriately (make or gmake)
#
# Other build options:
#
#   DEBUG=1 make        to build debug instead of optimized
#
# Other build targets:
#
#   make test           to build and run test code
#   make release_test   to build and run larger tests
#   make gcov           to build with code coverage and run gcov
#   make clean          the usual
#

BLOOM_VERSION_MAJOR=2
BLOOM_VERSION_MINOR=0

#
# Shared library names - these definitions work on most platforms but can
# be overridden in the platform-specific sections below.
#
BLOOM_VERSION=$(BLOOM_VERSION_MAJOR).$(BLOOM_VERSION_MINOR)
BLOOM_SONAME=libbloom.so.$(BLOOM_VERSION_MAJOR)
SO_VERSIONED=libbloom.so.$(BLOOM_VERSION)
LD_SONAME=-Wl,-soname,$(BLOOM_SONAME)
SO=so


TOP := $(shell /bin/pwd)
BUILD_OS := $(shell uname)
BINDIR=$(TOP)/build
TESTDIR=$(TOP)/misc/test

INC+=-I$(TOP) -I$(TOP)/murmur2
LIB+=-lm
CFLAGS+=-Wall
CFLAGS+=-fPIC
CFLAGS+=-DBLOOM_VERSION=$(BLOOM_VERSION)
CFLAGS+=-DBLOOM_VERSION_MAJOR=$(BLOOM_VERSION_MAJOR)
CFLAGS+=-DBLOOM_VERSION_MINOR=$(BLOOM_VERSION_MINOR)


ifeq ($(DEBUG),1)
OPT=-g $(DEBUGOPT)
else
OPT?=-O3
endif


ifeq ($(BUILD_OS),$(filter $(BUILD_OS), GNU/kFreeBSD GNU Linux))
RPATH=-Wl,-rpath,$(BINDIR)
endif

ifeq ($(BUILD_OS),SunOS)
RPATH=-R$(BINDIR)
CC=gcc
endif

ifeq ($(BUILD_OS),OpenBSD)
RPATH=-R$(BINDIR)
endif

ifeq ($(BUILD_OS),Darwin)
MAC=-install_name $(BINDIR)/libbloom.dylib \
	-compatibility_version $(BLOOM_VERSION_MAJOR) \
	-current_version $(BLOOM_VERSION)
RPATH=-Xlinker -rpath -Xlinker $(BINDIR)
SO=dylib
BLOOM_SONAME=libbloom.$(BLOOM_VERSION_MAJOR).$(SO)
SO_VERSIONED=libbloom.$(BLOOM_VERSION).$(SO)
LD_SONAME=
endif



all: $(BINDIR)/$(SO_VERSIONED) $(BINDIR)/libbloom.a

$(BINDIR)/$(SO_VERSIONED): $(BINDIR)/murmurhash2.o $(BINDIR)/bloom.o
	(cd $(BINDIR) && \
	    $(CC) $(OPT) $(LDFLAGS) bloom.o murmurhash2.o -shared \
	    $(LIB) $(MAC) $(LD_SONAME) -o $(SO_VERSIONED) && \
	rm -f $(BLOOM_SONAME) && \
	ln -s $(SO_VERSIONED) $(BLOOM_SONAME) && \
	rm -f libbloom.$(SO) && \
	ln -s $(BLOOM_SONAME) libbloom.$(SO) )

$(BINDIR)/libbloom.a: $(BINDIR)/murmurhash2.o $(BINDIR)/bloom.o
	(cd $(BINDIR) && ar rcs libbloom.a bloom.o murmurhash2.o)

$(BINDIR)/test-libbloom: $(TESTDIR)/test.c $(BINDIR)/$(SO_VERSIONED)
	$(CC) $(CFLAGS) $(OPT) $(INC) -c $(TESTDIR)/test.c -o \
	    $(BINDIR)/test.o
	(cd $(BINDIR) && \
	$(CC) $(CFLAGS) $(OPT) -L$(BINDIR) $(RPATH) test.o \
	    -lbloom -o test-libbloom)

$(BINDIR)/test-perf: $(TESTDIR)/perf.c $(BINDIR)/$(SO_VERSIONED)
	$(CC) $(CFLAGS) $(OPT) $(INC) -c $(TESTDIR)/perf.c -o $(BINDIR)/perf.o
	(cd $(BINDIR) && \
	    $(CC) perf.o -L$(BINDIR) $(RPATH) -lbloom -o test-perf)

$(BINDIR)/test-basic: $(TESTDIR)/basic.c $(BINDIR)/libbloom.a
	$(CC) $(CFLAGS) $(OPT) $(INC) $(TESTDIR)/basic.c \
	    $(BINDIR)/libbloom.a $(LIB) -o $(BINDIR)/test-basic

$(BINDIR)/visualize: $(TESTDIR)/visualize.c $(BINDIR)/libbloom.a
	$(CC) $(CFLAGS) $(OPT) $(INC) $(TESTDIR)/visualize.c \
	    $(BINDIR)/libbloom.a $(LIB) -lgd -o $(BINDIR)/visualize

$(BINDIR)/%.o: %.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(OPT) $(INC) -c $< -o $@

$(BINDIR)/murmurhash2.o: murmur2/MurmurHash2.c murmur2/murmurhash2.h
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(OPT) $(INC) \
	    -c murmur2/MurmurHash2.c -o $(BINDIR)/murmurhash2.o

clean:
	rm -rf $(BINDIR)

test: $(BINDIR)/test-libbloom $(BINDIR)/test-basic
	$(BINDIR)/test-basic
	$(BINDIR)/test-libbloom

perf: $(BINDIR)/test-perf
	$(BINDIR)/test-perf

#
# Builds the visualize program into $BINDIR
# This is not built by default as it requires the GD library
# (on Debian: libgd3 and libgd-dev) to be present.
#
visualize: $(BINDIR)/visualize

vtest: $(BINDIR)/test-libbloom
	valgrind --tool=memcheck --leak-check=full --show-reachable=yes \
	    --track-origins=yes $(BINDIR)/test-libbloom

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" \
	    $(MAKE) $(BINDIR)/test-libbloom
	(cd $(BINDIR) && \
	    cp ../*.c . && \
	    ./test-libbloom && \
	    gcov -bf bloom.c)
	@echo Remember to make clean to remove instrumented objects

lcov: gcov
	lcov --capture --directory build --output-file lcov.info
	lcov --remove lcov.info MurmurHash2.c --output-file lcov.info
	lcov --remove lcov.info test.c --output-file lcov.info
	genhtml lcov.info \
		--output-directory $(LCOV_OUTPUT_DIR)
	rm -f lcov.info
	$(MAKE) clean

#
# This target runs a test which creates a filter of capacity N and inserts
# N elements, for N in 100,000 to 1,000,000 with an expected error of 0.001.
# To preserve and graph the output, move it to ./misc/collisions and use
# the ./misc/collisions/dograph script to plot it.
#
# WARNING: This can take a very long time (on a slow machine, multiple days)
# to run.
#
collision_test: $(BINDIR)/test-libbloom
	$(BINDIR)/test-libbloom -G 100000 1000000 10 0.001 \
	    | tee collision_data_v$(BLOOM_VERSION)

#
# This target should be run when preparing a release, includes more tests
# than the 'test' target.
# For a final release, should run the collision_test target above as well,
# not included here as it takes so long.
#
release_test:
	$(MAKE) test
	$(MAKE) vtest
	$(BINDIR)/test-libbloom -G 100000 1000000 50000 0.001 \
	    | tee short_coll_data
	gzip short_coll_data
	./misc/collisions/dograph short_coll_data.gz
