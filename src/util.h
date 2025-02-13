/*
meshfile - a simple C library for reading/writing 3D mesh file formats
Copyright (C) 2025  John Tsiombikas <nuclear@mutantstargoat.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef UTIL_H_
#define UTIL_H_

#include "meshfile.h"

#if __STDC_VERSION__ >= 199901L
#include <stdint.h>
#else
#include <inttypes.h>
#endif

long mf_calc_b64_size(const char *s);
void *mf_b64decode(const char *str, void *buf, long *bufsz);

void mf_cross(mf_vec3 *dest, const mf_vec3 *a, const mf_vec3 *b);
void mf_normalize(mf_vec3 *v);
void mf_transform(mf_vec3 *dest, const mf_vec3 *v, const float *m);
void mf_transform_dir(mf_vec3 *dest, const mf_vec3 *v, const float *m);
void mf_mult_matrix(float *dest, const float *a, const float *b);
void mf_id_matrix(float *m);
void mf_trans_matrix(float *m, const mf_vec3 *v);
void mf_scale_matrix(float *m, const mf_vec3 *v);
void mf_quat_matrix(float *m, const mf_vec4 *q);
void mf_prs_matrix(float *mat, const mf_vec3 *p, const mf_vec4 *r, const mf_vec3 *s);

int mf_inverse_matrix(float *inv, const float *mat);
void mf_transpose_matrix(float *dest, const float *m);
void mf_print_matrix(const float *m);

#endif	/* UTIL_H_ */
