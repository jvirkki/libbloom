/*
 *  Copyright (c) 2012-2015, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

#ifndef _BLOOM_H
#define _BLOOM_H


/** ***************************************************************************
 * On Linux, the code attempts to compute a bucket size based on CPU cache
 * size info, if available. If that fails for any reason, this fallback size
 * is used instead.
 *
 * On non-Linux systems, this is the bucket size always used unless the
 * caller overrides it (see bloom_init_size()).
 *
 */
#define BLOOM_BUCKET_SIZE_FALLBACK (32 * 1024)


/** ***************************************************************************
 * It was found that using multiplier x0.5 for CPU L1 cache size is
 * more effective in terms of CPU usage and, surprisingly, collisions
 * number.
 *
 * Feel free to tune this constant the way it will work for you.
 *
 */
#define BLOOM_L1_CACHE_SIZE_DIV 1


/** ***************************************************************************
 * Structure to keep track of one bloom filter.  Caller needs to
 * allocate this and pass it to the functions below. First call for
 * every struct must be to bloom_init().
 *
 */
struct bloom
{
  // These fields are part of the public interface of this structure.
  // Client code may read these values if desired. Client code MUST NOT
  // modify any of these.
  int entries;
  double error;
  int bits;
  int bytes;
  int hashes;

  // Fields below are private to the implementation. These may go away or
  // change incompatibly at any moment. Client code MUST NOT access or rely
  // on these.
  unsigned buckets;
  unsigned bucket_bytes;

  // x86 CPU divide by/multiply by operation optimization helpers
  unsigned bucket_bytes_exponent;
  unsigned bucket_bits_fast_mod_operand;

  double bpe;
  unsigned char * bf;
  int ready;
};


/** ***************************************************************************
 * Initialize the bloom filter for use.
 *
 * The filter is initialized with a bit field and number of hash functions
 * according to the computations from the wikipedia entry:
 *     http://en.wikipedia.org/wiki/Bloom_filter
 *
 * Optimal number of bits is:
 *     bits = (entries * ln(error)) / ln(2)^2
 *
 * Optimal number of hash functions is:
 *     hashes = bpe * ln(2)
 *
 * Parameters:
 * -----------
 *     bloom   - Pointer to an allocated struct bloom (see above).
 *     entries - The expected number of entries which will be inserted.
 *     error   - Probability of collision (as long as entries are not
 *               exceeded).
 *
 * Return:
 * -------
 *     0 - on success
 *     1 - on failure
 *
 */
int bloom_init(struct bloom * bloom, int entries, double error);


/** ***************************************************************************
 * Initialize the bloom filter for use.
 *
 * See comments above for general information.
 *
 * This is the same as bloom_init() but allows the caller to pass in a
 * cache_size to override the internal value (which is either computed
 * or the default of BLOOM_BUCKET_SIZE_FALLBACK). Mostly useful for
 * experimenting.
 *
 * See misc/bucketsize for a script which can help identify a good value
 * for cache_size.
 *
 */
int bloom_init_size(struct bloom * bloom, int entries, double error,
                    unsigned int cache_size);


/** ***************************************************************************
 * Check if the given element is in the bloom filter. Remember this may
 * return false positive if a collision occured.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *     buffer - Pointer to buffer containing element to check.
 *     len    - Size of 'buffer'.
 *
 * Return:
 * -------
 *     0 - element is not present
 *     1 - element is present (or false positive due to collision)
 *    -1 - bloom not initialized
 *
 */
int bloom_check(struct bloom * bloom, const void * buffer, int len);


/** ***************************************************************************
 * Add the given element to the bloom filter.
 * The return code indicates if the element (or a collision) was already in,
 * so for the common check+add use case, no need to call check separately.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *     buffer - Pointer to buffer containing element to add.
 *     len    - Size of 'buffer'.
 *
 * Return:
 * -------
 *     0 - element was not present and was added
 *     1 - element (or a collision) had already been added previously
 *    -1 - bloom not initialized
 *
 */
int bloom_add(struct bloom * bloom, const void * buffer, int len);


/** ***************************************************************************
 * Print (to stdout) info about this bloom filter. Debugging aid.
 *
 */
void bloom_print(struct bloom * bloom);


/** ***************************************************************************
 * Deallocate internal storage.
 *
 * Upon return, the bloom struct is no longer usable. You may call bloom_init
 * again on the same struct to reinitialize it again.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *
 * Return: none
 *
 */
void bloom_free(struct bloom * bloom);


/** ***************************************************************************
 * Returns version string compiled into library.
 *
 * Return: version string
 *
 */
const char * bloom_version();


#endif
