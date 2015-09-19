
# Copyright (c) 2012-2015, Jyri J. Virkki
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
#   make perf_report    (Linux only) generate perf reports (see README.perf)
#

BLOOM_VERSION=1.2

TOP := $(shell /bin/pwd)
BUILD_OS := $(shell uname)

BUILD=$(TOP)/build
INC=-I$(TOP) -I$(TOP)/murmur2
LIB=-lm
CC=gcc -Wall ${OPT} ${MM} -std=c99 -fPIC -D_GNU_SOURCE -DBLOOM_VERSION=$(BLOOM_VERSION)

#
# Defines used by the perf_test target (Linux-specific)
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
LINUX_BO=$(BUILD)/linux.o
LINUX_O=linux.o
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


all: $(BUILD)/libbloom.$(SO) $(BUILD)/test-libbloom

$(BUILD)/libbloom.$(SO): $(BUILD)/murmurhash2.o $(BUILD)/bloom.o $(LINUX_BO)
	(cd $(BUILD) && $(CC) bloom.o murmurhash2.o $(LINUX_O) -shared $(LIB) $(MAC) -o libbloom.$(SO))

$(BUILD)/test-libbloom: $(BUILD)/libbloom.$(SO) $(BUILD)/test.o
	(cd $(BUILD) && $(CC) test.o -L$(BUILD) $(RPATH) -lbloom -o test-libbloom)

$(BUILD)/%.o: %.c
	mkdir -p $(BUILD)
	$(CC) $(INC) -c $< -o $@

$(BUILD)/murmurhash2.o: murmur2/MurmurHash2.c murmur2/murmurhash2.h
	mkdir -p $(BUILD)
	$(CC) $(INC) -c murmur2/MurmurHash2.c -o $(BUILD)/murmurhash2.o

clean:
	rm -rf $(BUILD)

lint:
	lint -x -errfmt=simple $(INC) $(LIB) *.c murmur2/*.c

test: $(BUILD)/test-libbloom
	$(BUILD)/test-libbloom

ifeq ($(BUILD_OS),Linux)
.PHONY: perf_report
perf_report: $(BUILD)/test-libbloom
	mkdir -p $(PERF_TEST_DIR_CPU)
	perf stat --log-fd 1 $(BUILD)/test-libbloom -p  5000000  5000000 | tee $(PERF_TEST_DIR_CPU)/test_1.log
	perf stat --log-fd 1 $(BUILD)/test-libbloom -p 10000000 10000000 | tee $(PERF_TEST_DIR_CPU)/test_2.log
	perf stat --log-fd 1 $(BUILD)/test-libbloom -p 50000000 50000000 | tee $(PERF_TEST_DIR_CPU)/test_3.log
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
