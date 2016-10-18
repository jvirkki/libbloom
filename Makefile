#
# Copyright (c) 2012-2016, Jyri J. Virkki
# All rights reserved.
#
# This file is under BSD license. See LICENSE file.
#
# By default, builds optimized 64bit libbloom (under ./build)
# Requires GNU Make, so invoke appropriately (make or gmake)
#
# Other build options:
#
#   DEBUG=1 make        to build debug instead of optimized
#   MM=-m32 make        to build 32bit library
#
# Other build targets:
#
#   make test           to build and run test code
#   make release_test   to build and run larger tests
#   make gcov           to build with code coverage and run gcov
#   make clean          the usual
#

BLOOM_VERSION=1.4dev

TOP := $(shell /bin/pwd)
BUILD_OS := $(shell uname)

BUILD=$(TOP)/build
INC=-I$(TOP) -I$(TOP)/murmur2
LIB=-lm
COM=${CC} -Wall ${OPT} ${MM} -std=c99 -fPIC -DBLOOM_VERSION=$(BLOOM_VERSION)
TESTDIR=$(TOP)/misc/test

ifeq ($(MM),)
MM=-m64
endif

ifeq ($(BUILD_OS),Linux)
RPATH=-Wl,-rpath,$(BUILD)
SO=so
endif

ifeq ($(BUILD_OS),SunOS)
RPATH=-R$(BUILD)
SO=so
CC=gcc
endif

ifeq ($(BUILD_OS),OpenBSD)
RPATH=-R$(BUILD)
SO=so
endif

ifeq ($(BUILD_OS),Darwin)
MAC=-install_name $(BUILD)/libbloom.dylib
RPATH=-Xlinker -rpath -Xlinker $(BUILD)
SO=dylib
endif

ifeq ($(DEBUG),1)
OPT=-g $(DEBUGOPT)
else
OPT=-O3
endif


all: $(BUILD)/libbloom.$(SO) $(BUILD)/libbloom.a

$(BUILD)/libbloom.$(SO): $(BUILD)/murmurhash2.o $(BUILD)/bloom.o
	(cd $(BUILD) && \
	    $(COM) bloom.o murmurhash2.o -shared $(LIB) $(MAC) \
	    -o libbloom.$(SO))

$(BUILD)/libbloom.a: $(BUILD)/murmurhash2.o $(BUILD)/bloom.o
	(cd $(BUILD) && ar rcs libbloom.a bloom.o murmurhash2.o)

$(BUILD)/test-libbloom: $(TESTDIR)/test.c $(BUILD)/libbloom.$(SO)
	$(COM) -I$(TOP) -c $(TESTDIR)/test.c -o $(BUILD)/test.o
	(cd $(BUILD) && \
	    $(COM) test.o -L$(BUILD) $(RPATH) -lbloom -o test-libbloom)

$(BUILD)/test-basic: $(TESTDIR)/basic.c $(BUILD)/libbloom.a
	$(COM) -I$(TOP) $(LIB) \
	    $(TESTDIR)/basic.c $(BUILD)/libbloom.a -o $(BUILD)/test-basic

$(BUILD)/%.o: %.c
	mkdir -p $(BUILD)
	$(COM) $(INC) -c $< -o $@

$(BUILD)/murmurhash2.o: murmur2/MurmurHash2.c murmur2/murmurhash2.h
	mkdir -p $(BUILD)
	$(COM) $(INC) -c murmur2/MurmurHash2.c -o $(BUILD)/murmurhash2.o

clean:
	rm -rf $(BUILD)

test: $(BUILD)/test-libbloom $(BUILD)/test-basic
	$(BUILD)/test-basic
	$(BUILD)/test-libbloom

vtest: $(BUILD)/test-libbloom
	valgrind --tool=memcheck --leak-check=full --show-reachable=yes \
	    $(BUILD)/test-libbloom

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" \
	    $(MAKE) $(BUILD)/test-libbloom
	(cd $(BUILD) && \
	    cp ../*.c . && \
	    ./test-libbloom && \
	    gcov -bf bloom.c)
	@echo Remember to make clean to remove instrumented objects

#
# This target runs a test which creates a filter of capacity N and inserts
# N elements, for N in 100,000 to 1,000,000 with an expected error of 0.001.
# To preserve and graph the output, move it to ./misc/collisions and use
# the ./misc/collisions/dograph script to plot it.
#
# WARNING: This can take a very long time (on a slow machine, multiple days)
# to run.
#
collision_test: $(BUILD)/test-libbloom
	$(BUILD)/test-libbloom -G 100000 1000000 10 0.001 \
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
	$(BUILD)/test-libbloom -G 100000 1000000 50000 0.001 \
	    | tee short_coll_data
	gzip short_coll_data
	./misc/collisions/dograph short_coll_data.gz
