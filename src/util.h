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

long mf_calc_b64_size(const char *s);
void *mf_b64decode(const char *str, void *buf, long *bufsz);

void mf_prs_matrix(float *mat, const mf_vec3 *p, const mf_vec4 *r, const mf_vec3 *s);

#endif	/* UTIL_H_ */
