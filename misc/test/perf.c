/*
 *  Copyright (c) 2019, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
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


uint64_t get_current_time_millis()
{
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return (tp.tv_sec * 1000L) + (tp.tv_usec / 1000L);
}

void add_and_test(int entries, double error, uint64_t count,
                  char test_known_added)
{
  struct bloom bloom;
  uint64_t n, initial, found = 0;
  int collisions = 0;
  int rv;

  int fd = open("/dev/urandom", O_RDONLY);
  read(fd, &n, sizeof(uint64_t));
  close(fd);
  initial = n;

  assert(bloom_init(&bloom, entries, error) == 0);

  uint64_t t1 = get_current_time_millis();

  n = initial;
  for (uint64_t c = 0; c < count; c++) {
    collisions += bloom_add(&bloom, &n, sizeof(uint64_t));
    n++;
  }

  uint64_t t2 = get_current_time_millis();

  if (test_known_added) { n = initial; }

  for (uint64_t c = 0; c < count; c++) {
    rv = bloom_check(&bloom, &n, sizeof(uint64_t));
    if (test_known_added) { assert(rv == 1); }
    n++;
    found += rv;
  }

  uint64_t t3 = get_current_time_millis();

  double pct = (double)collisions / (double)entries;

  printf("add_and_test: %10d (%1.4f): %8d collisions (%1.4f), %10" PRIu64
         " found; ADD: %6" PRIu64 " ms, CHECK: %6" PRIu64 " ms\n",
         entries, error, collisions, pct, found, (t2-t1), (t3-t2));
}


void basic()
{
  printf("libloom %s\n", bloom_version());

  int n = 50000;

  add_and_test(n+15, 0.01, n, 1);
  add_and_test(n+15, 0.01, n, 0);

  n = 1000000;
  add_and_test(n, 0.1, n, 1);
  add_and_test(n, 0.1, n, 0);

  add_and_test(n, 0.01, n, 1);
  add_and_test(n, 0.01, n, 0);

  add_and_test(n, 0.001, n, 1);
  add_and_test(n, 0.001, n, 0);

  n = 10000000;
  add_and_test(n, 0.001, n, 1);
  add_and_test(n, 0.001, n, 0);
}


int main(int argc, char **argv)
{
  if (argc == 1) {
    basic();
    exit(0);
  }

  if (!strncmp(argv[1], "-E", 2)) {
    if (argc != 4) {
      printf("-E COUNT ERROR\n");
      printf("Will do runs adding COUNT elements into bloom filter.\n");
      printf("Initial bloom filter size is COUNT, then increasing.\n");
      printf("Will run until stopped...\n");
      exit(0);
    }

    int count = atoi(argv[2]);
    double error = atof(argv[3]);
    int capacity = count;
    while(1) {
      add_and_test(capacity, error, count, 0);
      capacity += 1;
    }
  }

}
