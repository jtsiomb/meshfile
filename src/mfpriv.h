/*
meshfile - a simple C library for reading/writing 3D mesh file formats
Copyright (C) 2024  John Tsiombikas <nuclear@mutantstargoat.com>

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

struct mf_meshfile {
	char *name;
	char *dirname;
	struct mf_mesh **meshes;
	struct mf_material **mtl;
	mf_aabox aabox;

	struct rbtree *assetpath;
};

int mf_load_obj(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_obj(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_fgetc(const struct mf_userio *io);
char *mf_fgets(char *buf, int sz, const struct mf_userio *io);
int mf_fputs(const char *s, const struct mf_userio *io);
int mf_fprintf(const struct mf_userio *io, const char *fmt, ...);

#endif	/* MFPRIV_H_ */
