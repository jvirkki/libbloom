/*
 *  Copyright (c) 2022, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

/*
  Generate an image (PNG) based on the contents of a bloom filter bitmap.

  This is helpful to visualize how many bits are set. Each pixel in
  the image corresponds to one byte in the bitmap.

  The maximum image size is 1024 (square) unless you change MAXIMG
  below.  If the bitmap has more than 1024^2 bytes, the image is
  scaled down to fit into MAXIMG.

  Run without any arguments for usage info.

  Building this required the GD graphics library. On Debian, this is
  in libgd3 and libgd-dev.

  For testing only, so not much in the way of error handling.

 */

#include <assert.h>
#include <fcntl.h>
#include <gd.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bloom.h"

#define MAXIMG 1024

/*
 * Generate an image from 'bloom' into 'filename'
 *
 */
static void bloom2png(struct bloom * bloom, const char * filename)
{
  FILE *pngout;
  gdImagePtr image;
  int black;
  int scale;
  float scaling;

  BYTES_T bytes = bloom->bytes;
  long double dbytes = (long double)bytes;

  printf("--- bloom2png ---\n");
  printf("Image filename: %s\n", filename);
  printf("bloom->bytes: %lu (or as long double %Lf)\n", bytes, dbytes);

  long double sqrtd = sqrtl(dbytes);
  BYTES_T sqrtt = (BYTES_T)ceill(sqrtd);

  printf(" sqrtd: %Lf\n", sqrtd);
  printf(" image size: %lu\n", sqrtt);

  int size;
  if (sqrtt > MAXIMG) {
    size = MAXIMG;
    scale = 1;
    scaling = ((float)MAXIMG) / ((float)sqrtt);
    printf("Image size %lu too large, scaling down to %d (scaling factor %f)\n",
           sqrtt, MAXIMG, scaling);
  } else {
    size = sqrtt;
    scale = 0;
  }

  // Allocate the image object
  image = gdImageCreate(size, size);

  // Allocate color white (red, green and blue all maximum).
  // Because this is first call to gdImageColorAllocate() it becomes
  // the background color.

  gdImageColorAllocate(image, 255, 255, 255);

  // Allocate color black (red, green and blue all minimum).

  black = gdImageColorAllocate(image, 0, 0, 0);

  BYTES_T n, nonzero = 0;
  int x, y;
  for (n = 0; n < bytes; n++) {
    if (bloom->bf[n]) {
      nonzero++;
      x = n % sqrtt;
      y = n / sqrtt;
      //printf("byte %lu maps to (%d,%d)\n", n, x, y);
      if (scale) {
        x = (int)(((float)x) * scaling);
        y = (int)(((float)y) * scaling);
        //printf("   byte %lu scaled to (%d,%d)\n", n, x, y);
      }
      gdImageLine(image, x, y, x, y, black);
    }
  }
  printf("In bitfield, %lu out of %lu bytes are nonzero\n", nonzero, bytes);

  pngout = fopen(filename, "wb");
  gdImagePng(image, pngout);
  fclose(pngout);
  gdImageDestroy(image);
}


/*
 * Add 'entries' random entries into 'bloom'
 *
 */
static void add_random(struct bloom * bloom, unsigned int entries)
{
  uint64_t n;
  unsigned int c;

  int fd = open("/dev/urandom", O_RDONLY);
  read(fd, &n, sizeof(uint64_t));
  close(fd);

  for (c = 0; c < entries; c++) {
    bloom_add(bloom, &n, sizeof(uint64_t));
    n++;
  }
}


/*
 * Main. See usage below.
 *
 */
int main(int argc, char **argv)
{
  if (argc == 1) {
    printf("Usage:\n\n");

    printf("visualize -t\n");
    printf("  Generate an internally hardcoded set of images.\n\n");

    printf("visualize -c entries error elements imagefile\n");
    printf("  Create a bloom filter with (entries, error) and insert 'elements' number\n");
    printf("  of random entries into it, then generate the image into 'imagefile'.\n\n");

    printf("visualize -l filename imagefile\n");
    printf("  Load a bloom filter from 'filename' (with bloom_load) and generate the\n");
    printf("  image into 'imagefile'.\n");
    exit(0);
  }

  if (!strncmp(argv[1], "-t", 2)) {
    struct bloom bloom = NULL_BLOOM_FILTER;

    assert(bloom_init2(&bloom, 10000000, 0.01) == 0);
    add_random(&bloom, 1000000);
    bloom2png(&bloom, "vis_10M01.png");
    bloom_free(&bloom);

    assert(bloom_init2(&bloom, 2147483647, 0.01) == 0);
    add_random(&bloom, 1000000);
    bloom2png(&bloom, "vis_intmax01.png");
    bloom_free(&bloom);

    assert(bloom_init2(&bloom, 4294967295, 0.01) == 0);
    add_random(&bloom, 1000000);
    bloom2png(&bloom, "vis_uintmax01.png");
    bloom_free(&bloom);

    exit(0);
  }

  if (!strncmp(argv[1], "-c", 2)) {
    if (argc != 6) {
      printf("error: wrong number of args to -c\n");
      exit(1);
    }

    struct bloom bloom = NULL_BLOOM_FILTER;

    unsigned int entries = atoi(argv[2]);
    double error = atof(argv[3]);
    unsigned int elements = atoi(argv[4]);
    char * imagefile = argv[5];

    printf("bloom_init2(%u, %f)\n", entries, error);
    assert(bloom_init2(&bloom, entries, error) == 0);

    bloom_print(&bloom);
    printf("Adding %u elements\n", elements);
    add_random(&bloom, elements);
    bloom2png(&bloom, imagefile);
    exit(0);
  }

  if (!strncmp(argv[1], "-l", 2)) {
    if (argc != 4) {
      printf("error: wrong number of args to -l\n");
      exit(1);
    }

    struct bloom bloom = NULL_BLOOM_FILTER;
    char * inputfile = argv[2];
    char * imagefile = argv[3];

    if (bloom_load(&bloom, inputfile) != 0) {
      printf("error: while loading file %s\n", inputfile);
      exit(1);
    }

    bloom2png(&bloom, imagefile);
    exit(0);
  }

}
