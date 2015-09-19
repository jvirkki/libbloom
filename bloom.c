/*
 *  Copyright (c) 2012-2015, Jyri J. Virkki
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

#ifdef __linux__
unsigned detect_bucket_size(unsigned fallback_size);
#endif


static int test_bit_set_bit(unsigned char * buf, unsigned int x, int set_bit)
{
  register uint32_t * word_buf = (uint32_t *)buf;
  register unsigned int offset = x >> 5;
  register uint32_t word = word_buf[offset];
  register unsigned int mask = 1 << (x % 32);

  if (word & mask) {
    return 1;
  } else {
    if (set_bit) {
      word_buf[offset] = word | mask;
    }
    return 0;
  }
}


static int bloom_check_add(struct bloom * bloom,
                           const void * buffer, int len, int add)
{
  if (bloom->ready == 0) {
    (void)printf("bloom at %p not initialized!\n", (void *)bloom);
    return -1;
  }

  int hits = 0;
  register unsigned int a = murmurhash2(buffer, len, 0x9747b28c);
  register unsigned int b = murmurhash2(buffer, len, a);
  register unsigned int x;
  register unsigned int i;

  unsigned bucket_index = (a % bloom->buckets);

  unsigned char * bucket_ptr =
    (bloom->bf + (bucket_index << bloom->bucket_bytes_exponent));

  for (i = 0; i < bloom->hashes; i++) {
    x = (a + i*b) & bloom->bucket_bits_fast_mod_operand;
    if (test_bit_set_bit(bucket_ptr, x, add)) {
      hits++;
    }
  }

  if (hits == bloom->hashes) {
    return 1;                // 1 == element already in (or collision)
  }

  return 0;
}


static void setup_buckets(struct bloom * bloom, unsigned int cache_size)
{
  // If caller passed a non-zero cache_size, use it as given, otherwise
  // either compute it or use built-in default

  if (cache_size == 0) {
#ifdef __linux__
    cache_size = detect_bucket_size(BLOOM_BUCKET_SIZE_FALLBACK);
#else
    cache_size = BLOOM_BUCKET_SIZE_FALLBACK;
#endif
  }

  bloom->buckets = (bloom->bytes / cache_size);
  bloom->bucket_bytes = cache_size;

  // make sure bloom buffer bytes and bucket_bytes are even
  int not_even_by = (bloom->bytes % bloom->bucket_bytes);

  if (not_even_by) {
    // adjust bytes
    bloom->bytes += (bloom->bucket_bytes - not_even_by);
    assert((bloom->bytes % bloom->bucket_bytes) == 0); // Should get even

    // adjust bits
    bloom->bits = bloom->bytes * 8;

    // adjust bits per element
    bloom->bpe = bloom->bits*1. / bloom->entries;

    // adjust buckets
    bloom->buckets++;
  }

  bloom->bucket_bytes_exponent = __builtin_ctz(cache_size);
  bloom->bucket_bits_fast_mod_operand = (cache_size * 8 - 1);
}


int bloom_init_size(struct bloom * bloom, int entries, double error,
                    unsigned int cache_size)
{
  bloom->ready = 0;

  if (entries < 1 || error == 0) {
    return 1;
  }

  bloom->entries = entries;
  bloom->error = error;

  double num = log(bloom->error);
  double denom = 0.480453013918201; // ln(2)^2
  bloom->bpe = -(num / denom);

  double dentries = (double)entries;
  bloom->bits = (int)(dentries * bloom->bpe);

  if (bloom->bits % 8) {
    bloom->bytes = (bloom->bits / 8) + 1;
  } else {
    bloom->bytes = bloom->bits / 8;
  }

  bloom->hashes = (int)ceil(0.693147180559945 * bloom->bpe);  // ln(2)

  setup_buckets(bloom, cache_size);

  bloom->bf = (unsigned char *)calloc(bloom->bytes, sizeof(unsigned char));
  if (bloom->bf == NULL) {
    return 1;
  }

  bloom->ready = 1;
  return 0;
}


int bloom_init(struct bloom * bloom, int entries, double error)
{
  return bloom_init_size(bloom, entries, error, 0);
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
  (void)printf("bloom at %p\n", (void *)bloom);
  (void)printf(" ->entries = %d\n", bloom->entries);
  (void)printf(" ->error = %f\n", bloom->error);
  (void)printf(" ->bits = %d\n", bloom->bits);
  (void)printf(" ->bits per elem = %f\n", bloom->bpe);
  (void)printf(" ->bytes = %d\n", bloom->bytes);
  (void)printf(" ->buckets = %u\n", bloom->buckets);
  (void)printf(" ->bucket_bytes = %u\n", bloom->bucket_bytes);
  (void)printf(" ->bucket_bytes_exponent = %u\n",
               bloom->bucket_bytes_exponent);
  (void)printf(" ->bucket_bits_fast_mod_operand = 0%o\n",
               bloom->bucket_bits_fast_mod_operand);
  (void)printf(" ->hash functions = %d\n", bloom->hashes);
}


void bloom_free(struct bloom * bloom)
{
  if (bloom->ready) {
    free(bloom->bf);
  }
  bloom->ready = 0;
}


const char * bloom_version()
{
  return MAKESTRING(BLOOM_VERSION);
}
