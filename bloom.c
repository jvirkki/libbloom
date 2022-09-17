/*
 *  Copyright (c) 2012-2022, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

/*
 * Refer to bloom.h for documentation on the public interfaces.
 */

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bloom.h"
#include "murmurhash2.h"

#define MAKESTRING(n) STRING(n)
#define STRING(n) #n
#define BLOOM_MAGIC "libbloom2"

inline static int test_bit_set_bit(unsigned char * buf,
                                   unsigned long int bit, int set_bit)
{
  unsigned long int byte = bit >> 3;
  unsigned char c = buf[byte];        // expensive memory access
  unsigned char mask = 1 << (bit % 8ul);

  if (c & mask) {
    return 1;
  } else {
    if (set_bit) {
      buf[byte] = c | mask;
    }
    return 0;
  }
}


static int bloom_check_add(struct bloom * bloom,
                           const void * buffer, int len, int add)
{
  if (bloom->ready == 0) {
    printf("bloom at %p not initialized!\n", (void *)bloom);
    return -1;
  }

  unsigned char hits = 0;
  unsigned int a = murmurhash2(buffer, len, 0x9747b28c);
  unsigned int b = murmurhash2(buffer, len, a);
  unsigned long int x;
  unsigned long int i;

  for (i = 0; i < bloom->hashes; i++) {
    x = (a + b*i) % bloom->bits;
    if (test_bit_set_bit(bloom->bf, x, add)) {
      hits++;
    } else if (!add) {
      // Don't care about the presence of all the bits. Just our own.
      return 0;
    }
  }

  if (hits == bloom->hashes) {
    return 1;                // 1 == element already in (or collision)
  }

  return 0;
}


// DEPRECATED - Please migrate to bloom_init2.
int bloom_init(struct bloom * bloom, int entries, double error)
{
  return bloom_init2(bloom, (unsigned int)entries, error);
}


int bloom_init2(struct bloom * bloom, unsigned int entries, double error)
{
  if (sizeof(unsigned long int) < 8) {
    printf("error: libbloom will not function correctly because\n");
    printf("sizeof(unsigned long int) == %ld\n", sizeof(unsigned long int));
    exit(1);
  }

  memset(bloom, 0, sizeof(struct bloom));

  if (entries < 1000 || error <= 0 || error >= 1) {
    return 1;
  }

  bloom->entries = entries;
  bloom->error = error;

  double num = -log(bloom->error);
  double denom = 0.480453013918201; // ln(2)^2
  bloom->bpe = (num / denom);

  long double dentries = (long double)entries;
  long double allbits = dentries * bloom->bpe;
  bloom->bits = (unsigned long int)allbits;

  if (bloom->bits % 8) {
    bloom->bytes = (bloom->bits / 8) + 1;
  } else {
    bloom->bytes = bloom->bits / 8;
  }

  bloom->hashes = (unsigned char)ceil(0.693147180559945 * bloom->bpe); // ln(2)

  bloom->bf = (unsigned char *)calloc(bloom->bytes, sizeof(unsigned char));
  if (bloom->bf == NULL) {                                   // LCOV_EXCL_START
    return 1;
  }                                                          // LCOV_EXCL_STOP

  bloom->ready = 1;

  bloom->major = BLOOM_VERSION_MAJOR;
  bloom->minor = BLOOM_VERSION_MINOR;

  return 0;
}


int bloom_check(struct bloom * bloom, const void * buffer, int len)
{
  return bloom_check_add(bloom, buffer, len, 0);
}


int bloom_add(struct bloom * bloom, const void * buffer, int len)
{
  return bloom_check_add(bloom, buffer, len, 1);
}


void bloom_print(struct bloom * bloom)
{
  printf("bloom at %p\n", (void *)bloom);
  if (!bloom->ready) { printf(" *** NOT READY ***\n"); }
  printf(" ->version = %d.%d\n", bloom->major, bloom->minor);
  printf(" ->entries = %u\n", bloom->entries);
  printf(" ->error = %f\n", bloom->error);
  printf(" ->bits = %lu\n", bloom->bits);
  printf(" ->bits per elem = %f\n", bloom->bpe);
  printf(" ->bytes = %lu", bloom->bytes);
  unsigned int KB = bloom->bytes / 1024;
  unsigned int MB = KB / 1024;
  printf(" (%u KB, %u MB)\n", KB, MB);
  printf(" ->hash functions = %d\n", bloom->hashes);
}


void bloom_free(struct bloom * bloom)
{
  if (bloom->ready) {
    free(bloom->bf);
  }
  bloom->ready = 0;
}


int bloom_reset(struct bloom * bloom)
{
  if (!bloom->ready) return 1;
  memset(bloom->bf, 0, bloom->bytes);
  return 0;
}


int bloom_save(struct bloom * bloom, char * filename)
{
  if (filename == NULL || filename[0] == 0) {
    return 1;
  }

  int fd = open(filename, O_WRONLY | O_CREAT, 0644);
  if (fd < 0) {
    return 1;
  }

  ssize_t out = write(fd, BLOOM_MAGIC, strlen(BLOOM_MAGIC));
  if (out != strlen(BLOOM_MAGIC)) { goto save_error; }       // LCOV_EXCL_LINE

  uint16_t size = sizeof(struct bloom);
  out = write(fd, &size, sizeof(uint16_t));
  if (out != sizeof(uint16_t)) { goto save_error; }          // LCOV_EXCL_LINE

  out = write(fd, bloom, sizeof(struct bloom));
  if (out != sizeof(struct bloom)) { goto save_error; }      // LCOV_EXCL_LINE

  out = write(fd, bloom->bf, bloom->bytes);
  if (out != bloom->bytes) { goto save_error; }              // LCOV_EXCL_LINE

  close(fd);
  return 0;
                                                             // LCOV_EXCL_START
 save_error:
  close(fd);
  return 1;
                                                             // LCOV_EXCL_STOP
}


int bloom_load(struct bloom * bloom, char * filename)
{
  int rv = 0;

  if (filename == NULL || filename[0] == 0) { return 1; }
  if (bloom == NULL) { return 2; }

  memset(bloom, 0, sizeof(struct bloom));

  int fd = open(filename, O_RDONLY);
  if (fd < 0) { return 3; }

  char line[30];
  memset(line, 0, 30);
  ssize_t in = read(fd, line, strlen(BLOOM_MAGIC));

  if (in != strlen(BLOOM_MAGIC)) {
    rv = 4;
    goto load_error;
  }

  if (strncmp(line, BLOOM_MAGIC, strlen(BLOOM_MAGIC))) {
    rv = 5;
    goto load_error;
  }

  uint16_t size;
  in = read(fd, &size, sizeof(uint16_t));
  if (in != sizeof(uint16_t)) {
    rv = 6;
    goto load_error;
  }

  if (size != sizeof(struct bloom)) {
    rv = 7;
    goto load_error;
  }

  in = read(fd, bloom, sizeof(struct bloom));
  if (in != sizeof(struct bloom)) {
    rv = 8;
    goto load_error;
  }

  bloom->bf = NULL;
  if (bloom->major != BLOOM_VERSION_MAJOR) {
    rv = 9;
    goto load_error;
  }

  bloom->bf = (unsigned char *)malloc(bloom->bytes);
  if (bloom->bf == NULL) { rv = 10; goto load_error; }       // LCOV_EXCL_LINE

  in = read(fd, bloom->bf, bloom->bytes);
  if (in != bloom->bytes) {
    rv = 11;
    free(bloom->bf);
    bloom->bf = NULL;
    goto load_error;
  }

  close(fd);
  return rv;

 load_error:
  close(fd);
  bloom->ready = 0;
  return rv;
}


int bloom_merge(struct bloom * bloom_dest, struct bloom * bloom_src)
{
  if (bloom_dest->ready == 0) {
    printf("bloom at %p not initialized!\n", (void *)bloom_dest);
    return -1;
  }

  if (bloom_src->ready == 0) {
    printf("bloom at %p not initialized!\n", (void *)bloom_src);
    return -1;
  }

  if (bloom_dest->entries != bloom_src->entries) {
    return 1;
  }

  if (bloom_dest->error != bloom_src->error) {
    return 1;
  }

  if (bloom_dest->major != bloom_src->major) {
    return 1;
  }

  if (bloom_dest->minor != bloom_src->minor) {
    return 1;
  }

  // Not really possible if properly used but check anyway to avoid the
  // possibility of buffer overruns.
  if (bloom_dest->bytes != bloom_src->bytes) {
    return 1;                                                // LCOV_EXCL_LINE
  }

  unsigned long int p;
  for (p = 0; p < bloom_dest->bytes; p++) {
    bloom_dest->bf[p] |= bloom_src->bf[p];
  }

  return 0;
}


const char * bloom_version()
{
  return MAKESTRING(BLOOM_VERSION);
}
