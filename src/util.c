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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"

static int b64bits(int c);

long mf_calc_b64_size(const char *s)
{
	long len = strlen(s);
	const char *end = s + len;
	while(end > s && *--end == '=') len--;
	return len * 3 / 4;
}

void *mf_b64decode(const char *str, void *buf, long *bufsz)
{
	unsigned char *dest, *end;
	unsigned char acc;
	int bits;
	long sz;
	unsigned int gidx;

	if(buf) {
		sz = *bufsz;
	} else {
		sz = mf_calc_b64_size(str);
		if(!(buf = malloc(sz))) {
			return 0;
		}
		if(bufsz) *bufsz = sz;
	}
	dest = buf;
	end = (unsigned char*)buf + sz;

	sz = 0;
	gidx = 0;
	acc = 0;
	while(*str) {
		if((bits = b64bits(*str++)) == -1) {
			continue;
		}

		switch(gidx++ & 3) {
		case 0:
			acc = bits << 2;
			break;
		case 1:
			if(dest < end) *dest = acc | (bits >> 4);
			dest++;
			acc = bits << 4;
			break;
		case 2:
			if(dest < end) *dest = acc | (bits >> 2);
			dest++;
			acc = bits << 6;
			break;
		case 3:
			if(dest < end) *dest = acc | bits;
			dest++;
		default:
			break;
		}
	}

	if(gidx & 3) {
		if(dest < end) *dest = acc;
		dest++;
	}

	if(bufsz) *bufsz = dest - (unsigned char*)buf;
	return buf;
}


static int b64bits(int c)
{
	if(c >= 'A' && c <= 'Z') {
		return c - 'A';
	}
	if(c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	}
	if(c >= '0' && c <= '9') {
		return c - '0' + 52;
	}
	if(c == '+') return 62;
	if(c == '/') return 63;

	return -1;
}

void mf_vadd(mf_vec3 *dest, const mf_vec3 *a, const mf_vec3 *b)
{
	dest->x = a->x + b->x;
	dest->y = a->y + b->y;
	dest->z = a->z + b->z;
}

void mf_vsub(mf_vec3 *dest, const mf_vec3 *a, const mf_vec3 *b)
{
	dest->x = a->x - b->x;
	dest->y = a->y - b->y;
	dest->z = a->z - b->z;
}

float mf_dot(const mf_vec3 *a, const mf_vec3 *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

void mf_cross(mf_vec3 *dest, const mf_vec3 *a, const mf_vec3 *b)
{
	dest->x = a->y * b->z - a->z * b->y;
	dest->y = a->z * b->x - a->x * b->z;
	dest->z = a->x * b->y - a->y * b->x;
}

void mf_normalize(mf_vec3 *v)
{
	float len = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
	if(len > 0.0f) {
		float s = 1.0f / len;
		v->x *= s;
		v->y *= s;
		v->z *= s;
	}
}

void mf_transform(mf_vec3 *dest, const mf_vec3 *v, const float *m)
{
	float x = v->x * m[0] + v->y * m[4] + v->z * m[8] + m[12];
	float y = v->x * m[1] + v->y * m[5] + v->z * m[9] + m[13];
	dest->z = v->x * m[2] + v->y * m[6] + v->z * m[10] + m[14];
	dest->x = x;
	dest->y = y;
}

void mf_transform_dir(mf_vec3 *dest, const mf_vec3 *v, const float *m)
{
	float x = v->x * m[0] + v->y * m[4] + v->z * m[8];
	float y = v->x * m[1] + v->y * m[5] + v->z * m[9];
	dest->z = v->x * m[2] + v->y * m[6] + v->z * m[10];
	dest->x = x;
	dest->y = y;
}

void mf_mult_matrix(float *dest, const float *a, const float *b)
{
	int i, j;
	float res[16];
	float *resptr;
	const float *brow = b;

	if(dest == a || dest == b) {
		resptr = res;
	} else {
		resptr = dest;
	}

	for(i=0; i<4; i++) {
		for(j=0; j<4; j++) {
			*resptr++ = brow[0] * a[j] + brow[1] * a[4 + j] +
				brow[2] * a[8 + j] + brow[3] * a[12 + j];
		}
		brow += 4;
	}
	if(resptr == res + 16) {
		memcpy(dest, res, sizeof res);
	}
}

void mf_id_matrix(float *m)
{
	memset(m, 0, 16 * sizeof *m);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mf_trans_matrix(float *m, const mf_vec3 *v)
{
	mf_id_matrix(m);
	m[12] = v->x;
	m[13] = v->y;
	m[14] = v->z;
}

void mf_scale_matrix(float *m, const mf_vec3 *v)
{
	memset(m, 0, 16 * sizeof *m);
	m[0] = v->x;
	m[5] = v->y;
	m[10] = v->z;
	m[15] = 1.0f;
}

void mf_quat_matrix(float *m, const mf_vec4 *q)
{
	float xsq2 = 2.0f * q->x * q->x;
	float ysq2 = 2.0f * q->y * q->y;
	float zsq2 = 2.0f * q->z * q->z;
	float sx = 1.0f - ysq2 - zsq2;
	float sy = 1.0f - xsq2 - zsq2;
	float sz = 1.0f - xsq2 - ysq2;

	m[3] = m[7] = m[11] = m[12] = m[13] = m[14] = 0.0f;
	m[15] = 1.0f;

	m[0] = sx;
	m[1] = 2.0f * q->x * q->y + 2.0f * q->w * q->z;
	m[2] = 2.0f * q->z * q->x - 2.0f * q->w * q->y;
	m[4] = 2.0f * q->x * q->y - 2.0f * q->w * q->z;
	m[5] = sy;
	m[6] = 2.0f * q->y * q->z + 2.0f * q->w * q->x;
	m[8] = 2.0f * q->z * q->x + 2.0f * q->w * q->y;
	m[9] = 2.0f * q->y * q->z - 2.0f * q->w * q->x;
	m[10] = sz;
}

void mf_prs_matrix(float *mat, const mf_vec3 *p, const mf_vec4 *r, const mf_vec3 *s)
{
	float tmp[16];

	mf_quat_matrix(mat, r);
	mf_trans_matrix(tmp, p);
	mf_mult_matrix(mat, tmp, mat);
	mf_scale_matrix(tmp, s);
	mf_mult_matrix(mat, tmp, mat);
}

void mf_transpose_matrix(float *dest, const float *m)
{
	int i, j;
	for(i=0; i<4; i++) {
		for(j=0; j<i; j++) {
			int a = i * 4 + j;
			int b = j * 4 + i;
			float tmp = m[a];
			dest[a] = m[b];
			dest[b] = tmp;
		}
	}

	if(dest != m) {
		dest[0] = m[0];
		dest[5] = m[5];
		dest[10] = m[10];
		dest[15] = m[15];
	}
}

/* enough of cgmath for inverting matrices */
static void cgm_mupper3(float *m)
{
	m[3] = m[7] = m[11] = m[12] = m[13] = m[14] = 0.0f;
	m[15] = 1.0f;
}

static void cgm_msubmatrix(float *m, int row, int col)
{
	float orig[16];
	int i, j, subi, subj;

	memcpy(orig, m, sizeof orig);

	subi = 0;
	for(i=0; i<4; i++) {
		if(i == row) continue;

		subj = 0;
		for(j=0; j<4; j++) {
			if(j == col) continue;

			m[subi * 4 + subj++] = orig[i * 4 + j];
		}
		subi++;
	}

	cgm_mupper3(m);
}

static float cgm_msubdet(const float *m, int row, int col)
{
	float tmp[16];
	float subdet00, subdet01, subdet02;

	memcpy(tmp, m, sizeof tmp);
	cgm_msubmatrix(tmp, row, col);

	subdet00 = tmp[5] * tmp[10] - tmp[6] * tmp[9];
	subdet01 = tmp[4] * tmp[10] - tmp[6] * tmp[8];
	subdet02 = tmp[4] * tmp[9] - tmp[5] * tmp[8];

	return tmp[0] * subdet00 - tmp[1] * subdet01 + tmp[2] * subdet02;
}

static float cgm_mcofactor(const float *m, int row, int col)
{
	float min = cgm_msubdet(m, row, col);
	return (row + col) & 1 ? -min : min;
}

static float cgm_mdet(const float *m)
{
	return m[0] * cgm_msubdet(m, 0, 0) - m[1] * cgm_msubdet(m, 0, 1) +
		m[2] * cgm_msubdet(m, 0, 2) - m[3] * cgm_msubdet(m, 0, 3);
}

int mf_inverse_matrix(float *inv, const float *m)
{
	int i, j;
	float tmp[16];
	float inv_det;
	float det = cgm_mdet(m);
	if(det == 0.0f) return -1;
	inv_det = 1.0f / det;

	if(inv == m) {
		memcpy(tmp, m, sizeof tmp);
		m = tmp;
	}

	for(i=0; i<4; i++) {
		for(j=0; j<4; j++) {
			inv[i * 4 + j] = cgm_mcofactor(m, j, i) * inv_det;	/* transposed */
		}
	}
	return 0;
}


void mf_print_matrix(const float *m)
{
	int i;
	for(i=0; i<4; i++) {
		printf("%f %f %f %f\n", m[0], m[1], m[2], m[3]);
		m += 4;
	}
}
