/*
 * vibrance-icc - ICC profile generator with vibrant-cli style saturation
 * Copyright (C) 2026 soud4
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. It is distributed WITHOUT ANY WARRANTY. See the LICENSE
 * file.
 *
 * Writes a monitor class ICC v2 profile whose PCS is Lab and whose conversion
 * is described by tables (lut16Type / 'mft2'): there is no way to express the
 * libvibrant formula with a matrix and three curves, so it gets tabulated.
 */
#include "icc.h"
#include "color.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPRT_TEXT "Public domain"

/*
 * Sequential write buffer over an exactly sized allocation: the profile size
 * is known in advance from grid, so there is not a single realloc while the
 * CLUT is being filled.
 */
struct buf {
  unsigned char *p;
  size_t len;
  size_t cap;
};

static void put(struct buf *b, const void *data, size_t n) {
  assert(b->len + n <= b->cap);
  memcpy(b->p + b->len, data, n);
  b->len += n;
}

static void put_str(struct buf *b, const char *s) { put(b, s, strlen(s)); }

static void put_zeros(struct buf *b, size_t n) {
  assert(b->len + n <= b->cap);
  memset(b->p + b->len, 0, n);
  b->len += n;
}

static void put_u8(struct buf *b, unsigned v) {
  const unsigned char c = (unsigned char)v;

  put(b, &c, 1);
}

/* The whole ICC format is big-endian; it is written byte by byte so as not
 * to depend on the machine's endianness or on htons(). */
static void put_u16(struct buf *b, unsigned v) {
  put_u8(b, (v >> 8) & 0xff);
  put_u8(b, v & 0xff);
}

static void put_u32(struct buf *b, uint32_t v) {
  put_u8(b, (v >> 24) & 0xff);
  put_u8(b, (v >> 16) & 0xff);
  put_u8(b, (v >> 8) & 0xff);
  put_u8(b, v & 0xff);
}

/* s15Fixed16Number: the real v with 16 bits of fractional part. */
static void put_s15f16(struct buf *b, double v) {
  put_u32(b, (uint32_t)(int32_t)nearbyint(v * 65536.0));
}

static size_t pad4(size_t n) { return (n + 3u) & ~(size_t)3u; }

/* --- The two tabulated transformations ------------------------------------ */

/*
 * Monitor -> PCS: undoes the saturation. It uses 1/s, which never pushes the
 * color out of the cube, and describes the result in Lab.
 */
void icc_a2b(const double in[3], double out[3], double s) {
  const double inv = 1.0 / s;
  double d[3], c[3], lin[3], xyz[3], lab[3];
  int i;

  for (i = 0; i < 3; i++)
    d[i] = in[i] / 65535.0;

  color_vibrant(d, inv, 1.0 - inv, c);

  for (i = 0; i < 3; i++)
    lin[i] = color_srgb_decode(c[i]);

  color_to_xyz(lin, xyz);
  color_xyz_to_lab(xyz, lab);
  color_lab_encode(lab, out);
}

/*
 * PCS -> monitor: the sRGB color matching that Lab, saturated with the
 * vibrant-cli formula. This is the direction Sway actually applies.
 */
void icc_b2a(const double in[3], double out[3], double s) {
  double lab[3], xyz[3], lin[3], c[3], v[3];
  int i;

  color_lab_decode(in, lab);
  color_lab_to_xyz(lab, xyz);
  color_from_xyz(xyz, lin);

  for (i = 0; i < 3; i++)
    c[i] = color_srgb_encode(lin[i]);

  color_vibrant(c, s, 1.0 - s, v);

  for (i = 0; i < 3; i++)
    out[i] = nearbyint(color_clamp(v[i]) * 65535.0);
}

/* --- Tags ----------------------------------------------------------------- */

typedef void (*clut_fn)(const double in[3], double out[3], double s);

static size_t lut16_size(int grid) {
  const size_t n = (size_t)grid * (size_t)grid * (size_t)grid;

  /* header + input tables + CLUT + output tables */
  return 52u + 12u + 6u * n + 12u;
}

static void put_ramp(struct buf *b) {
  int i;

  /* Two-entry identity table for each of the 3 channels: the whole
   * transformation lives in the CLUT. */
  for (i = 0; i < 3; i++) {
    put_u16(b, 0);
    put_u16(b, 65535);
  }
}

static void put_lut16(struct buf *b, int grid, clut_fn fn, double s) {
  const double step = 65535.0 / (grid - 1);
  int i, j, k, c;

  put_str(b, "mft2");
  put_zeros(b, 4);
  put_u8(b, 3);              /* input channels      */
  put_u8(b, 3);              /* output channels     */
  put_u8(b, (unsigned)grid); /* points per axis     */
  put_zeros(b, 1);           /* reserved            */

  /* Input matrix: identity (it only applies if the input space is XYZ,
   * but the tag requires it regardless). */
  for (i = 0; i < 9; i++)
    put_s15f16(b, i % 4 == 0 ? 1.0 : 0.0);

  put_u16(b, 2); /* input table entries  */
  put_u16(b, 2); /* output table entries */

  put_ramp(b);

  for (i = 0; i < grid; i++) {
    for (j = 0; j < grid; j++) {
      for (k = 0; k < grid; k++) {
        const double in[3] = {i * step, j * step, k * step};
        double out[3];

        fn(in, out, s);
        for (c = 0; c < 3; c++)
          put_u16(b, (unsigned)out[c]);
      }
    }
  }

  put_ramp(b);
}

static size_t desc_size(const char *text) { return 90u + strlen(text) + 1u; }

static void put_desc(struct buf *b, const char *text) {
  const size_t n = strlen(text) + 1u;

  put_str(b, "desc");
  put_zeros(b, 4);
  put_u32(b, (uint32_t)n);
  put(b, text, n);
  put_zeros(b, 8);  /* language code and Unicode length */
  put_zeros(b, 3);  /* ScriptCode: code and length      */
  put_zeros(b, 67); /* Macintosh description            */
}

static void put_text(struct buf *b, const char *text) {
  put_str(b, "text");
  put_zeros(b, 4);
  put(b, text, strlen(text) + 1u);
}

static void put_xyz(struct buf *b, const double xyz[3]) {
  int i;

  put_str(b, "XYZ ");
  put_zeros(b, 4);
  for (i = 0; i < 3; i++)
    put_s15f16(b, xyz[i]);
}

/* --- Assembly ------------------------------------------------------------- */

#define NTAGS 9

static void describe(char *dst, size_t n, double s) {
  snprintf(dst, n, "Sway vibrance %.2f", s);
}

size_t icc_size(double s, int grid) {
  char desc[64];
  size_t body;

  describe(desc, sizeof desc, s);

  body = pad4(desc_size(desc)) + pad4(8u + strlen(CPRT_TEXT) + 1u) + pad4(20u) +
         2u * pad4(lut16_size(grid));

  return 128u + 4u + 12u * NTAGS + body;
}

unsigned char *icc_build(double s, int grid, size_t *size) {
  const size_t lut = lut16_size(grid);
  const size_t total = icc_size(s, grid);
  /* The three A2B tags share a single table, and the three B2A tags
   * another: that way the file holds two tables instead of six. */
  const size_t off_desc = 128u + 4u + 12u * NTAGS;
  size_t off_cprt, off_wtpt, off_a2b, off_b2a;
  char desc[64];
  struct buf b;

  describe(desc, sizeof desc, s);

  off_cprt = off_desc + pad4(desc_size(desc));
  off_wtpt = off_cprt + pad4(8u + strlen(CPRT_TEXT) + 1u);
  off_a2b = off_wtpt + pad4(20u);
  off_b2a = off_a2b + pad4(lut);

  b.p = malloc(total);
  if (!b.p)
    return NULL;
  b.len = 0;
  b.cap = total;

  /* Header: exactly 128 bytes. */
  put_u32(&b, (uint32_t)total);
  put_str(&b, "lcms"); /* preferred CMM        */
  put_u8(&b, 2);       /* version 2.4.0        */
  put_u8(&b, 0x40);
  put_zeros(&b, 2);
  put_str(&b, "mntr"); /* class: monitor       */
  put_str(&b, "RGB "); /* data space           */
  put_str(&b, "Lab "); /* PCS                  */
  put_zeros(&b, 12);   /* date and time        */
  put_str(&b, "acsp");
  put_zeros(&b, 24);            /* platform, flags, manufacturer,
                                 * model and attributes */
  put_zeros(&b, 4);             /* intent: perceptual   */
  put_s15f16(&b, color_d50[0]); /* PCS illuminant       */
  put_s15f16(&b, color_d50[1]);
  put_s15f16(&b, color_d50[2]);
  put_zeros(&b, 4);  /* creator              */
  put_zeros(&b, 44); /* profile ID and reserved */
  assert(b.len == 128);

  /* Tag table. */
  put_u32(&b, NTAGS);
  put_str(&b, "desc");
  put_u32(&b, (uint32_t)off_desc);
  put_u32(&b, (uint32_t)desc_size(desc));
  put_str(&b, "cprt");
  put_u32(&b, (uint32_t)off_cprt);
  put_u32(&b, (uint32_t)(8u + strlen(CPRT_TEXT) + 1u));
  put_str(&b, "wtpt");
  put_u32(&b, (uint32_t)off_wtpt);
  put_u32(&b, 20);
  put_str(&b, "A2B0");
  put_u32(&b, (uint32_t)off_a2b);
  put_u32(&b, (uint32_t)lut);
  put_str(&b, "A2B1");
  put_u32(&b, (uint32_t)off_a2b);
  put_u32(&b, (uint32_t)lut);
  put_str(&b, "A2B2");
  put_u32(&b, (uint32_t)off_a2b);
  put_u32(&b, (uint32_t)lut);
  put_str(&b, "B2A0");
  put_u32(&b, (uint32_t)off_b2a);
  put_u32(&b, (uint32_t)lut);
  put_str(&b, "B2A1");
  put_u32(&b, (uint32_t)off_b2a);
  put_u32(&b, (uint32_t)lut);
  put_str(&b, "B2A2");
  put_u32(&b, (uint32_t)off_b2a);
  put_u32(&b, (uint32_t)lut);
  assert(b.len == off_desc);

  /* Body, in the same order as the table; every tag aligned to 4. */
  put_desc(&b, desc);
  put_zeros(&b, off_cprt - b.len);
  assert(b.len == off_cprt);

  put_text(&b, CPRT_TEXT);
  put_zeros(&b, off_wtpt - b.len);
  assert(b.len == off_wtpt);

  put_xyz(&b, color_d50);
  assert(b.len == off_a2b);

  put_lut16(&b, grid, icc_a2b, s);
  put_zeros(&b, off_b2a - b.len);
  assert(b.len == off_b2a);

  put_lut16(&b, grid, icc_b2a, s);
  put_zeros(&b, total - b.len);
  assert(b.len == total);

  *size = total;
  return b.p;
}
