
# Copyright (c) 2012, Jyri J. Virkki
# All rights reserved.
#
# This file is under BSD license. See LICENSE file.
#
# By default, builds optimized 32bit libbloom (under ./build)
# Requires GNU Make, so invoke appropriately (make or gmake)
#
# Other build options:
#
#   DEBUG=1 make        to build debug instead of optimized
#   MM=-m64 make        to build 64bit library
#
# Other build targets:
#
#   make test           to build and run test code
#   make gcov           to build with code coverage and run gcov
#   make lint           to run lint
#   make clean          the usual
#

TOP := $(shell /bin/pwd)
BUILD_OS := $(shell uname)

BUILD=$(TOP)/build
INC=-I$(TOP) -I$(TOP)/murmur2
LIB=-lm
CC=gcc -Wall ${OPT} ${MM} -std=c99 -fPIC

ifeq ($(MM),)
MM=-m32
endif

ifeq ($(BUILD_OS),Linux)
RPATH=-Wl,-rpath,$(BUILD)
SO=so
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

$(BUILD)/libbloom.$(SO): $(BUILD)/murmurhash2.o $(BUILD)/bloom.o
	(cd $(BUILD) && $(CC) bloom.o murmurhash2.o -shared $(LIB) $(MAC) -o libbloom.$(SO))

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

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" $(MAKE) all
	(cd $(BUILD) && \
		cp ../*.c . && \
		./test-libbloom && \
		gcov -bf bloom.c)
	@echo Remember to make clean to remove instrumented objects
