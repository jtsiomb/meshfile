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
#include "mfpriv.h"
#include "dynarr.h"
#include "util.h"

enum {
	CID_VERSION			= 0x0002,
	CID_RGBF			= 0x0010,
	CID_RGB,
	CID_RGB_GAMMA,
	CID_RGBF_GAMMA,
	CID_PERCENT_INT		= 0x0030,
	CID_PERCENT_FLT,
	CID_MAIN			= 0x4d4d,
	CID_3DEDITOR		= 0x3d3d,
	CID_ONEUNIT			= 0x0100,
	CID_MESHVER			= 0x3d3e,
	CID_OBJECT			= 0x4000,
	CID_TRIMESH			= 0x4100,
	CID_VERTLIST		= 0x4110,
	CID_FACEDESC		= 0x4120,
	CID_FACEMTL			= 0x4130,
	CID_UVLIST			= 0x4140,
	CID_SMOOTHLIST		= 0x4150,
	CID_LOCALCOORD		= 0x4160,
	CID_LIGHT			= 0x4600,
	CID_SPOTLT			= 0x4610,
	CID_CAMERA			= 0x4700,
	CID_MATERIAL		= 0xafff,
	CID_MTL_NAME		= 0xa000,
	CID_MTL_AMBIENT		= 0xa010,
	CID_MTL_DIFFUSE		= 0xa020,
	CID_MTL_SPECULAR	= 0xa030,
	CID_MTL_SHININESS	= 0xa040,
	CID_MTL_SHINSTR		= 0xa041,
	CID_MTL_TRANSMIT	= 0xa050,
	CID_MTL_SELFILLUM	= 0xa084,
	CID_MTL_TEXMAP1		= 0xa200,
	CID_MTL_TEXMAP2		= 0xa33a,
	CID_MTL_ALPHAMAP	= 0xa210,
	CID_MTL_BUMPMAP		= 0xa230,
	CID_MTL_SHINMAP		= 0xa33c,
	CID_MTL_SPECMAP		= 0xa204,
	CID_MTL_REFLMAP		= 0xa220,
	CID_MAP_FILENAME	= 0xa300,
	CID_MAP_PARAM		= 0xa351,
	CID_MAP_VSCALE		= 0xa354,
	CID_MAP_USCALE		= 0xa356,
	CID_MAP_UOFFS		= 0xa358,
	CID_MAP_VOFFS		= 0xa35a,
	CID_MAP_UVROT		= 0xa35c
};

#define CHDR_SIZE	6

struct chunk {
	uint16_t id;
	uint32_t len;

	long fpos, endpos;
};


static int read_material(struct mf_meshfile *mf, struct chunk *par, const struct mf_userio *io);
static int read_object(struct mf_meshfile *mf, struct chunk *par, const struct mf_userio *io);
static int read_trimesh(struct mf_mesh *mesh, struct mf_node *node, struct chunk *par, const struct mf_userio *io);
static int read_color(mf_vec4 *col, struct chunk *par, const struct mf_userio *io);
static int read_percent(float *retval, struct chunk *par, const struct mf_userio *io);
static int read_str(char *buf, int bufsz, struct chunk *par, const struct mf_userio *io);

static int read_chunk(struct chunk *ck, struct chunk *par, const struct mf_userio *io);
static void skip_chunk(struct chunk *ck, const struct mf_userio *io);


int mf_load_3ds(struct mf_meshfile *mf, const struct mf_userio *io)
{
	struct chunk ck, root;

	if(read_chunk(&root, 0, io) == -1 || root.id != CID_MAIN) {
		return -1;
	}

	while(read_chunk(&ck, &root, io) != -1) {
		switch(ck.id) {
		case CID_3DEDITOR:
			break;

		case CID_MATERIAL:
			if(read_material(mf, &ck, io) == -1) {
				return -1;
			}
			break;

		case CID_OBJECT:
			if(read_object(mf, &ck, io) == -1) {
				return -1;
			}
			break;

		default:
			skip_chunk(&ck, io);
		}
	}

	return 0;
}

static int read_material(struct mf_meshfile *mf, struct chunk *par, const struct mf_userio *io)
{
	int datalen;
	struct chunk ck;
	struct mf_material *mtl;
	float shin = 0.0f, shinstr = 1.0f;
	float selfillum = 0.0f;

	if(!(mtl = mf_alloc_mtl())) {
		fprintf(stderr, "load_3ds: failed to allocate material\n");
		return -1;
	}

	while(read_chunk(&ck, par, io) != -1) {
		datalen = ck.len - CHDR_SIZE;

		switch(ck.id) {
		case CID_MTL_NAME:
			if(!(mtl->name = malloc(datalen))) {
				goto err;
			}
			if(io->read(io->file, mtl->name, datalen) < datalen) {
				goto rdfail;
			}
			mtl->name[datalen - 1] = 0;
			break;

		case CID_MTL_DIFFUSE:
			if(read_color(&mtl->attr[MF_COLOR].val, &ck, io) == -1) {
				goto rdfail;
			}
			break;

		case CID_MTL_SPECULAR:
			if(read_color(&mtl->attr[MF_SPECULAR].val, &ck, io) == -1) {
				goto rdfail;
			}
			break;

		case CID_MTL_SELFILLUM:
			if(read_percent(&selfillum, &ck, io) == -1) {
				goto rdfail;
			}
			break;

		case CID_MTL_SHININESS:
			if(read_percent(&shin, &ck, io) == -1) {
				goto rdfail;
			}
			break;

		case CID_MTL_SHINSTR:
			if(read_percent(&shinstr, &ck, io) == -1) {
				goto rdfail;
			}
			break;

		default:
			skip_chunk(&ck, io);
		}
	}

	mtl->attr[MF_SHININESS].val.x = shin * shinstr * 128.0f;
	mtl->attr[MF_EMISSIVE].val.x = mtl->attr[MF_COLOR].val.x * selfillum;
	mtl->attr[MF_EMISSIVE].val.y = mtl->attr[MF_COLOR].val.y * selfillum;
	mtl->attr[MF_EMISSIVE].val.z = mtl->attr[MF_COLOR].val.z * selfillum;

	if(mf_add_material(mf, mtl) == -1) {
		fprintf(stderr, "load_3ds: failed to add material\n");
		goto err;
	}
	return 0;
rdfail:
	fprintf(stderr, "load_3ds: failed to read material property\n");
err:
	mf_free_mtl(mtl);
	return -1;
}

static int read_object(struct mf_meshfile *mf, struct chunk *par, const struct mf_userio *io)
{
	struct chunk ck;
	struct mf_mesh *mesh;
	struct mf_node *node;
	char buf[128];

	if(!(mesh = mf_alloc_mesh())) {
		fprintf(stderr, "load_3ds: failed to allocate mesh\n");
		return -1;
	}
	if(!(node = mf_alloc_node())) {
		fprintf(stderr, "load_3ds: failed to allocate node\n");
		mf_free_mesh(mesh);
		return -1;
	}

	if(read_str(buf, sizeof buf, par, io) == -1 || !(mesh->name = strdup(buf)) ||
			!(node->name = strdup(buf))) {
		goto err;
	}

	while(read_chunk(&ck, par, io) != -1) {
		switch(ck.id) {
		case CID_TRIMESH:
			if(read_trimesh(mesh, node, &ck, io) == -1) {
				goto err;
			}
			break;

		default:
			skip_chunk(&ck, io);
		}
	}

	if(!mesh->num_verts) {
		goto err;
	}

	if(mf_node_add_mesh(node, mesh) == -1) {
		fprintf(stderr, "load_3ds: failed to add mesh to node\n");
		goto err;
	}
	if(mf_add_mesh(mf, mesh) == -1) {
		fprintf(stderr, "load_3ds: failed to add mesh\n");
		goto err;
	}
	if(mf_add_node(mf, node) == -1) {
		fprintf(stderr, "load_3ds: failed to add node\n");
		goto err;
	}
	return 0;
err:
	skip_chunk(par, io);
	mf_free_mesh(mesh);
	mf_free_node(node);
	return -1;
}

static int read_word(uint16_t *val, struct chunk *par, const struct mf_userio *io)
{
	long fpos = io->seek(io->file, 0, MF_SEEK_CUR);
	if(fpos + 2 > par->endpos) {
		return -1;
	}
	if(io->read(io->file, val, 2) < 2) {
		return -1;
	}
	CONV_LE16(*val);
	return 0;
}

static int read_float(float *val, struct chunk *par, const struct mf_userio *io)
{
	long fpos = io->seek(io->file, 0, MF_SEEK_CUR);
	if(fpos + 4 > par->endpos) {
		return -1;
	}
	if(io->read(io->file, val, 4) < 4) {
		return -1;
	}
	CONV_LEFLT(*val);
	return 0;
}

static int read_trimesh(struct mf_mesh *mesh, struct mf_node *node, struct chunk *par, const struct mf_userio *io)
{
	struct chunk ck;
	mf_vec3 vec;
	uint16_t nverts, nfaces, vidx[3];
	int i, j;
	float *mptr;

	while(read_chunk(&ck, par, io) != -1) {
		switch(ck.id) {
		case CID_VERTLIST:
			if(read_word(&nverts, &ck, io) == -1) {
				fprintf(stderr, "load_3ds: failed to read vertex count\n");
				goto err;
			}
			for(i=0; i<(int)nverts; i++) {
				if(read_float(&vec.x, &ck, io) == -1 ||
						read_float(&vec.y, &ck, io) == -1 ||
						read_float(&vec.z, &ck, io) == -1) {
					fprintf(stderr, "load_3ds: failed to read vertex\n");
					goto err;
				}
				if(mf_add_vertex(mesh, vec.x, vec.y, vec.z) == -1) {
					fprintf(stderr, "load_3ds: failed to add vertex\n");
					goto err;
				}
			}
			break;

		case CID_UVLIST:
			if(read_word(&nverts, &ck, io) == -1) {
				fprintf(stderr, "load_3ds: failed to read texture coordinate count\n");
				goto err;
			}
			for(i=0; i<(int)nverts; i++) {
				if(read_float(&vec.x, &ck, io) == -1 || read_float(&vec.y, &ck, io) == -1) {
					fprintf(stderr, "load_3ds: failed to read texture coordinates\n");
					goto err;
				}
				if(mf_add_texcoord(mesh, vec.x, vec.y) == -1) {
					fprintf(stderr, "load_3ds: failed to add texcoord\n");
					goto err;
				}
			}
			break;

		case CID_FACEDESC:
			if(read_word(&nfaces, &ck, io) == -1) {
				fprintf(stderr, "load_3ds: failed to read face count\n");
				goto err;
			}
			for(i=0; i<(int)nfaces; i++) {
				if(read_word(vidx, &ck, io) == -1 || read_word(vidx + 1, &ck, io) == -1 ||
						read_word(vidx + 2, &ck, io) == -1) {
					fprintf(stderr, "load_3ds: failed to read face\n");
					goto err;
				}
				if(mf_add_triangle(mesh, vidx[0], vidx[1], vidx[2]) == -1) {
					fprintf(stderr, "load_3ds: failed to add face\n");
					goto err;
				}
				read_word(vidx, &ck, io);	/* ignore edge flags */
			}
			break;

		case CID_LOCALCOORD:
			mptr = node->matrix;
			for(i=0; i<4; i++) {
				for(j=0; j<3; j++) {
					if(read_float(mptr++, &ck, io) == -1) {
						fprintf(stderr, "load_3ds: failed to read mesh matrix\n");
						goto err;
					}
				}
				*mptr++ = i < 3 ? 0 : 1;
			}
		}
	}

	mf_calc_normals(mesh);
	return 0;
err:
	skip_chunk(par, io);
	return -1;
}


static int read_color(mf_vec4 *col, struct chunk *par, const struct mf_userio *io)
{
	struct chunk ck;
	unsigned char rgb[3];
	int res = -1;

	if(read_chunk(&ck, par, io) == -1) {
		return -1;
	}

	switch(ck.id) {
	case CID_RGB:
	case CID_RGB_GAMMA:
		if(io->read(io->file, rgb, 3) < 3) {
			return -1;
		}
		col->x = (float)rgb[0] / 255.0f;
		col->y = (float)rgb[1] / 255.0f;
		col->z = (float)rgb[2] / 255.0f;
		res = 0;
		break;

	case CID_RGBF:
	case CID_RGBF_GAMMA:
		if(io->read(io->file, col, 3 * sizeof(float)) < 3 * sizeof(float)) {
			return -1;
		}
		if(TARGET_BIGEND) {
			BSWAPFLT(col->x);
			BSWAPFLT(col->y);
			BSWAPFLT(col->z);
		}
		res = 0;
		break;

	default:
		skip_chunk(&ck, io);
	}
	return res;
}

static int read_percent(float *retval, struct chunk *par, const struct mf_userio *io)
{
	struct chunk ck;
	uint16_t ival;
	int res = -1;

	if(read_chunk(&ck, par, io) == -1) {
		return -1;
	}

	switch(ck.id) {
	case CID_PERCENT_INT:
		if(io->read(io->file, &ival, 2) < 2) {
			return -1;
		}
		CONV_LE16(ival);
		*retval = ival / 100.0f;
		res = 0;
		break;

	case CID_PERCENT_FLT:
		if(io->read(io->file, retval, sizeof(float)) < sizeof(float)) {
			return -1;
		}
		CONV_LEFLT(*retval);
		*retval /= 100.0f;
		res = 0;
		break;

	default:
		skip_chunk(&ck, io);
	}
	return res;
}

static int read_str(char *buf, int bufsz, struct chunk *par, const struct mf_userio *io)
{
	int c;
	long fpos = io->seek(io->file, 0, MF_SEEK_CUR);
	char *endp = buf + bufsz - 1;

	while(fpos++ < par->endpos && (c = mf_fgetc(io)) != -1 && c) {
		if(buf < endp) {
			*buf++ = c;
		}
	}
	*buf = 0;
	return 0;
}


static int read_chunk(struct chunk *ck, struct chunk *par, const struct mf_userio *io)
{
	ck->fpos = io->seek(io->file, 0, MF_SEEK_CUR);
	if(par && ck->fpos + CHDR_SIZE > par->endpos) {
		return -1;
	}
	if(io->read(io->file, &ck->id, sizeof ck->id) < sizeof ck->id) {
		return -1;
	}
	if(io->read(io->file, &ck->len, sizeof ck->len) < sizeof ck->len) {
		return -1;
	}
	CONV_LE16(ck->id);
	CONV_LE32(ck->len);
	ck->endpos = ck->fpos + ck->len;
	return 0;
}

static void skip_chunk(struct chunk *ck, const struct mf_userio *io)
{
	io->seek(io->file, ck->endpos, MF_SEEK_SET);
}


int mf_save_3ds(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	return -1;
}
