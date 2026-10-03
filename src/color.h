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
#ifndef VIBRANCE_COLOR_H
#define VIBRANCE_COLOR_H

/* sRGB -> XYZ (D50): the standard matrix from the ICC sRGB profile. */
extern const double color_m[3][3];
/* Its inverse. Only valid after calling color_init(). */
extern double color_mi[3][3];
/* D50 white point. */
extern const double color_d50[3];

/* Computes color_mi. Must be called once before using anything else. */
void color_init(void);

static inline double color_clamp(double v) {
  return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/*
 * The exact libvibrant (vibrant-cli) formula:
 *     output = s * color + (1 - s) * average(R, G, B)
 *
 * The caller passes s and 1-s already computed: this runs hundreds of
 * thousands of times and there is no point redoing the subtraction at every
 * table point.
 */
static inline void color_vibrant(const double in[3], double s, double s1,
                                 double out[3]) {
  const double avg = (in[0] + in[1] + in[2]) / 3.0;
  int i;

  for (i = 0; i < 3; i++)
    out[i] = color_clamp(s * in[i] + s1 * avg);
}

/* 3x3 matrix by vector product; the matrix is passed flat so that a const
 * matrix and one computed at run time work the same. */
static inline void color_mul(const double *m, const double v[3],
                             double out[3]) {
  int i;

  for (i = 0; i < 3; i++)
    out[i] = m[3 * i] * v[0] + m[3 * i + 1] * v[1] + m[3 * i + 2] * v[2];
}

/* Linear RGB -> XYZ (D50) and its inverse. */
static inline void color_to_xyz(const double rgb[3], double xyz[3]) {
  color_mul(&color_m[0][0], rgb, xyz);
}

static inline void color_from_xyz(const double xyz[3], double rgb[3]) {
  color_mul(&color_mi[0][0], xyz, rgb);
}

double color_srgb_decode(double v);
double color_srgb_encode(double v);

void color_xyz_to_lab(const double xyz[3], double lab[3]);
void color_lab_to_xyz(const double lab[3], double xyz[3]);

/*
 * ICC v2 16-bit Lab encoding (the one lut16Type expects).
 * color_lab_encode produces the three u16 values; color_lab_decode goes the
 * other way and accepts non-integer values, because the CLUT grid is walked
 * in floating point (x * 65535 / (grid - 1)).
 */
void color_lab_encode(const double lab[3], double u[3]);
void color_lab_decode(const double u[3], double lab[3]);

#endif /* VIBRANCE_COLOR_H */
