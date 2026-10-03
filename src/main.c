/*
 * vibrance-icc - ICC profile generator with vibrant-cli style saturation
 * Copyright (C) 2026 soud4
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. It is distributed WITHOUT ANY WARRANTY. See the LICENSE
 * file.
 */

/* getopt_long, open() and fdopen() live outside pure C11. */
#define _GNU_SOURCE
#include "color.h"
#include "icc.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM "vibrance-icc"
#define VERSION "1.0.0"

#define SAT_MIN 0.05
#define SAT_MAX 4.0
#define GRID_MIN 17
#define GRID_MAX 65
#define GRID_DEF 65

static void usage(FILE *out) {
  fprintf(
      out,
      "Usage: " PROGRAM " <saturation> [options]\n"
      "\n"
      "Generates an ICC profile that applies the vibrant-cli saturation to "
      "the\n"
      "whole screen:  output = s * color + (1 - s) * average(R, G, B)\n"
      "\n"
      "  <saturation>        1.0 = no change, >1 more saturated, <1 less.\n"
      "                      Allowed range: %.2f to %.2f. Try 1.3 - 1.6.\n"
      "\n"
      "Options:\n"
      "  -o, --output FILE     destination (default "
      "vibrance-<saturation>.icc)\n"
      "  -g, --grid N          table points per axis, %d to %d (default %d)\n"
      "  -f, --force           overwrite the file if it already exists\n"
      "  -q, --quiet           print nothing if all goes well\n"
      "      --selftest        check the profile in memory and exit\n"
      "  -V, --version         show the version\n"
      "  -h, --help            show this help\n"
      "\n"
      "To use it in Sway, in ~/.config/sway/config:\n"
      "  output * color_profile icc /path/to/profile.icc\n",
      SAT_MIN, SAT_MAX, GRID_MIN, GRID_MAX, GRID_DEF);
}

/*
 * Checks that A2B and B2A really are inverses: walks a grid of colors, takes
 * them to Lab and brings them back. The points where the formula clips
 * (outside the RGB cube) are not invertible and are counted separately.
 */
static int selftest(double s, int grid) {
  const int n = 17;
  const double step = 65535.0 / (n - 1);
  double worst = 0.0;
  long checked = 0, clipped = 0;
  int i, j, k, c;

  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      for (k = 0; k < n; k++) {
        const double in[3] = {i * step, j * step, k * step};
        double lab[3], back[3];
        double avg, inv, err = 0.0;
        int clip = 0;

        /* Does undoing the saturation push any
         * channel out of the cube? Then the return
         * trip cannot recover the original. */
        inv = 1.0 / s;
        avg = (in[0] + in[1] + in[2]) / 3.0;
        for (c = 0; c < 3; c++) {
          const double v = inv * in[c] + (1.0 - inv) * avg;

          if (v < -0.5 || v > 65535.5)
            clip = 1;
        }

        icc_a2b(in, lab, s);
        icc_b2a(lab, back, s);

        for (c = 0; c < 3; c++) {
          const double e = fabs(back[c] - in[c]);

          if (e > err)
            err = e;
        }

        if (clip) {
          clipped++;
          continue;
        }
        checked++;
        if (err > worst)
          worst = err;
      }
    }
  }

  printf("selftest: saturation %.2f, grid %d\n", s, grid);
  printf("  invertible samples: %ld (clipped: %ld)\n", checked, clipped);
  printf("  worst round-trip error: %.1f of 65535 (%.5f%%)\n", worst,
         worst * 100.0 / 65535.0);
  printf("  profile size: %zu bytes\n", icc_size(s, grid));

  /* A round-trip error above ~1/1000 of the range would mean the two
   * tables do not describe the same transformation. */
  if (worst > 65.0) {
    fprintf(stderr, "selftest: FAILED, the error is too high\n");
    return 1;
  }
  puts("  result: OK");
  return 0;
}

static int parse_double(const char *s, double *out) {
  char *end;

  errno = 0;
  *out = strtod(s, &end);
  if (end == s || *end != '\0' || errno == ERANGE || !isfinite(*out))
    return -1;
  return 0;
}

static int parse_int(const char *s, long *out) {
  char *end;

  errno = 0;
  *out = strtol(s, &end, 10);
  return (end == s || *end != '\0' || errno == ERANGE) ? -1 : 0;
}

static int write_profile(const char *path, const unsigned char *data,
                         size_t size, int force) {
  int flags = O_WRONLY | O_CREAT | (force ? O_TRUNC : O_EXCL);
  int fd = open(path, flags, 0644);
  FILE *f;

  if (fd < 0) {
    if (errno == EEXIST)
      fprintf(stderr,
              PROGRAM ": %s already exists (use "
                      "--force to overwrite it)\n",
              path);
    else
      fprintf(stderr, PROGRAM ": %s: %s\n", path, strerror(errno));
    return -1;
  }

  f = fdopen(fd, "wb");
  if (!f) {
    fprintf(stderr, PROGRAM ": %s: %s\n", path, strerror(errno));
    close(fd);
    return -1;
  }

  if (fwrite(data, 1, size, f) != size || fclose(f) != 0) {
    fprintf(stderr, PROGRAM ": %s: could not write: %s\n", path,
            strerror(errno));
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  static const struct option opts[] = {
      {"output", required_argument, NULL, 'o'},
      {"grid", required_argument, NULL, 'g'},
      {"force", no_argument, NULL, 'f'},
      {"quiet", no_argument, NULL, 'q'},
      {"selftest", no_argument, NULL, 't'},
      {"version", no_argument, NULL, 'V'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0},
  };
  const char *output = NULL;
  char fallback[64];
  double saturation;
  long grid = GRID_DEF;
  int force = 0, quiet = 0, test = 0, c;
  unsigned char *profile;
  size_t size;

  while ((c = getopt_long(argc, argv, "o:g:fqVh", opts, NULL)) != -1) {
    switch (c) {
    case 'o':
      output = optarg;
      break;
    case 'g':
      if (parse_int(optarg, &grid) < 0) {
        fprintf(stderr,
                PROGRAM ": --grid: '%s' is not "
                        "an integer\n",
                optarg);
        return 2;
      }
      break;
    case 'f':
      force = 1;
      break;
    case 'q':
      quiet = 1;
      break;
    case 't':
      test = 1;
      break;
    case 'V':
      puts(PROGRAM " " VERSION);
      return 0;
    case 'h':
      usage(stdout);
      return 0;
    default:
      usage(stderr);
      return 2;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, PROGRAM ": missing saturation value\n\n");
    usage(stderr);
    return 2;
  }
  if (parse_double(argv[optind], &saturation) < 0) {
    fprintf(stderr, PROGRAM ": '%s' is not a number\n", argv[optind]);
    return 2;
  }
  if (argc - optind > 1) {
    fprintf(stderr, PROGRAM ": extra argument: '%s'\n", argv[optind + 1]);
    return 2;
  }
  if (saturation < SAT_MIN || saturation > SAT_MAX) {
    fprintf(stderr,
            PROGRAM ": saturation must be between "
                    "%.2f and %.2f\n",
            SAT_MIN, SAT_MAX);
    return 2;
  }
  if (grid < GRID_MIN || grid > GRID_MAX) {
    fprintf(stderr, PROGRAM ": --grid must be between %d and %d\n", GRID_MIN,
            GRID_MAX);
    return 2;
  }

  color_init();

  if (test)
    return selftest(saturation, (int)grid);

  if (!output) {
    snprintf(fallback, sizeof fallback, "vibrance-%.2f.icc", saturation);
    output = fallback;
  }

  profile = icc_build(saturation, (int)grid, &size);
  if (!profile) {
    fprintf(stderr,
            PROGRAM ": out of memory for a grid %ld "
                    "profile\n",
            grid);
    return 1;
  }

  if (write_profile(output, profile, size, force) < 0) {
    free(profile);
    return 1;
  }
  free(profile);

  if (!quiet)
    printf("Profile created: %s (%.1f MiB, grid %ld)\n", output,
           size / 1048576.0, grid);
  return 0;
}
