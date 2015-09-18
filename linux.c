/*
 * Contributed by m0nkeyc0der <noface@inbox.ru>
 *
 * This file is under BSD license. See LICENSE file.
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bloom.h"


/**
 * Gets first line of file (w/o new line character)
 */
static const char * get_file_content(int dir, const char * file)
{
  static char buf[128];

  int fd = openat(dir, file, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }

  FILE * fp = fdopen(fd, "r");
  int ok = fscanf(fp, "%127s", buf);
  fclose(fp); // this also closes fd

  return ok ? buf : NULL;
}


static int apply_size_suffix(int val, char suffix, const char * errmsg)
{
  switch (suffix) {
  case 'K':
    return val * 1024;
  case 'M':
    return val * 1024*1024;
  default:
    printf("%s: Unknown suffix '%c'\n", errmsg, suffix);
    return -1;
  }
}


static unsigned make_log2_friendly(unsigned cache_size)
{
  return 1 << (int)log2(cache_size);
}


unsigned detect_bucket_size(unsigned fallback_size)
{
  const char * s;
  char size_suffix;
  static int bucket_size = 0;

  if (bucket_size) {
    return bucket_size > 0 ?
      (bucket_size / BLOOM_L1_CACHE_SIZE_DIV) : fallback_size;
  }

  int dir = open("/sys/devices/system/cpu/cpu0/cache/index0", O_DIRECTORY);
  if (dir < 0) {
    bucket_size = -1;
    return fallback_size;
  }

  // Double check cache is L1
  if (!(s = get_file_content(dir, "level")) || strncmp(s, "1", 1) != 0) {
    printf("Cannot detect L1 cache size in %s:%d\n", __FILE__, __LINE__);
    goto out_err;
  }

  // Double check cache type is "Data"
  if (!(s = get_file_content(dir, "type")) || strncmp(s, "Data", 4) != 0) {
    printf("Cannot detect L1 cache size in %s:%d\n", __FILE__, __LINE__);
    goto out_err;
  }

  // Fetch L1 cache size
  if (!(s = get_file_content(dir, "size")) ||
      sscanf(s, "%d%c", &bucket_size, &size_suffix) != 2) {
    printf("Cannot detect L1 cache size in %s:%d\n", __FILE__, __LINE__);
    goto out_err;
  }

  bucket_size = apply_size_suffix(bucket_size, size_suffix,
                                  "Cannot detect L1 cache size");
  if (bucket_size < 0) {
    goto out_err;
  }

  bucket_size  = make_log2_friendly(bucket_size);
  bucket_size /= BLOOM_L1_CACHE_SIZE_DIV;

  close(dir);
  return bucket_size;

 out_err:
  bucket_size = -1;
  close(dir);
  return fallback_size;
}
