
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
#   make gcov           to build with code coverage and run gcov
#   make lint           to run lint
#   make clean          the usual
#   make perf_report    generate perf reports (see README.perf)
#

BLOOM_VERSION=1.3dev

TOP := $(shell /bin/pwd)
BUILD_OS := $(shell uname)

BUILD=$(TOP)/build
INC=-I$(TOP) -I$(TOP)/murmur2
LIB=-lm
CC=gcc -Wall ${OPT} ${MM} -std=c99 -fPIC -D_GNU_SOURCE -DBLOOM_VERSION=$(BLOOM_VERSION)

#
# Defines used by the perf_test target
#
HEAD=$(shell git log -1 --format="%h_%f")
ifndef HOSTNAME
HOSTNAME=$(shell hostname)
endif
PERF_TEST_DIR=$(TOP)/perf_reports
PERF_TEST_DIR_HEAD=$(PERF_TEST_DIR)/$(HEAD)
PERF_TEST_DIR_CPU=$(PERF_TEST_DIR_HEAD)/$(HOSTNAME)_$(CPU_ID)
CPU_ID=$(shell $(PERF_TEST_DIR)/cpu_id)


ifeq ($(MM),)
MM=-m64
endif

ifeq ($(BUILD_OS),Linux)
RPATH=-Wl,-rpath,$(BUILD)
SO=so
PERF_STAT=perf stat --log-fd 1
endif

ifeq ($(BUILD_OS),SunOS)
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
	    $(CC) bloom.o murmurhash2.o -shared $(LIB) $(MAC) \
	    -o libbloom.$(SO))

$(BUILD)/libbloom.a: $(BUILD)/murmurhash2.o $(BUILD)/bloom.o
	(cd $(BUILD) && ar rcs libbloom.a bloom.o murmurhash2.o)

$(BUILD)/test-libbloom: $(BUILD)/libbloom.$(SO) $(BUILD)/test.o
	(cd $(BUILD) && $(CC) test.o -L$(BUILD) $(RPATH) -lbloom -o test-libbloom)

$(BUILD)/test-basic: misc/test/basic.c $(BUILD)/libbloom.a
	$(CC) -I$(TOP) $(LIB) \
	    misc/test/basic.c $(BUILD)/libbloom.a -o $(BUILD)/test-basic

$(BUILD)/%.o: %.c
	mkdir -p $(BUILD)
	$(CC) $(INC) -c $< -o $@

$(BUILD)/murmurhash2.o: murmur2/MurmurHash2.c murmur2/murmurhash2.h
	mkdir -p $(BUILD)
	$(CC) $(INC) -c murmur2/MurmurHash2.c -o $(BUILD)/murmurhash2.o

clean:
	rm -rf $(BUILD)

lint:
	lint -x -errfmt=simple $(INC) $(LIB) bloom.c

test: $(BUILD)/test-libbloom $(BUILD)/test-basic
	$(BUILD)/test-basic
	$(BUILD)/test-libbloom

.PHONY: perf_report
perf_report: $(BUILD)/test-libbloom
	mkdir -p $(PERF_TEST_DIR_CPU)
	$(PERF_STAT) $(BUILD)/test-libbloom -p  5000000  5000000 | tee $(PERF_TEST_DIR_CPU)/test_1.log
	$(PERF_STAT) $(BUILD)/test-libbloom -p 10000000 10000000 | tee $(PERF_TEST_DIR_CPU)/test_2.log
	$(PERF_STAT) $(BUILD)/test-libbloom -p 50000000 50000000 | tee $(PERF_TEST_DIR_CPU)/test_3.log
ifeq ($(BUILD_OS),Linux)
	lscpu > ${PERF_TEST_DIR_CPU}/lscpu.log
	inxi -Cm -c0 > ${PERF_TEST_DIR_CPU}/inxi.log 2>/dev/null || inxi -C -c0 > ${PERF_TEST_DIR_CPU}/inxi.log
endif

vtest: $(BUILD)/test-libbloom
	valgrind --tool=memcheck --leak-check=full $(BUILD)/test-libbloom

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" $(MAKE) all
	(cd $(BUILD) && \
		cp ../*.c . && \
		./test-libbloom && \
		gcov -bf bloom.c)
	@echo Remember to make clean to remove instrumented objects

#
# This target runs a test which creates a filter of capacity N and inserts
# N elements, for N in 100,000 to 1,000,000 with an expected error of 0.001.
# To preserve and graph the output, move it to ./data/collisions and use
# the ./data/collisions/dograph script to plot it.
#
# WARNING: This can take a very long time (on a slow machine, multiple days)
# to run.
#
collision_test: $(BUILD)/test-libbloom
	$(BUILD)/test-libbloom -G 100000 1000000 10 0.001 | tee collision_data_v$(BLOOM_VERSION)
