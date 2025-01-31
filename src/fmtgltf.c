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
#include <ctype.h>
#include "mfpriv.h"
#include "json.h"
#include "dynarr.h"
#include "util.h"

struct buffer {
	unsigned long size;
	unsigned char *data;
};

struct bufview {
	int bufidx;
	unsigned long offs, len;
	unsigned int stride;
};

struct accessor {
	int bvidx;
	int nelem, type;
	unsigned long offs, count;
};

struct image {
	char *fname;
};

struct gltf_file {
	struct buffer *buffers;
	struct bufview *bufviews;
	struct accessor *accessors;
	struct image *images;
};

static struct mf_material *read_material(struct mf_meshfile *g, struct json_obj *jmtl);
static int read_data(struct mf_meshfile *mf, void *buf, unsigned long sz, const char *str,
		const struct mf_userio *io);
static int read_image(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbuf,
		const struct mf_userio *io);
static int read_buffer(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbuf,
		const struct mf_userio *io);

static int jarr_to_vec(struct json_arr *jarr, mf_vec4 *vec);
static int jval_to_vec(struct json_value *jval, mf_vec4 *vec);

int mf_load_gltf(struct mf_meshfile *mf, const struct mf_userio *io)
{
	int res = -1;
	long i, filesz;
	char *filebuf;
	struct json_obj root;
	struct json_value *jval;
	struct json_item *jitem;
	struct mf_material *mtl;
	struct gltf_file gltf = {0};

	if(!(filebuf = malloc(4096))) {
		fprintf(stderr, "mf_load: failed to allocate file buffer\n");
		return -1;
	}
	filesz = io->read(io->file, filebuf, 4096);
	if(filesz < 2) {
		free(filebuf);
		return -1;
	}
	for(i=0; i<filesz; i++) {
		if(!isspace(filebuf[i])) {
			if(filebuf[i] != '{') {
				free(filebuf);
				return -1;		/* not json */
			}
			break;
		}
	}
	free(filebuf);

	/* alright, it looks like json, load into memory and parse it to continue */
	filesz = io->seek(io->file, 0, MF_SEEK_END);
	io->seek(io->file, 0, MF_SEEK_SET);
	if(!(filebuf = malloc(filesz + 1))) {
		fprintf(stderr, "mf_load: failed to load file into memory\n");
		return -1;
	}
	if(io->read(io->file, filebuf, filesz) != filesz) {
		fprintf(stderr, "mf_load: EOF while reading file\n");
		free(filebuf);
		return -1;
	}
	filebuf[filesz] = 0;

	json_init_obj(&root);
	if(json_parse(&root, filebuf) == -1) {
		free(filebuf);
		return -1;
	}
	free(filebuf);

	/* a valid gltf file needs to have an "asset" node with a version number */
	if(!(jval = json_lookup(&root, "asset.version"))) {
		json_destroy_obj(&root);
		return -1;
	}

	/* initialize the dynamic arrays in the gltf structure */
	gltf.images = mf_dynarr_alloc(0, sizeof *gltf.images);
	gltf.buffers = mf_dynarr_alloc(0, sizeof *gltf.buffers);
	gltf.bufviews = mf_dynarr_alloc(0, sizeof *gltf.bufviews);
	gltf.accessors = mf_dynarr_alloc(0, sizeof *gltf.accessors);
	if(!gltf.images || !gltf.buffers || !gltf.bufviews || !gltf.accessors) {
		fprintf(stderr, "mf_load: failed to allocate dynamic array\n");
		goto end;
	}

	/* read all images */
	if((jitem = json_find_item(&root, "images"))) {
		if(jitem->val.type != JSON_ARR) {
			fprintf(stderr, "mf_load: gltf images is not an array!\n");
			goto skipimg;
		}

		for(i=0; i<jitem->val.arr.size; i++) {
			jval = jitem->val.arr.val + i;

			if(jval->type != JSON_OBJ) {
				fprintf(stderr, "mf_load: gltf image %ld is not a json object!\n", i);
				continue;
			}
			read_image(mf, &gltf, &jval->obj, io);
		}
	}
skipimg:

	/* read all materials */
	if((jitem = json_find_item(&root, "materials"))) {
		if(jitem->val.type != JSON_ARR) {
			fprintf(stderr, "mf_load: gltf materials value is not an array!\n");
			goto skipmtl;
		}

		for(i=0; i<jitem->val.arr.size; i++) {
			jval = jitem->val.arr.val + i;

			if(jval->type != JSON_OBJ) {
				fprintf(stderr, "mf_load: gltf material %ld is not a json object!\n", i);
				continue;
			}
			if((mtl = read_material(mf, &jval->obj))) {
				mf_add_material(mf, mtl);
			}
		}
	}
skipmtl:

	/* read all buffers */
	if((jitem = json_find_item(&root, "buffers"))) {
		if(jitem->val.type != JSON_ARR) {
			fprintf(stderr, "mf_load: gltf buffers is not an array!\n");
			goto skipbuf;
		}

		for(i=0; i<jitem->val.arr.size; i++) {
			jval = jitem->val.arr.val + i;

			if(jval->type != JSON_OBJ) {
				fprintf(stderr, "mf_load: gltf buffer %ld is not a json object!\n", i);
				continue;
			}
			read_buffer(mf, &gltf, &jval->obj, io);
		}
	}
skipbuf:

	res = 0;
end:
	mf_dynarr_free(gltf.images);
	mf_dynarr_free(gltf.buffers);
	mf_dynarr_free(gltf.bufviews);
	mf_dynarr_free(gltf.accessors);
	json_destroy_obj(&root);
	return res;
}


static struct mf_material *read_material(struct mf_meshfile *g, struct json_obj *jmtl)
{
	struct mf_material *mtl;
	const char *str;
	struct json_value *jval;
	float val;

	if(!(mtl = mf_alloc_mtl())) {
		fprintf(stderr, "read_material: failed to allocate material\n");
		return 0;
	}

	if((str = json_lookup_str(jmtl, "name", 0))) {
		mtl->name = strdup(str);
	}

	if((jval = json_lookup(jmtl, "pbrMetallicRoughness.baseColorFactor"))) {
		jval_to_vec(jval, &mtl->attr[MF_COLOR].val);
	}
	/* TODO textures */

	if((val = json_lookup_num(jmtl, "pbrMetallicRoughness.roughnessFactor", -1.0)) >= 0) {
		mtl->attr[MF_ROUGHNESS].val.x = val;
		mtl->attr[MF_SHININESS].val.x = (1.0f - val) * 100.0f + 1.0f;
	}
	if((val = json_lookup_num(jmtl, "pbrMetallicRoughness.metallicFactor", -1.0)) >= 0) {
		mtl->attr[MF_METALLIC].val.x = val;
	}
	if((jval = json_lookup(jmtl, "extensions.KHR_materials_specular.specularColorFactor"))) {
		jval_to_vec(jval, &mtl->attr[MF_SPECULAR].val);
	}
	if((val = json_lookup_num(jmtl, "extensions.KHR_materials_ior.ior", -1.0)) >= 0) {
		mtl->attr[MF_IOR].val.x = val;
	}
	/* TODO more attributes */

	return mtl;
}

static int read_data(struct mf_meshfile *mf, void *buf, unsigned long sz, const char *str,
		const struct mf_userio *io)
{
	void *file;

	if(memcmp(str, "data:", 5) == 0) {
		if(!(str = strstr(str, "base64,"))) {
			fprintf(stderr, "load_gltf: invalid embedded data, not base64\n");
			return -1;
		}
		str += 7;

		mf_b64decode(str, buf, (long*)&sz);

	} else {
		str = mf_find_asset(mf, str);
		if(!(file = io->open(str, "rb"))) {
			fprintf(stderr, "load_gltf: failed to load external data file: %s\n", str);
			return -1;
		}
		if(io->read(file, buf, sz) != sz) {
			fprintf(stderr, "load_gltf: unexpected EOF while reading data file: %s\n", str);
			return -1;
		}
	}
	return 0;
}

static int read_image(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jimg,
		const struct mf_userio *io)
{
	struct image img;
	const char *str;
	void *ptr;

	if(!(str = json_lookup_str(jimg, "uri", 0))) {
		fprintf(stderr, "load_gltf: image missing or invalid uri\n");
		return -1;
	}
	if(!(img.fname = strdup(str))) {
		fprintf(stderr, "load_gltf: failed to dup image name\n");
		return -1;
	}

	if(!(ptr = mf_dynarr_push(gltf->images, &img))) {
		fprintf(stderr, "load_gltf: failed to add image\n");
		return -1;
	}
	return 0;
}

static int read_buffer(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbuf,
		const struct mf_userio *io)
{
	struct buffer buf;
	struct json_value *jval;
	void *ptr;

	if((buf.size = json_lookup_int(jbuf, "byteLength", -1)) <= 0) {
		fprintf(stderr, "load_gltf: buffer missing or invalid byteLength\n");
		return -1;
	}

	if(!(jval = json_lookup(jbuf, "uri"))) {
		fprintf(stderr, "load_gltf: buffer missing or invalid uri\n");
		return -1;
	}

	if(!(buf.data = malloc(buf.size))) {
		fprintf(stderr, "load_gltf: failed to allocate %ld byte buffer\n", buf.size);
		return -1;
	}
	if(read_data(mf, buf.data, buf.size, jval->str, io) == -1) {
		free(buf.data);
		return -1;
	}
	if(!(ptr = mf_dynarr_push(gltf->buffers, &buf))) {
		fprintf(stderr, "load_gltf: failed to add buffer\n");
		free(buf.data);
		return -1;
	}
	gltf->buffers = ptr;
	return 0;
}

int mf_save_gltf(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	return -1;
}

static int jarr_to_vec(struct json_arr *jarr, mf_vec4 *vec)
{
	int i;
	float *vptr = &vec->x;

	if(jarr->size < 3 || jarr->size > 4) {
		return -1;
	}

	for(i=0; i<4; i++) {
		if(i >= jarr->size) {
			vptr[i] = 0;
			continue;
		}
		if(jarr->val[i].type != JSON_NUM) {
			return -1;
		}
		vptr[i] = jarr->val[i].num;
	}
	return jarr->size;
}

static int jval_to_vec(struct json_value *jval, mf_vec4 *vec)
{
	if(jval->type != JSON_ARR) return -1;
	return jarr_to_vec(&jval->arr, vec);
}

