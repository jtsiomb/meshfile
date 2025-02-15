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
#ifndef MFPRIV_H_
#define MFPRIV_H_

#include "meshfile.h"
#include "rbtree.h"
#include "util.h"

#ifdef __GNUC__
#define PACKED	__attribute__((packed))
#else
#define PACKED
#endif

int mf_load_obj(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_obj(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_load_jtf(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_jtf(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_load_gltf(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_gltf(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_load_3ds(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_3ds(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_load_stl(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_stl(const struct mf_meshfile *mf, const struct mf_userio *io);

struct mf_meshfile {
	char *name;
	char *dirname;
	struct mf_mesh **meshes;
	struct mf_material **mtl;
	struct mf_node **nodes, **topnodes;
	mf_aabox aabox;

	struct rbtree *assetpath;
	unsigned int flags;
};

struct filefmt {
	int fmt;
	const char *suffixes[32];

	int (*load)(struct mf_meshfile*, const struct mf_userio*);
	int (*save)(const struct mf_meshfile*, const struct mf_userio*);
};

extern struct filefmt filefmt[MF_NUM_FMT];


int mf_fgetc(const struct mf_userio *io);
char *mf_fgets(char *buf, int sz, const struct mf_userio *io);
int mf_fputc(int c, const struct mf_userio *io);
int mf_fputs(const char *s, const struct mf_userio *io);
int mf_fprintf(const struct mf_userio *io, const char *fmt, ...);

int mf_strcasecmp(const char *a, const char *b);

#define TARGET_BIGEND		(*(uint16_t*)"ab" == 0x6162)
#define TARGET_LITEND		(*(uint16_t*)"ab" == 0x6261)

#define BSWAP16(x)		((x) = (((uint16_t)(x) >> 8) | ((uint16_t)(x) << 8)))
#define BSWAP32(x)		((x) = ((uint32_t)(x) >> 24) | ((uint32_t)(x) << 24) | \
			(((uint32_t)(x) >> 8) & 0xff00) | (((uint32_t)(x) << 8) & 0xff0000))
#define BSWAPFLT(x)		BSWAP32(*(uint32_t*)&(x))

#define CONV_LE16(x)	do if(TARGET_BIGEND) BSWAP16(x); while(0)
#define CONV_LE32(x)	do if(TARGET_BIGEND) BSWAP32(x); while(0)
#define CONV_LEFLT(x)	do if(TARGET_BIGEND) BSWAPFLT(x); while(0)

#endif	/* MFPRIV_H_ */
