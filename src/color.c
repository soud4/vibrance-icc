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
#include "color.h"

#include <math.h>

const double color_m[3][3] = {
    {0.4360747, 0.3850649, 0.1430804},
    {0.2225045, 0.7168786, 0.0606169},
    {0.0139322, 0.0971045, 0.7141733},
};

double color_mi[3][3];

const double color_d50[3] = {0.9642, 1.0, 0.8249};

/* CIE Lab space constants, in exact rational form. */
#define LAB_EPS (216.0 / 24389.0)
#define LAB_KAPPA (24389.0 / 27.0)

void color_init(void) {
  const double a = color_m[0][0], b = color_m[0][1], c = color_m[0][2];
  const double d = color_m[1][0], e = color_m[1][1], f = color_m[1][2];
  const double g = color_m[2][0], h = color_m[2][1], i = color_m[2][2];
  const double det =
      a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

  color_mi[0][0] = (e * i - f * h) / det;
  color_mi[0][1] = (c * h - b * i) / det;
  color_mi[0][2] = (b * f - c * e) / det;
  color_mi[1][0] = (f * g - d * i) / det;
  color_mi[1][1] = (a * i - c * g) / det;
  color_mi[1][2] = (c * d - a * f) / det;
  color_mi[2][0] = (d * h - e * g) / det;
  color_mi[2][1] = (b * g - a * h) / det;
  color_mi[2][2] = (a * e - b * d) / det;
}

double color_srgb_decode(double v) {
  return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

double color_srgb_encode(double v) {
  v = color_clamp(v);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

void color_xyz_to_lab(const double xyz[3], double lab[3]) {
  double f[3];
  int i;

  for (i = 0; i < 3; i++) {
    const double t = xyz[i] / color_d50[i];

    /* cbrt() instead of pow(t, 1/3): faster and more accurate. */
    f[i] = t > LAB_EPS ? cbrt(t) : (LAB_KAPPA * t + 16.0) / 116.0;
  }

  lab[0] = 116.0 * f[1] - 16.0;
  lab[1] = 500.0 * (f[0] - f[1]);
  lab[2] = 200.0 * (f[1] - f[2]);
}

void color_lab_to_xyz(const double lab[3], double xyz[3]) {
  const double fy = (lab[0] + 16.0) / 116.0;
  const double f[3] = {fy + lab[1] / 500.0, fy, fy - lab[2] / 200.0};
  int i;

  for (i = 0; i < 3; i++) {
    const double t3 = f[i] * f[i] * f[i];

    xyz[i] =
        (t3 > LAB_EPS ? t3 : (116.0 * f[i] - 16.0) / LAB_KAPPA) * color_d50[i];
  }
}

static double quant(double v) {
  v = nearbyint(v);
  return v < 0.0 ? 0.0 : (v > 65535.0 ? 65535.0 : v);
}

void color_lab_encode(const double lab[3], double u[3]) {
  u[0] = quant(lab[0] * 652.8);
  u[1] = quant((lab[1] + 128.0) * 256.0);
  u[2] = quant((lab[2] + 128.0) * 256.0);
}

void color_lab_decode(const double u[3], double lab[3]) {
  lab[0] = u[0] / 652.8;
  lab[1] = u[1] / 256.0 - 128.0;
  lab[2] = u[2] / 256.0 - 128.0;
}
