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

enum {
	GLTF_BYTE =	5120,
	GLTF_UBYTE,
	GLTF_SHORT,
	GLTF_USHORT,
	GLTF_UINT = 5125,
	GLTF_FLOAT
};

enum {
	GLTF_POINTS,
	GLTF_LINES,
	GLTF_LINE_LOOP,
	GLTF_LINE_STRIP,
	GLTF_TRIANGLES,
	GLTF_TRIANGLE_STRIP,
	GLTF_TRIANGLE_FAN
};

enum {
	GLTF_NEAREST = 9728,
	GLTF_LINEAR,
	GLTF_NEAREST_MIPMAP_NEAREST = 9984,
	GLTF_LINEAR_MIPMAP_NEAREST,
	GLTF_NEAREST_MIPMAP_LINEAR,
	GLTF_LINEAR_MIPMAP_LINEAR
};

enum {
	GLTF_REPEAT = 10497,
	GLTF_CLAMP_TO_EDGE = 33071,
	GLTF_MIRRORED_REPEAT = 33648
};

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

struct sampler {
	int magfilt, minfilt;
	int uwrap, vwrap;
};

struct texture {
	int sampleridx;
	int imgidx;
};

struct node {
	int *cidx;
	int num_child;
	struct mf_node *mfnode;
};

struct gltf_file {
	int meshcount;

	struct buffer *buffers;
	struct bufview *bufviews;
	struct accessor *accessors;
	struct image *images;
	struct sampler *samplers;
	struct texture *textures;
	struct node *nodes;
};


static int read_data(struct mf_meshfile *mf, void *buf, unsigned long sz, const char *str,
		const struct mf_userio *io);

static int read_image(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jimg,
		const struct mf_userio *io);
static int read_sampler(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jimg,
		const struct mf_userio *io);
static int read_texture(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jtex,
		const struct mf_userio *io);
static int read_material(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jmtl,
		const struct mf_userio *io);
static int read_buffer(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbuf,
		const struct mf_userio *io);
static int read_bufview(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbv,
		const struct mf_userio *io);
static int read_accessor(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jacc,
		const struct mf_userio *io);
static int read_node(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jnode,
		const struct mf_userio *io);
static int read_mesh(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jprim,
		const struct mf_userio *io);

static int proc_node(struct node *nodes, int nidx);

static int jarr_to_vec(struct json_arr *jarr, mf_vec4 *vec);
static int jval_to_vec(struct json_value *jval, mf_vec4 *vec);

static struct {
	const char *arrname;
	int (*read_thing)(struct mf_meshfile*, struct gltf_file*, struct json_obj*, const struct mf_userio*);
} gltf_thing[] = {
	{"images", read_image},
	{"samplers", read_sampler},
	{"textures", read_texture},
	{"materials", read_material},
	{"buffers", read_buffer},
	{"bufferViews", read_bufview},
	{"accessors", read_accessor},
	{"meshes", read_mesh},
	{"nodes", read_node},
	{0, 0}
};


int mf_load_gltf(struct mf_meshfile *mf, const struct mf_userio *io)
{
	int res = -1;
	long i, j, filesz;
	int num_nodes, num_meshes;
	char *filebuf;
	struct json_obj root;
	struct json_value *jval;
	struct json_item *jitem;
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
	gltf.samplers = mf_dynarr_alloc(0, sizeof *gltf.samplers);
	gltf.textures = mf_dynarr_alloc(0, sizeof *gltf.textures);
	gltf.buffers = mf_dynarr_alloc(0, sizeof *gltf.buffers);
	gltf.bufviews = mf_dynarr_alloc(0, sizeof *gltf.bufviews);
	gltf.accessors = mf_dynarr_alloc(0, sizeof *gltf.accessors);
	gltf.nodes = mf_dynarr_alloc(0, sizeof *gltf.nodes);
	if(!gltf.images || !gltf.buffers || !gltf.bufviews || !gltf.accessors ||
			!gltf.samplers || !gltf.textures || !gltf.nodes) {
		fprintf(stderr, "mf_load: failed to allocate dynamic array\n");
		goto end;
	}

	/* read all the gltf structure things */
	for(i=0; gltf_thing[i].arrname; i++) {
		if((jitem = json_find_item(&root, gltf_thing[i].arrname))) {
			if(jitem->val.type != JSON_ARR) {
				fprintf(stderr, "mf_load: gltf %s is not an array!\n", gltf_thing[i].arrname);
				continue;
			}

			for(j=0; j<jitem->val.arr.size; j++) {
				jval = jitem->val.arr.val + j;

				if(jval->type != JSON_OBJ) {
					fprintf(stderr, "mf_load: gltf %s %ld is not a json object!\n",
							gltf_thing[i].arrname, j);
					continue;
				}
				gltf_thing[i].read_thing(mf, &gltf, &jval->obj, io);
			}
		}
	}

	/* clear all mesh index numbers from mesh udata */
	num_meshes = mf_num_meshes(mf);
	for(i=0; i<num_meshes; i++) {
		mf_get_mesh(mf, i)->udata = 0;
	}

	/* process nodes and construct hierarchy */
	num_nodes = mf_dynarr_size(gltf.nodes);
	for(i=0; i<num_nodes; i++) {
		proc_node(gltf.nodes, i);
	}
	for(i=0; i<num_nodes; i++) {
		mf_add_node(mf, gltf.nodes[i].mfnode);
		gltf.nodes[i].mfnode = 0;
	}

	res = 0;
end:
	mf_dynarr_free(gltf.images);
	mf_dynarr_free(gltf.samplers);
	mf_dynarr_free(gltf.textures);
	for(i=0; i<mf_dynarr_size(gltf.buffers); i++) {
		free(gltf.buffers[i].data);
	}
	mf_dynarr_free(gltf.buffers);
	mf_dynarr_free(gltf.bufviews);
	mf_dynarr_free(gltf.accessors);
	for(i=0; i<mf_dynarr_size(gltf.nodes); i++) {
		free(gltf.nodes[i].cidx);
		mf_free_node(gltf.nodes[i].mfnode);
	}
	mf_dynarr_free(gltf.nodes);
	json_destroy_obj(&root);
	return res;
}

static int proc_node(struct node *nodes, int nidx)
{
	int i, num_nodes = mf_dynarr_size(nodes);
	struct node *n = nodes + nidx;

	if(!n->cidx) return 0;

	for(i=0; i<n->num_child; i++) {
		if(n->cidx[i] >= num_nodes) {
			fprintf(stderr, "load_gltf: invalid child node reference: %d\n", n->cidx[i]);
			return -1;
		}
		if(mf_node_add_child(n->mfnode, nodes[n->cidx[i]].mfnode) == -1) {
			fprintf(stderr, "load_gltf: failed to add child node\n");
			return -1;
		}
	}
	return 0;
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
	gltf->images = ptr;
	return 0;
}

static int read_sampler(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jsam,
		const struct mf_userio *io)
{
	struct sampler sam;
	void *ptr;

	sam.uwrap = json_lookup_int(jsam, "wrapS", GLTF_REPEAT);
	sam.vwrap = json_lookup_int(jsam, "wrapT", GLTF_REPEAT);
	sam.magfilt = json_lookup_int(jsam, "magFilter", GLTF_LINEAR);
	sam.minfilt = json_lookup_int(jsam, "minFilter", GLTF_LINEAR_MIPMAP_LINEAR);

	if(!(ptr = mf_dynarr_push(gltf->samplers, &sam))) {
		fprintf(stderr, "load_gltf: failed to add sampler\n");
		return -1;
	}
	gltf->samplers = ptr;
	return 0;
}

static int read_texture(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jtex,
		const struct mf_userio *io)
{
	struct texture tex;
	void *ptr;

	if((tex.imgidx = json_lookup_int(jtex, "source", -1)) < 0 ||
			tex.imgidx >= mf_dynarr_size(gltf->images)) {
		fprintf(stderr, "load_gltf: missing or invalid texture source\n");
		return -1;
	}

	if((tex.sampleridx = json_lookup_int(jtex, "sampler", -1)) >= 0) {
		if(tex.sampleridx >= mf_dynarr_size(gltf->samplers)) {
			fprintf(stderr, "load_gltf: texture refers to invalid sampler: %d\n", tex.sampleridx);
			return -1;
		}
	}

	if(!(ptr = mf_dynarr_push(gltf->textures, &tex))) {
		fprintf(stderr, "load_gltf: failed to add texture\n");
		return -1;
	}
	gltf->textures = ptr;
	return 0;
}

static int get_texture(struct gltf_file *gltf, struct mf_texmap *tex, const char *basepath, struct json_obj *jmtl)
{
	static struct sampler defsam = {GLTF_LINEAR, GLTF_LINEAR_MIPMAP_LINEAR, GLTF_REPEAT, GLTF_REPEAT};
	int val;
	struct texture *gtex;
	struct image *img;
	struct sampler *sam;
	char *path;
	struct json_arr *jarr;

	if(!(path = malloc(strlen(basepath) + 64))) {
		fprintf(stderr, "load_gltf: failed to allocate path buffer\n");
		return -1;
	}
	sprintf(path, "%s.index", basepath);

	if((val = json_lookup_int(jmtl, path, -1)) < 0) {
		free(path);
		return -1;
	}
	if(val >= mf_dynarr_size(gltf->textures)) {
		fprintf(stderr, "load_gltf: material refers to invalid texture: %d\n", val);
		free(path);
		return -1;
	}
	gtex = gltf->textures + val;
	sam = gtex->sampleridx >= 0 ? gltf->samplers + gtex->sampleridx : &defsam;
	img = gltf->images + gtex->imgidx;

	if(!(tex->name = strdup(img->fname))) {
		fprintf(stderr, "load_gltf: failed to allocate texture name\n");
		free(path);
		return -1;
	}
	if(sam->minfilt == GLTF_NEAREST || sam->minfilt == GLTF_NEAREST_MIPMAP_NEAREST ||
			sam->minfilt == GLTF_NEAREST_MIPMAP_LINEAR || sam->magfilt == GLTF_NEAREST) {
		tex->ufilt = tex->vfilt = MF_TEX_NEAREST;
	} else {
		tex->ufilt = tex->vfilt = MF_TEX_LINEAR;
	}
	tex->uwrap = sam->uwrap == GLTF_CLAMP_TO_EDGE ? MF_TEX_CLAMP : MF_TEX_REPEAT;
	tex->vwrap = sam->vwrap == GLTF_CLAMP_TO_EDGE ? MF_TEX_CLAMP : MF_TEX_REPEAT;

	sprintf(path, "%s.extensions.KHR_texture_transform.offset", basepath);
	if((jarr = json_lookup_arr(jmtl, path, 0))) {
		if(jarr->size != 2 || jarr->val[0].type != JSON_NUM || jarr->val[1].type != JSON_NUM) {
			fprintf(stderr, "load_gltf: invalid texture transform.offset\n");
		} else {
			tex->offset.x = jarr->val[0].num;
			tex->offset.y = jarr->val[1].num;
			tex->offset.z = 0.0f;
		}
	}
	sprintf(path, "%s.extensions.KHR_texture_transform.scale", basepath);
	if((jarr = json_lookup_arr(jmtl, path, 0))) {
		if(jarr->size != 2 || jarr->val[0].type != JSON_NUM || jarr->val[1].type != JSON_NUM) {
			fprintf(stderr, "load_gltf: invalid texture transform.scale\n");
		} else {
			tex->scale.x = jarr->val[0].num;
			tex->scale.y = jarr->val[1].num;
			tex->scale.z = 1.0f;
		}
	}
	/* there's also KHR_texture_transform.rotation, but we don't support that for now */

	free(path);
	return 0;
}

/* TODO: KHR_materials_common */
static int read_material(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jmtl,
		const struct mf_userio *io)
{
	int i;
	struct mf_material *mtl;
	const char *str;
	struct json_value *jval;
	float val;
	static const char *texpaths[] = {
		"pbrMetallicRoughness.baseColorTexture",
		0, 0,
		"pbrMetallicRoughness.metallicRoughnessTexture",
		0,
		"emissiveTexture",
		0,
		"extensions.KHR_materials_transmission.transmissionTexture",
		0, 0,
		"normalTexture"
	};

	if(!(mtl = mf_alloc_mtl())) {
		fprintf(stderr, "read_material: failed to allocate material\n");
		return -1;
	}

	str = json_lookup_str(jmtl, "name", 0);
	mtl->name = strdup(str ? str : "unnamed material");

	if((jval = json_lookup(jmtl, "pbrMetallicRoughness.baseColorFactor"))) {
		jval_to_vec(jval, &mtl->attr[MF_COLOR].val);
	}
	if((val = json_lookup_num(jmtl, "pbrMetallicRoughness.roughnessFactor", -1.0)) >= 0) {
		mtl->attr[MF_ROUGHNESS].val.x = val;
		mtl->attr[MF_SHININESS].val.x = (1.0f - val) * 100.0f + 1.0f;
	}
	if((val = json_lookup_num(jmtl, "pbrMetallicRoughness.metallicFactor", -1.0)) >= 0) {
		mtl->attr[MF_METALLIC].val.x = val;
	}
	if((jval = json_lookup(jmtl, "emissiveFactor"))) {
		jval_to_vec(jval, &mtl->attr[MF_EMISSIVE].val);
	}
	if((jval = json_lookup(jmtl, "extensions.KHR_materials_specular.specularColorFactor"))) {
		jval_to_vec(jval, &mtl->attr[MF_SPECULAR].val);
	}
	if((val = json_lookup_num(jmtl, "extensions.KHR_materials_ior.ior", -1.0)) >= 0) {
		mtl->attr[MF_IOR].val.x = val;
	}
	if((val = json_lookup_num(jmtl, "extensions.KHR_materials_transmission.transmissionFactor", -1.0)) >= 0) {
		mtl->attr[MF_TRANSMIT].val.x = val;
	}

	for(i=0; i<MF_NUM_MTLATTR; i++) {
		if(texpaths[i]) {
			get_texture(gltf, &mtl->attr[i].map, texpaths[i], jmtl);
		}
	}

	mf_add_material(mf, mtl);
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

static int read_bufview(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jbv,
		const struct mf_userio *io)
{
	struct bufview bv;
	void *ptr;
	long val;

	if((bv.bufidx = json_lookup_int(jbv, "buffer", -1)) < 0 || bv.bufidx >= mf_dynarr_size(gltf->buffers)) {
		fprintf(stderr, "load_gltf: bufferview missing or invalid buffer index\n");
		return -1;
	}
	if((val = json_lookup_int(jbv, "byteLength", -1)) < 0) {
		fprintf(stderr, "load_gltf: bufferview missing or invalid buffer length\n");
		return -1;
	}
	bv.len = val;
	bv.offs = json_lookup_int(jbv, "byteOffset", 0);
	bv.stride = json_lookup_int(jbv, "byteStride", 0);

	if(!(ptr = mf_dynarr_push(gltf->bufviews, &bv))) {
		fprintf(stderr, "load_gltf: failed to add buffer view\n");
		return -1;
	}
	gltf->bufviews = ptr;
	return 0;
}

static int parse_elem_type(const char *str)
{
	if(!str || !*str) return -1;
	if(strcmp(str, "SCALAR") == 0) return 1;
	if(strcmp(str, "VEC2") == 0) return 2;
	if(strcmp(str, "VEC3") == 0) return 3;
	if(strcmp(str, "VEC4") == 0) return 4;
	if(strcmp(str, "MAT2") == 0) return 4;
	if(strcmp(str, "MAT3") == 0) return 9;
	if(strcmp(str, "MAT4") == 0) return 16;
	return -1;
}

static int read_accessor(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jacc,
		const struct mf_userio *io)
{
	struct accessor acc;
	void *ptr;
	long val;

	if((acc.bvidx = json_lookup_int(jacc, "bufferView", -1)) < 0 || acc.bvidx >= mf_dynarr_size(gltf->bufviews)) {
		fprintf(stderr, "load_gltf: accessor missing or invalid buffer view index\n");
		return -1;
	}
	acc.offs = json_lookup_int(jacc, "byteOffset", 0);
	if((acc.type = json_lookup_int(jacc, "componentType", -1)) < 0) {
		fprintf(stderr, "load_gltf: accessor missing or invalid component type\n");
		return -1;
	}
	if((val = json_lookup_int(jacc, "count", -1)) < 0) {
		fprintf(stderr, "load_gltf: accessor missing or invalid count\n");
		return -1;
	}
	acc.count = val;
	if((acc.nelem = parse_elem_type(json_lookup_str(jacc, "type", 0))) <= 0) {
		fprintf(stderr, "load_gltf: accessor missing or invalid element type\n");
		return -1;
	}

	if(!(ptr = mf_dynarr_push(gltf->accessors, &acc))) {
		fprintf(stderr, "load_gltf: failed to add accessor\n");
		return -1;
	}
	gltf->accessors = ptr;
	return 0;
}

static int read_node(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jnode,
		const struct mf_userio *io)
{
	int i, nmeshes, meshidx;
	struct node node = {0};
	void *ptr;
	struct json_item *jitem;
	struct json_arr *arr;
	mf_vec3 pos = {0}, scale = {1, 1, 1};
	mf_vec4 rot = {0, 0, 0, 1};
	struct mf_node *mfnode = 0;
	struct mf_mesh *mesh;


	if(!(mfnode = mf_alloc_node())) {
		fprintf(stderr, "load_gltf: failed to allocate node\n");
		return -1;
	}
	node.mfnode = mfnode;

	if(!(mfnode->name = strdup(json_lookup_str(jnode, "name", "unnamed node")))) {
		fprintf(stderr, "load_gltf: failed to allocate node name\n");
		return -1;
	}

	if((meshidx = json_lookup_int(jnode, "mesh", -1)) >= 0) {
		/* add all meshes with the matching mesh index to this node */
		nmeshes = mf_num_meshes(mf);
		for(i=0; i<nmeshes; i++) {
			mesh = mf->meshes[i];
			/* udata was hijacked to keep the mesh index while loading */
			if((intptr_t)mesh->udata == meshidx) {
				if(mf_node_add_mesh(mfnode, mesh) == -1) {
					fprintf(stderr, "load_gltf: failed to add mesh to node\n");
					goto err;
				}
			}
		}
	}

	if((jitem = json_find_item(jnode, "matrix"))) {
		/* if there is a matrix property, use it */
		if(jitem->val.type != JSON_ARR || jitem->val.arr.size != 16) {
			fprintf(stderr, "load_gltf: node matrix invalid\n");
			goto err;
		}
		arr = &jitem->val.arr;

		for(i=0; i<16; i++) {
			if(arr->val[i].type != JSON_NUM) {
				fprintf(stderr, "load_gltf: node matrix elements are of invalid type\n");
				goto err;
			}
			mfnode->matrix[i] = arr->val[i].num;
		}
	} else {
		/* otherwise see if we have translation/rotation/scaling */
		if((arr = json_lookup_arr(jnode, "translation", 0))) {
			if(arr->size != 3 || jarr_to_vec(arr, (mf_vec4*)&pos) == -1) {
				fprintf(stderr, "load_gltf: node translation invalid\n");
				goto err;
			}
		}
		if((arr = json_lookup_arr(jnode, "rotation", 0))) {
			if(arr->size != 4 || jarr_to_vec(arr, &rot) == -1) {
				fprintf(stderr, "load_gltf: node rotation invalid\n");
				goto err;
			}
		}
		if((arr = json_lookup_arr(jnode, "scale", 0))) {
			if(arr->size != 3 || jarr_to_vec(arr, (mf_vec4*)&scale) == -1) {
				fprintf(stderr, "load_gltf: node scale invalid\n");
				goto err;
			}
		}
		mf_prs_matrix(mfnode->matrix, &pos, &rot, &scale);
	}

	/* read children node indices */
	if((jitem = json_find_item(jnode, "children"))) {
		if(jitem->val.type != JSON_ARR) {
			fprintf(stderr, "load_gltf: node children invalid\n");
			goto err;
		}
		arr = &jitem->val.arr;
		node.num_child = arr->size;
		if(!(node.cidx = malloc(node.num_child * sizeof *node.cidx))) {
			fprintf(stderr, "load_gltf: failed to allocate node children array\n");
			goto err;
		}

		for(i=0; i<arr->size; i++) {
			if(arr->val[i].type != JSON_NUM) {
				fprintf(stderr, "load_gltf: invalid child index in node children array\n");
				goto err;
			}
			node.cidx[i] = arr->val[i].inum;
		}
	}

	if(!(ptr = mf_dynarr_push(gltf->nodes, &node))) {
		fprintf(stderr, "load_gltf: failed to add node\n");
		goto err;
	}
	gltf->nodes = ptr;
	return 0;

err:
	free(node.cidx);
	mf_free_node(mfnode);
	return -1;
}

static struct accessor *find_accessor(struct gltf_file *gltf, struct json_obj *jattr, const char *name)
{
	int idx = json_lookup_int(jattr, name, -1);
	if(idx < 0) return 0;
	return gltf->accessors + idx;
}


enum { POSITION, NORMAL, TANGENT, TEXCOORD_0, COLOR_0, FACEIDX };

static int read_mesh_attr(struct mf_mesh *mesh, struct gltf_file *gltf, struct accessor *acc, int attrid)
{
	int j, curidx = 0;
	long i;
	unsigned char *src;
	struct bufview *bview;
	struct buffer *buf;
	float vec[4] = {0, 0, 0, 1};
	unsigned int vidx[3];

	bview = gltf->bufviews + acc->bvidx;
	buf = gltf->buffers + bview->bufidx;

	src = buf->data + bview->offs + acc->offs;

	for(i=0; i<acc->count; i++) {
		switch(acc->type) {
		case GLTF_FLOAT:
			memcpy(vec, src, acc->nelem * sizeof(float));
			src += acc->nelem * sizeof(float);
			break;

		case GLTF_UBYTE:
			for(j=0; j<acc->nelem; j++) {
				vec[j] = *src++ / 255.0f;
			}
			break;

		case GLTF_USHORT:
			if(attrid == FACEIDX) {
				vidx[curidx++] = *(unsigned short*)src;
				src += sizeof(unsigned short);
			} else {
				for(j=0; j<acc->nelem; j++) {
					vec[j] = *(unsigned short*)src / 65535.0f;
					src += sizeof(unsigned short);
				}
			}
			break;

		case GLTF_UINT:
			if(attrid == FACEIDX) {
				vidx[curidx++] = *(unsigned int*)src;
				src += sizeof(unsigned int);
				break;
			}
		default:
			fprintf(stderr, "load_gltf: unsupported element type\n");
			return -1;
		}

		switch(attrid) {
		case POSITION:
			mf_add_vertex(mesh, vec[0], vec[1], vec[2]);
			break;
		case NORMAL:
			mf_add_normal(mesh, vec[0], vec[1], vec[2]);
			break;
		case TANGENT:
			mf_add_tangent(mesh, vec[0], vec[1], vec[2]);
			break;
		case TEXCOORD_0:
			mf_add_texcoord(mesh, vec[0], vec[1]);
			break;
		case COLOR_0:
			mf_add_color(mesh, vec[0], vec[1], vec[2], vec[3]);
			break;
		case FACEIDX:
			if(curidx >= 3) {
				mf_add_triangle(mesh, vidx[0], vidx[1], vidx[2]);
				curidx = 0;
			}
			break;
		default:
			fprintf(stderr, "load_gltf: invalid attribute: %d\n", attrid);
			return -1;
		}
	}
	return 0;
}

static struct mf_mesh *read_prim(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jp)
{
	static const char *modestr[] = {"POINTS", "LINES", "LINE_LOOP", "LINE_STRIP",
		"TRIANGLES", "TRIANGLE_STRIP", "TRIANGLE_FAN"};
	static const char *attrstr[] = {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "COLOR_0", 0};
	int i, val;
	struct json_obj *jattr;
	struct mf_mesh *mesh;
	struct accessor *acc;

	if((val = json_lookup_int(jp, "mode", GLTF_TRIANGLES)) != GLTF_TRIANGLES) {
		printf("load_gltf: skip unsupported primitive type: %s\n", modestr[val]);
		return 0;
	}

	if(!(jattr = json_lookup_obj(jp, "attributes", 0))) {
		fprintf(stderr, "load_gltf: mesh primitive missing or invalid attributes object\n");
		return 0;
	}

	if(!(mesh = mf_alloc_mesh())) {
		fprintf(stderr, "load_gltf: failed to allocate mesh\n");
		return 0;
	}

	/* read all supported vertex attributes */
	for(i=0; attrstr[i]; i++) {
		if(!(acc = find_accessor(gltf, jattr, attrstr[i])) ||
				read_mesh_attr(mesh, gltf, acc, i) == -1) {
			if(i != POSITION) continue;
			fprintf(stderr, "load_gltf: missing or invalid POSITION attribute in primitive\n");
			goto err;
		}
	}

	/* read vertex indices if it's an indexed mesh */
	if((val = json_lookup_int(jp, "indices", -1)) >= 0) {
		if(val >= mf_dynarr_size(gltf->accessors)) {
			fprintf(stderr, "load_gltf: indices refers to invalid accessor: %d\n", val);
			goto err;
		}
		acc = gltf->accessors + val;
		if((acc->type != GLTF_UINT && acc->type != GLTF_USHORT) || acc->nelem != 1) {
			fprintf(stderr, "load_gltf: indices refers to accessor of invalid type\n");
			goto err;
		}

		if(read_mesh_attr(mesh, gltf, acc, FACEIDX) == -1) {
			fprintf(stderr, "load_gltf: invalid face index data in primitive\n");
			goto err;
		}
	}

	/* assign material */
	if((val = json_lookup_int(jp, "material", -1)) >= 0) {
		if(val >= mf_num_materials(mf)) {
			fprintf(stderr, "load_gltf: primitive refers to invalid material: %d\n", val);
		} else {
			mesh->mtl = mf_get_material(mf, val);
		}
	}

	return mesh;

err:
	mf_free_mesh(mesh);
	return 0;
}

static int read_mesh(struct mf_meshfile *mf, struct gltf_file *gltf, struct json_obj *jmesh,
		const struct mf_userio *io)
{
	int i;
	struct json_item *jitem;
	struct json_value *jprim;
	const char *mesh_name = json_lookup_str(jmesh, "name", 0);
	struct mf_mesh *mesh;

	if(!(jitem = json_find_item(jmesh, "primitives")) || jitem->val.type != JSON_ARR) {
		fprintf(stderr, "load_gltf: mesh missing or invalid primitives array\n");
		return -1;
	}

	for(i=0; i<jitem->val.arr.size; i++) {
		jprim = jitem->val.arr.val + i;

		if(jprim->type != JSON_OBJ) {
			fprintf(stderr, "load_gltf: mesh primitive not an object!\n");
			return -1;
		}

		if((mesh = read_prim(mf, gltf, &jprim->obj))) {
			if(!(mesh->name = strdup(mesh_name ? mesh_name : "unnamed mesh"))) {
				fprintf(stderr, "load_gltf: failed to allocate mesh name\n");
				mf_free_mesh(mesh);
				return -1;
			}
			mesh->udata = (void*)(intptr_t)gltf->meshcount;
			mf_add_mesh(mf, mesh);
		}
	}

	gltf->meshcount++;
	return 0;
}

int mf_save_gltf(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	/* TODO */
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

