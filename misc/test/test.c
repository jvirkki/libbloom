/*
 *  Copyright (c) 2012-2022, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "bloom.h"

#ifdef __linux
#include <sys/time.h>
#include <time.h>
#endif



/** ***************************************************************************
 * Sanity check bits & bytes
 *
 */
static void bits()
{
  struct bloom bloom;
  unsigned int entries;
  unsigned long long int bytes, bits, prevbytes = 0;

  printf("----- bits and bytes sanity tests -----\n");

  for (entries = UINT_MAX; entries > 1000; entries = entries / 2) {
    assert(bloom_init2(&bloom, entries, 0.01) == 0);

    bytes = bloom.bytes;
    bits = bloom.bits;
    bloom_free(&bloom);

    printf("entries = %10u (bytes = %12llu, bits = %12llu)\n",
           entries, bytes, bits);

    if (prevbytes > 0) {
      assert(bytes < prevbytes);
    }
    prevbytes = bytes;
  }
}


/** ***************************************************************************
 * Test bloom_merge operation.
 *
 */
static void merge_test(unsigned int entries, double error, int count)
{
  struct bloom bloom_dest;
  struct bloom bloom_src;

  printf("----- bloom_merge tests -----\n");

  printf("Testing invalid filter combinations for merge\n");

  assert(bloom_init2(&bloom_dest, entries, error) == 0);
  assert(bloom_init2(&bloom_src, entries - 1, error) == 0);
  assert(bloom_merge(&bloom_dest, &bloom_src) == 1);
  bloom_free(&bloom_dest);
  bloom_free(&bloom_src);

  assert(bloom_init2(&bloom_dest, entries, error) == 0);
  assert(bloom_init2(&bloom_src, entries, error / 2) == 0);
  assert(bloom_merge(&bloom_dest, &bloom_src) == 1);
  bloom_free(&bloom_dest);
  bloom_free(&bloom_src);

  assert(bloom_init2(&bloom_dest, entries, error) == 0);
  assert(bloom_merge(&bloom_dest, &bloom_src) == -1);
  assert(bloom_merge(&bloom_src, &bloom_dest) == -1);
  bloom_free(&bloom_dest);

  assert(bloom_init2(&bloom_dest, entries, error) == 0);
  assert(bloom_init2(&bloom_src, entries, error) == 0);
  bloom_dest.major = 99;
  assert(bloom_merge(&bloom_dest, &bloom_src) == 1);
  bloom_src.major = 99;
  bloom_src.minor = 99;
  assert(bloom_merge(&bloom_dest, &bloom_src) == 1);
  bloom_free(&bloom_dest);
  bloom_free(&bloom_src);

  printf("Merging two filters with %u entries, %f error, %d count\n",
         entries, error, count);

  assert(bloom_init2(&bloom_dest, entries, error) == 0);
  assert(bloom_init2(&bloom_src, entries, error) == 0);

  int collisions = 0;
  uint64_t n, initial;
  int fd = open("/dev/urandom", O_RDONLY);
  read(fd, &n, sizeof(uint64_t));
  close(fd);
  initial = n;

  // Populate bloom_src with `count` elements
  uint64_t c;
  for (c = 0; c < count; c++) {
    collisions += bloom_add(&bloom_src, &n, sizeof(uint64_t));
    n++;
  }
  printf("%d collisions adding to bloom_src\n", collisions);

  // Also populate bloom_dest with `count` elements
  collisions = 0;
  for (c = 0; c < count; c++) {
    bloom_add(&bloom_dest, &n, sizeof(uint64_t));
    n++;
  }
  printf("%d collisions adding to bloom_dest\n", collisions);

  assert(bloom_merge(&bloom_dest, &bloom_src) == 0);

  // Verify all elements now in bloom_dest
  int rv;
  n = initial;
  for (c = 0; c < count * 2; c++) {
    rv = bloom_check(&bloom_dest, &n, sizeof(uint64_t));
    assert(rv == 1);
    n++;
  }

  bloom_free(&bloom_dest);
  bloom_free(&bloom_src);
}


/** ***************************************************************************
 * Testing bloom_load with various failure cases.
 *
 */
static void load_tests()
{
  char * filename = "/tmp/libbloom.test";
  struct bloom bloom;
  struct bloom bloom2;
  uint64_t n;
  int fd;

  printf("----- bloom_load tests -----\n");

  memset(&bloom, 0, sizeof(struct bloom));
  memset(&bloom2, 0, sizeof(struct bloom));

  bloom_init2(&bloom, 1000000, 0.1);
  for (n = 1; n < 1000; n++) {
    bloom_add(&bloom, &n, sizeof(uint64_t));
  }

  // BLOOM_MAGIC too short
  bloom_save(&bloom, filename);
  truncate(filename, 4);
  assert(bloom_load(&bloom2, filename) == 4);

  // BLOOM_MAGIC incorrect
  fd = open(filename, O_WRONLY | O_CREAT, 0644);
  write(fd, "lobbliim3", 9);
  close(fd);
  assert(bloom_load(&bloom2, filename) == 5);

  // struct size not present
  bloom_save(&bloom, filename);
  truncate(filename, 10);
  assert(bloom_load(&bloom2, filename) == 6);

  // struct size incorrect
  fd = open(filename, O_WRONLY | O_CREAT, 0644);
  write(fd, "libbloom2", 9);
  uint16_t size = sizeof(struct bloom) - 2;
  write(fd, &size, sizeof(uint16_t));
  close(fd);
  assert(bloom_load(&bloom2, filename) == 7);

  // struct content too short
  bloom_save(&bloom, filename);
  truncate(filename, 18);
  assert(bloom_load(&bloom2, filename) == 8);

  // incompatible version
  bloom.major++;
  bloom_save(&bloom, filename);
  assert(bloom_load(&bloom2, filename) == 9);
  bloom.major--;

  // data buffer too short
  bloom_save(&bloom, filename);
  truncate(filename, 75);
  assert(bloom_load(&bloom2, filename) == 11);

  bloom_free(&bloom);
  unlink(filename);
}


/** ***************************************************************************
 * A few simple tests to check if it works at all.
 *
 */
static int basic()
{
  printf("----- basic -----\n");

  struct bloom bloom;

  assert(bloom_save(&bloom, NULL) == 1);
  assert(bloom_save(&bloom, "") == 1);
  assert(bloom_save(&bloom, "/no-such-directory/foo") == 1);

  assert(bloom_load(&bloom, NULL) == 1);
  assert(bloom_load(&bloom, "") == 1);
  assert(bloom_load(NULL, "hi") == 2);
  assert(bloom_load(&bloom, "/no-such-directory/foo") == 3);

  assert(bloom_init(&bloom, 5000, 1.0) == 1);
  assert(bloom_init(&bloom, 5000, 1.1) == 1);
  assert(bloom_init(&bloom, 5000, -1.0) == 1);
  assert(bloom_init(&bloom, 0, 1.0) == 1);
  assert(bloom_init(&bloom, 10, 0) == 1);
  assert(bloom_init(&bloom, 1001, 0) == 1);
  assert(bloom.ready == 0);
  assert(bloom_add(&bloom, "hello world", 11) == -1);
  assert(bloom_check(&bloom, "hello world", 11) == -1);
  bloom_print(&bloom);
  bloom_free(&bloom);
  assert(bloom_reset(&bloom) == 1);

  assert(bloom_init(&bloom, 1002, 0.1) == 0);
  assert(bloom.ready == 1);
  bloom_print(&bloom);

  assert(bloom_check(&bloom, "hello world", 11) == 0);
  assert(bloom_add(&bloom, "hello world", 11) == 0);
  assert(bloom_check(&bloom, "hello world", 11) == 1);
  assert(bloom_add(&bloom, "hello world", 11) > 0);
  assert(bloom_add(&bloom, "hello", 5) == 0);
  assert(bloom_add(&bloom, "hello", 5) > 0);
  assert(bloom_check(&bloom, "hello", 5) == 1);
  bloom_free(&bloom);

  load_tests();

  merge_test(100000, 0.001, 500);

  bits();

  struct bloom null_bloom = NULL_BLOOM_FILTER;
  bloom_print(&null_bloom);
  assert(bloom_reset(&null_bloom) == 1);
  bloom_free(&null_bloom);

  return 0;
}


/** ***************************************************************************
 * Create a bloom filter with given parameters and add 'count' random elements
 * into it to see if collision rates are within expectations.
 *
 */
static int add_random(unsigned int entries, double error, int count,
                      int quiet, int check_error, uint8_t elem_size, int validate)
{
  if (!quiet) {
    printf("----- add_random(%u, %f, %d, %d, %d, %d, %d) -----\n",
           entries, error, count, quiet, check_error, elem_size, validate);
  }

  struct bloom bloom;
  struct bloom bloom2;
  assert(bloom_init(&bloom, entries, error) == 0);
  if (!quiet) { bloom_print(&bloom); }
  assert(bloom_reset(&bloom) == 0);

  char block[elem_size];
  uint8_t * saved = NULL;
  uint8_t * savedp = NULL;
  int collisions = 0;
  int n;

  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    printf("error: unable to open /dev/random\n");
    exit(1);
  }

  if (validate) {
    saved = (uint8_t *)malloc(elem_size * count);
    if (!saved) {
      printf("error: unable to allocate buffer for validation\n");
      exit(1);
    }
    savedp = saved;
  }

  for (n = 0; n < count; n++) {
    assert(read(fd, block, elem_size) == elem_size);
    memcpy(savedp, block, elem_size);
    savedp += elem_size;
    if (bloom_add(&bloom, (void *)block, elem_size)) { collisions++; }
  }
  close(fd);

  double er = (double)collisions / (double)count;

  if (!quiet) {
    printf("entries: %u, error: %f, count: %d, coll: %d, error: %f, "
           "bytes: %lu\n",
           entries, error, count, collisions, er, bloom.bytes);
  } else {
    printf("%u %f %d %d %f %lu\n",
           entries, error, count, collisions, er, bloom.bytes);
  }

  if (check_error && er > error) {
    printf("error: expected error %f but observed %f\n", error, er);
    exit(1);
  }

  bloom_save(&bloom, "/tmp/bloom.test");
  bloom_load(&bloom2, "/tmp/bloom.test");

  if (validate) {
    for (n = 0; n < count; n++) {
      if (!bloom_check(&bloom2, saved + (n * elem_size), elem_size)) {
        printf("error: data saved in filter is not there!\n");
        exit(1);
      }
    }
  }

  bloom_free(&bloom);
  bloom_free(&bloom2);
  if (saved) { free(saved); }
  return 0;
}


/** ***************************************************************************
 * Simple loop to compare performance.
 *
 */
static int perf_loop(int entries, int count)
{
  printf("----- perf_loop -----\n");

  struct bloom bloom;
  assert(bloom_init(&bloom, entries, 0.001) == 0);
  bloom_print(&bloom);

  int i;
  int collisions = 0;

  struct timeval tp;
  gettimeofday(&tp, NULL);
  long before = (tp.tv_sec * 1000L) + (tp.tv_usec / 1000L);

  for (i = 0; i < count; i++) {
    if (bloom_add(&bloom, (void *)&i, sizeof(int))) { collisions++; }
  }

  gettimeofday(&tp, NULL);
  long after = (tp.tv_sec * 1000L) + (tp.tv_usec / 1000L);

  printf("Added %d elements of size %d, took %d ms (collisions=%d)\n",
         count, (int)sizeof(int), (int)(after - before), collisions);

  printf("%d,%lu,%ld\n", entries, bloom.bytes, after - before);

  bloom_print(&bloom);
  bloom_free(&bloom);

  return 0;
}


/** ***************************************************************************
 * Default set of basic tests.
 *
 * These should run reasonably quick so they can be run all the time.
 *
 */
static int basic_tests()
{
  int rv = 0;

  rv += basic();
  rv += add_random(5002, 0.01, 5000, 0, 1, 32, 1);
  rv += add_random(10000, 0.1, 10000, 0, 1, 32, 1);
  rv += add_random(10000, 0.01, 10000, 0, 1, 32, 1);
  rv += add_random(10000, 0.001, 10000, 0, 1, 32, 1);
  rv += add_random(10000, 0.0001, 10000, 0, 1, 32, 1);
  rv += add_random(1000000, 0.0001, 1000000, 0, 1, 32, 1);

  printf("\nBrought to you by libbloom-%s\n", bloom_version());

  return 0;
}


/** ***************************************************************************
 * Some longer-running tests.
 *
 */
static int larger_tests()
{
  int rv = 0;
  int e;

  printf("\nAdd 10M elements and verify (0.00001)\n");
  rv += add_random(10000000, 0.00001, 10000000, 0, 1, 32, 1);

  printf("\nChecking collision rates with filters from 100K to 1M (0.001)\n");
  for (e = 100000; e <= 1000000; e+= 100) {
    rv += add_random(e, 0.001, e, 1, 1, 8, 1);
  }

  return rv;
}


/** ***************************************************************************
 * With no options, runs brief default tests.
 *
 * With -L, runs some longer-running tests.
 *
 * To test collisions over a range of sizes: -G START END INCREMENT ERROR
 * This produces output that can be graphed with collisions/dograph
 * See also collision_test make target.
 *
 * To test collisions, run with options: -c ENTRIES ERROR COUNT
 * Where 'ENTRIES' is the expected number of entries used to initialize the
 * bloom filter and 'ERROR' is the acceptable probability of collision
 * used to initialize the bloom filter. 'COUNT' is the actual number of
 * entries inserted.
 *
 * To test performance only, run with options:  -p ENTRIES COUNT
 * Where 'ENTRIES' is the expected number of entries used to initialize the
 * bloom filter and 'COUNT' is the actual number of entries inserted.
 *
 */
int main(int argc, char **argv)
{
  // Calls return() instead of exit() just to make valgrind mark as
  // an error any reachable allocations. That makes them show up
  // when running the tests.

  int rv = 0;

  if (argc == 1) {
    printf("----- Running basic tests -----\n");
    rv = basic_tests();
    printf("----- DONE Running basic tests -----\n");
    return rv;
  }

  if (!strncmp(argv[1], "-L", 2)) {
    return larger_tests();
  }

  if (!strncmp(argv[1], "-G", 2)) {
    if (argc != 6) {
      printf("-G START END INCREMENT ERROR\n");
      return 1;
    }
    int e;
    for (e = atoi(argv[2]); e <= atoi(argv[3]); e+= atoi(argv[4])) {
      rv += add_random(e, atof(argv[5]), e, 1, 0, 32, 1);
    }
    return rv;
  }

  if (!strncmp(argv[1], "-c", 2)) {
    if (argc != 5) {
      printf("-c ENTRIES ERROR COUNT\n");
      return 1;
    }

    return add_random(atoi(argv[2]), atof(argv[3]), atoi(argv[4]), 0, 1, 32, 1);
  }

  if (!strncmp(argv[1], "-p", 2)) {
    if (argc != 4) {
      printf("-p ENTRIES COUNT\n");
    }
    return perf_loop(atoi(argv[2]), atoi(argv[3]));
  }

  return rv;
}
