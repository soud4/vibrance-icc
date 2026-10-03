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
#ifndef VIBRANCE_ICC_H
#define VIBRANCE_ICC_H

#include <stddef.h>

/* Exact size in bytes of the profile icc_build() will generate. */
size_t icc_size(double saturation, int grid);

/*
 * Builds the complete profile in memory. Returns a buffer the caller must
 * release with free(), or NULL if out of memory. *size receives the size.
 */
unsigned char *icc_build(double saturation, int grid, size_t *size);

/*
 * The two transformations that fill the profile's tables, exposed so that
 * main.c can check the round trip (--selftest).
 * Input and output use the CLUT's 0..65535 scale, in floating point.
 */
void icc_a2b(const double in[3], double out[3], double saturation);
void icc_b2a(const double in[3], double out[3], double saturation);

#endif /* VIBRANCE_ICC_H */
