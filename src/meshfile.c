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
#include <stdarg.h>
#include <float.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include "meshfile.h"
#include "mfpriv.h"
#include "dynarr.h"
#include "util.h"

/* the order in this table is significant. It's the order used when trying to
 * open a file. wavefront obj must be last, because it can't be identified.
 */
struct filefmt filefmt[MF_NUM_FMT] = {
	{MF_FMT_3DS, {"3ds", 0}, mf_load_3ds, mf_save_3ds},
	{MF_FMT_JTF, {"jtf", 0}, mf_load_jtf, mf_save_jtf},
	{MF_FMT_GLTF, {"gltf", 0}, mf_load_gltf, mf_save_gltf},
	{MF_FMT_STL, {"stl", 0}, mf_load_stl, mf_save_stl},
	{MF_FMT_OBJ, {"obj", 0}, mf_load_obj, mf_save_obj},
	{0}
};

static void assetpath_rbdelnode(struct rbnode *n, void *cls);

static void init_aabox(mf_aabox *box);
static void calc_aabox(struct mf_meshfile *mf);
static void expand_aabox(mf_aabox *box, mf_vec3 v);

static void *io_open(const char *fname, const char *mode);
static void io_close(void *file);
static int io_read(void *file, void *buf, int sz);
static int io_write(void *file, const void *buf, int sz);
static long io_seek(void *file, long offs, int from);

#define MF_FMT_MASK		0xff

#define DEFMAP \
	{0, {0}, MF_TEX_LINEAR, MF_TEX_LINEAR, MF_TEX_REPEAT, MF_TEX_REPEAT, {0, 0, 0}, {1, 1, 1}}

static struct mf_material defmtl = {
	"default material",
	{
		{MF_COLOR, {0.7, 0.7, 0.7, 0.7}, DEFMAP},
		{MF_SPECULAR, {0}, DEFMAP},
		{MF_SHININESS, {1}, DEFMAP},
		{MF_ROUGHNESS, {1, 1, 1, 1}, DEFMAP},
		{MF_METALLIC, {0}, DEFMAP},
		{MF_EMISSIVE, {0}, DEFMAP},
		{MF_REFLECT, {0}, DEFMAP},
		{MF_TRANSMIT, {0}, DEFMAP},
		{MF_IOR, {1}, DEFMAP},
		{MF_ALPHA, {1, 1, 1, 1}, DEFMAP},
		{MF_BUMP, {0}, DEFMAP}
	}
};


struct mf_meshfile *mf_alloc(void)
{
	struct mf_meshfile *mf;

	if(!(mf = malloc(sizeof *mf))) {
		return 0;
	}
	if(mf_init(mf) == -1) {
		free(mf);
		return 0;
	}
	return mf;
}

void mf_free(struct mf_meshfile *mf)
{
	if(!mf) return;
	mf_destroy(mf);
	free(mf);
}

int mf_init(struct mf_meshfile *mf)
{
	memset(mf, 0, sizeof *mf);

	if(!(mf->meshes = mf_dynarr_alloc(0, sizeof *mf->meshes))) {
		goto err;
	}
	if(!(mf->mtl = mf_dynarr_alloc(0, sizeof *mf->mtl))) {
		goto err;
	}
	if(!(mf->nodes = mf_dynarr_alloc(0, sizeof *mf->nodes))) {
		goto err;
	}
	if(!(mf->topnodes = mf_dynarr_alloc(0, sizeof *mf->topnodes))) {
		goto err;
	}

	if(!(mf->assetpath = rb_create(RB_KEY_STRING))) {
		goto err;
	}
	rb_set_delete_func(mf->assetpath, assetpath_rbdelnode, 0);

	init_aabox(&mf->aabox);
	return 0;

err:
	mf_dynarr_free(mf->meshes); mf->meshes = 0;
	mf_dynarr_free(mf->mtl); mf->mtl = 0;
	mf_dynarr_free(mf->nodes); mf->nodes = 0;
	mf_dynarr_free(mf->topnodes); mf->topnodes = 0;
	return -1;
}

void mf_destroy(struct mf_meshfile *mf)
{
	mf_clear(mf);
	mf_dynarr_free(mf->meshes);
	mf_dynarr_free(mf->mtl);
	mf_dynarr_free(mf->nodes);
	mf_dynarr_free(mf->topnodes);
	free(mf->name);
	free(mf->dirname);
	rb_free(mf->assetpath);
}

void mf_clear(struct mf_meshfile *mf)
{
	int i;

	for(i=0; i<mf_dynarr_size(mf->meshes); i++) {
		mf_free_mesh(mf->meshes[i]);
	}
	mf->meshes = mf_dynarr_clear(mf->meshes);

	for(i=0; i<mf_dynarr_size(mf->mtl); i++) {
		mf_free_mtl(mf->mtl[i]);
	}
	mf->mtl = mf_dynarr_clear(mf->mtl);

	for(i=0; i<mf_dynarr_size(mf->nodes); i++) {
		mf_free_node(mf->nodes[i]);
	}
	mf->nodes = mf_dynarr_clear(mf->nodes);
	mf->topnodes = mf_dynarr_clear(mf->topnodes);

	rb_clear(mf->assetpath);
}

struct mf_mesh *mf_alloc_mesh(void)
{
	struct mf_mesh *m;
	if(!(m = malloc(sizeof *m))) {
		return 0;
	}
	if(mf_init_mesh(m) == -1) {
		free(m);
		return 0;
	}
	return m;
}

void mf_free_mesh(struct mf_mesh *m)
{
	if(!m) return;
	mf_destroy_mesh(m);
	free(m);
}

int mf_init_mesh(struct mf_mesh *m)
{
	memset(m, 0, sizeof *m);
	m->mtl = &defmtl;
	init_aabox(&m->aabox);
	return 0;
}

void mf_destroy_mesh(struct mf_mesh *m)
{
	free(m->name);
	mf_dynarr_free(m->vertex);
	mf_dynarr_free(m->normal);
	mf_dynarr_free(m->tangent);
	mf_dynarr_free(m->texcoord);
	mf_dynarr_free(m->color);
	mf_dynarr_free(m->faces);
}

struct mf_material *mf_alloc_mtl(void)
{
	struct mf_material *mtl;
	if(!(mtl = malloc(sizeof *mtl))) {
		return 0;
	}
	if(mf_init_mtl(mtl) == -1) {
		free(mtl);
		return 0;
	}
	return mtl;
}

void mf_free_mtl(struct mf_material *mtl)
{
	if(!mtl) return;
	mf_destroy_mtl(mtl);
	free(mtl);
}

int mf_init_mtl(struct mf_material *mtl)
{
	memcpy(mtl, &defmtl, sizeof *mtl);
	return 0;
}

void mf_destroy_mtl(struct mf_material *mtl)
{
	int i, j;
	if(mtl->name != defmtl.name) {
		free(mtl->name);
	}
	for(i=0; i<MF_NUM_MTLATTR; i++) {
		free(mtl->attr[i].map.name);
		for(j=0; j<6; j++) {
			free(mtl->attr[i].map.cube[j]);
		}
	}
}

struct mf_node *mf_alloc_node(void)
{
	struct mf_node *n;
	if(!(n = malloc(sizeof *n))) {
		return 0;
	}
	if(mf_init_node(n) == -1) {
		free(n);
		return 0;
	}
	return n;
}

void mf_free_node(struct mf_node *node)
{
	if(node) {
		mf_destroy_node(node);
		free(node);
	}
}

int mf_init_node(struct mf_node *node)
{
	memset(node, 0, sizeof *node);
	if(!(node->child = mf_dynarr_alloc(0, sizeof *node->child))) {
		return -1;
	}
	if(!(node->meshes = mf_dynarr_alloc(0, sizeof *node->meshes))) {
		mf_dynarr_free(node->child);
		return -1;
	}

	mf_id_matrix(node->matrix);
	mf_id_matrix(node->global_matrix);
	return 0;
}

void mf_destroy_node(struct mf_node *node)
{
	if(!node) return;
	free(node->name);
	mf_dynarr_free(node->child);
	mf_dynarr_free(node->meshes);
}

const char *mf_get_name(const struct mf_meshfile *mf)
{
	return mf->name;
}

unsigned int mf_num_meshes(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->meshes);
}

unsigned int mf_num_materials(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->mtl);
}

unsigned int mf_num_nodes(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->nodes);
}

unsigned int mf_num_topnodes(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->topnodes);
}

struct mf_mesh *mf_get_mesh(const struct mf_meshfile *mf, int idx)
{
	return mf->meshes[idx];
}

struct mf_material *mf_get_material(const struct mf_meshfile *mf, int idx)
{
	return mf->mtl[idx];
}

struct mf_node *mf_get_node(const struct mf_meshfile *mf, int idx)
{
	return mf->nodes[idx];
}

struct mf_node *mf_get_topnode(const struct mf_meshfile *mf, int idx)
{
	return mf->topnodes[idx];
}

struct mf_mesh *mf_find_mesh(const struct mf_meshfile *mf, const char *name)
{
	int i, num = mf_dynarr_size(mf->meshes);
	for(i=0; i<num; i++) {
		if(strcmp(mf->meshes[i]->name, name) == 0) {
			return mf->meshes[i];
		}
	}
	return 0;
}

struct mf_material *mf_find_material(const struct mf_meshfile *mf, const char *name)
{
	int i, num = mf_dynarr_size(mf->mtl);
	for(i=0; i<num; i++) {
		if(strcmp(mf->mtl[i]->name, name) == 0) {
			return mf->mtl[i];
		}
	}
	return 0;
}

struct mf_node *mf_find_node(const struct mf_meshfile *mf, const char *name)
{
	int i, num = mf_dynarr_size(mf->nodes);
	for(i=0; i<num; i++) {
		if(strcmp(mf->nodes[i]->name, name) == 0) {
			return mf->nodes[i];
		}
	}
	return 0;
}

int mf_add_mesh(struct mf_meshfile *mf, struct mf_mesh *m)
{
	void *tmp;

	assert(m->name);

	if(!(tmp = mf_dynarr_push(mf->meshes, &m))) {
		return -1;
	}
	mf->meshes = tmp;
	return 0;
}

int mf_add_material(struct mf_meshfile *mf, struct mf_material *mtl)
{
	void *tmp;

	assert(mtl->name);

	if(!(tmp = mf_dynarr_push(mf->mtl, &mtl))) {
		return -1;
	}
	mf->mtl = tmp;
	return 0;
}

int mf_add_node(struct mf_meshfile *mf, struct mf_node *n)
{
	void *tmp;

	assert(n->name);

	if(!(tmp = mf_dynarr_push(mf->nodes, &n))) {
		return -1;
	}
	mf->nodes = tmp;

	if(!n->parent) {
		if(!(tmp = mf_dynarr_push(mf->topnodes, &n))) {
			mf->nodes = mf_dynarr_pop(mf->nodes);
			return -1;
		}
		mf->topnodes = tmp;
	}
	return 0;
}

int mf_bounds(const struct mf_meshfile *mf, mf_aabox *bb)
{
	if(mf->aabox.vmax.x < mf->aabox.vmin.x) {
		return -1;
	}
	*bb = mf->aabox;
	return 0;
}

void mf_update_xform(struct mf_meshfile *mf)
{
	int i, num = mf_num_topnodes(mf);
	for(i=0; i<num; i++) {
		mf_node_update_xform(mf->topnodes[i]);
	}
}

int mf_apply_xform(struct mf_meshfile *mf)
{
	unsigned int i, j, num_nodes;
	struct mf_node *node;

	num_nodes = mf_num_nodes(mf);
	for(i=0; i<num_nodes; i++) {
		node = mf_get_node(mf, i);
		for(j=0; j<node->num_meshes; j++) {
			/* TODO clone meshes referenced by more than one nodes */
			mf_transform_mesh(node->meshes[j], node->global_matrix);
		}
		mf_id_matrix(node->matrix);
		mf_id_matrix(node->global_matrix);
	}
	return 0;
}

int mf_load(struct mf_meshfile *mf, const char *fname, unsigned int flags)
{
	int res;
	FILE *fp;
	char *slash;
	struct mf_userio io = {0};

	if(!(fp = fopen(fname, "rb"))) {
		fprintf(stderr, "mf_load: failed to open: %s: %s\n", fname, strerror(errno));
		return -1;
	}
	io.file = fp;
	io.open = io_open;
	io.close = io_close;
	io.read = io_read;
	io.seek = io_seek;

	mf->name = strdup(fname);
	if((slash = strrchr(fname, '/')) && (mf->dirname = strdup(fname))) {
		slash = mf->dirname + (slash - fname);
		*slash = 0;
	}

	res = mf_load_userio(mf, &io, flags);
	fclose(fp);
	return res;
}

int mf_load_userio(struct mf_meshfile *mf, const struct mf_userio *io, unsigned int flags)
{
	unsigned int i, num_meshes;
	struct mf_mesh *mesh;
	long fpos = io->seek(io->file, 0, MF_SEEK_CUR);

	mf->flags = flags;

	for(i=0; i<MF_NUM_FMT; i++) {
		if(filefmt[i].load && filefmt[i].load(mf, io) == 0) {
			break;
		}
		if(io->seek(io->file, fpos, MF_SEEK_SET) == -1) {
			return -1;
		}
	}

	if(i == MF_NUM_FMT) {
		return -1;
	}
	mf_update_xform(mf);
	calc_aabox(mf);

	/* do any post-processing after load */
	if(flags & MF_NOPROC) return 0;

	num_meshes = mf_num_meshes(mf);
	for(i=0; i<num_meshes; i++) {
		mesh = mf_get_mesh(mf, i);
		if(!mesh->normal) {
			if(mf_calc_normals(mesh) == -1) {
				return -1;
			}
		}
	}

	if(flags & MF_GEN_TANGENTS) {
		for(i=0; i<num_meshes; i++) {
			mesh = mf_get_mesh(mf, i);
			mf_calc_tangents(mesh);
		}
	}

	if(flags & MF_APPLY_XFORM) {
		if(mf_apply_xform(mf) == -1) {
			mf_clear(mf);
			return -1;
		}
	}
	return 0;
}

int mf_strcasecmp(const char *a, const char *b)
{
	while(*a && *b && tolower(*a) == tolower(*b)) {
		a++;
		b++;
	}
	return (int)*(unsigned char*)a - (int)*(unsigned char*)b;
}

int mf_save(const struct mf_meshfile *mf, const char *fname, unsigned int flags)
{
	int i, j, res;
	FILE *fp;
	struct mf_meshfile *mmf;
	struct mf_userio io = {0};
	char *orig_name, *orig_dirname, *slash;
	const char *suffix;

	if(!(fp = fopen(fname, "wb"))) {
		fprintf(stderr, "mf_save: failed to open %s for writing: %s\n", fname, strerror(errno));
		return -1;
	}
	io.open = io_open;
	io.close = io_close;
	io.file = fp;
	io.write = io_write;
	io.seek = io_seek;

	mmf = (struct mf_meshfile*)mf;
	orig_name = mf->name;
	orig_dirname = mf->dirname;
	mmf->dirname = 0;

	mmf->name = strdup(fname);
	if((slash = strrchr(fname, '/')) && (mmf->dirname = strdup(fname))) {
		slash = mmf->dirname + (slash - fname);
		*slash = 0;
	}

	if((flags & MF_FMT_MASK) == 0 && (suffix = strrchr(fname, '.'))) {
		for(i=0; i<MF_NUM_FMT; i++) {
			for(j=0; filefmt[i].suffixes[j]; j++) {
				if(mf_strcasecmp(suffix + 1, filefmt[i].suffixes[j]) == 0) {
					flags |= filefmt[i].fmt;
					goto matched;
				}
			}
		}
	}
matched:

	res = mf_save_userio(mf, &io, flags);

	free(mmf->name);
	free(mmf->dirname);
	mmf->name = orig_name;
	mmf->dirname = orig_dirname;
	fclose(fp);
	return res;
}

int mf_save_userio(const struct mf_meshfile *mf, const struct mf_userio *io, unsigned int flags)
{
	int i, fmt;

	if(!(fmt = flags & MF_FMT_MASK)) {
		fmt = MF_FMT_OBJ;
	}

	((struct mf_meshfile*)mf)->flags = flags;

	for(i=0; i<MF_NUM_FMT; i++) {
		if(filefmt[i].fmt == fmt) {
			return filefmt[i].save(mf, io);
		}
	}
	return -1;
}

/* mesh functions */
void mf_clear_mesh(struct mf_mesh *m)
{
	free(m->name);
	mf_dynarr_free(m->vertex); m->vertex = 0;
	mf_dynarr_free(m->normal); m->normal = 0;
	mf_dynarr_free(m->tangent); m->tangent = 0;
	mf_dynarr_free(m->texcoord); m->texcoord = 0;
	mf_dynarr_free(m->color); m->color = 0;
	mf_dynarr_free(m->faces); m->faces = 0;

	init_aabox(&m->aabox);

	m->num_verts = m->num_faces = 0;
}

#define PUSH(arr, item) \
	do { \
		if(!(arr) && !((arr) = mf_dynarr_alloc(0, sizeof *(arr)))) { \
			return -1; \
		} \
		if(!((arr) = mf_dynarr_push((arr), &(item)))) { \
			return -1; \
		} \
	} while(0)


int mf_add_vertex(struct mf_mesh *m, float x, float y, float z)
{
	mf_vec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	PUSH(m->vertex, v);
	m->num_verts++;

	if(x < m->aabox.vmin.x) m->aabox.vmin.x = x;
	if(y < m->aabox.vmin.y) m->aabox.vmin.y = y;
	if(z < m->aabox.vmin.z) m->aabox.vmin.z = z;
	if(x > m->aabox.vmax.x) m->aabox.vmax.x = x;
	if(y > m->aabox.vmax.y) m->aabox.vmax.y = y;
	if(z > m->aabox.vmax.z) m->aabox.vmax.z = z;
	return 0;
}

int mf_add_normal(struct mf_mesh *m, float x, float y, float z)
{
	mf_vec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	PUSH(m->normal, v);
	return 0;
}

int mf_add_tangent(struct mf_mesh *m, float x, float y, float z)
{
	mf_vec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	PUSH(m->tangent, v);
	return 0;
}

int mf_add_texcoord(struct mf_mesh *m, float x, float y)
{
	mf_vec2 v;
	v.x = x;
	v.y = y;
	PUSH(m->texcoord, v);
	return 0;
}

int mf_add_color(struct mf_mesh *m, float r, float g, float b, float a)
{
	mf_vec4 v;
	v.x = r;
	v.y = g;
	v.z = b;
	v.w = a;
	PUSH(m->color, v);
	return 0;
}

int mf_add_triangle(struct mf_mesh *m, int a, int b, int c)
{
	mf_face f;
	f.vidx[0] = a;
	f.vidx[1] = b;
	f.vidx[2] = c;
	PUSH(m->faces, f);
	m->num_faces++;
	return 0;
}

int mf_add_quad(struct mf_mesh *m, int a, int b, int c, int d)
{
	if(mf_add_triangle(m, a, b, c) == -1) return -1;
	return mf_add_triangle(m, a, c, d);
}

enum {
	NORMAL		= 1,
	TANGENT		= 2,
	TEXCOORD	= 4,
	COLOR		= 8
};

#define IM_MAGIC	0xaaed55de
struct immed {
	unsigned int magic;
	int prim, vnum;
	void *orig_udata;
	unsigned int attrmask;

	mf_vec3 norm, tang;
	mf_vec2 uv;
	mf_vec4 col;
};

#define IMMED(m) \
	(m)->udata; if(((struct immed*)(m)->udata)->magic != IM_MAGIC) return

int mf_begin(struct mf_mesh *m, enum mf_primitive prim)
{
	struct immed *im;

	if(!(im = calloc(1, sizeof *im))) {
		return -1;
	}
	im->magic = IM_MAGIC;
	im->prim = prim;
	im->orig_udata = m->udata;

	mf_clear_mesh(m);
	m->udata = im;
	return 0;
}

void mf_end(struct mf_mesh *m)
{
	struct immed *im = m->udata;
	if(im->magic == IM_MAGIC) {
		m->udata = im->orig_udata;
		free(im);
	}
}

int mf_vertex(struct mf_mesh *m, float x, float y, float z)
{
	int res;
	unsigned int vidx;
	struct immed *im = m->udata;
	if(im->magic != IM_MAGIC) return -1;

	if(mf_add_vertex(m, x, y, z) == -1) return -1;
	if(im->attrmask & NORMAL) {
		if(mf_add_normal(m, im->norm.x, im->norm.y, im->norm.z) == -1) {
			return -1;
		}
	}
	if(im->attrmask & TANGENT) {
		if(mf_add_tangent(m, im->tang.x, im->tang.y, im->tang.z) == -1) {
			return -1;
		}
	}
	if(im->attrmask & TEXCOORD) {
		if(mf_add_texcoord(m, im->uv.x, im->uv.y) == -1) {
			return -1;
		}
	}
	if(im->attrmask & COLOR) {
		if(mf_add_color(m, im->col.x, im->col.y, im->col.z, im->col.w) == -1) {
			return -1;
		}
	}

	if(++im->vnum >= im->prim) {
		im->vnum = 0;
		vidx = mf_dynarr_size(m->vertex) - im->prim;

		if(im->prim == 4) {
			res = mf_add_quad(m, vidx, vidx + 1, vidx + 2, vidx + 3);
		} else {
			res = mf_add_triangle(m, vidx, vidx + 1, vidx + 2);
		}
		if(res == -1) {
			return -1;
		}
	}
	return 0;
}

void mf_normal(struct mf_mesh *m, float x, float y, float z)
{
	struct immed *im = IMMED(m);
	im->norm.x = x;
	im->norm.y = y;
	im->norm.z = z;
	im->attrmask |= NORMAL;
}

void mf_tangent(struct mf_mesh *m, float x, float y, float z)
{
	struct immed *im = IMMED(m);
	im->tang.x = x;
	im->tang.y = y;
	im->tang.z = z;
	im->attrmask |= TANGENT;
}

void mf_texcoord(struct mf_mesh *m, float u, float v)
{
	struct immed *im = IMMED(m);
	im->uv.x = u;
	im->uv.y = v;
	im->attrmask |= TEXCOORD;
}

void mf_color(struct mf_mesh *m, float r, float g, float b, float a)
{
	struct immed *im = IMMED(m);
	im->col.x = r;
	im->col.y = g;
	im->col.z = b;
	im->col.w = a;
	im->attrmask |= COLOR;
}

void mf_vertexv(struct mf_mesh *m, float *v)
{
	mf_vertex(m, v[0], v[1], v[2]);
}

void mf_normalv(struct mf_mesh *m, float *v)
{
	struct immed *im = IMMED(m);
	im->norm.x = v[0];
	im->norm.y = v[1];
	im->norm.z = v[2];
	im->attrmask |= NORMAL;
}

void mf_tangentv(struct mf_mesh *m, float *v)
{
	struct immed *im = IMMED(m);
	im->tang.x = v[0];
	im->tang.y = v[1];
	im->tang.z = v[2];
	im->attrmask |= TANGENT;
}

void mf_texcooordv(struct mf_mesh *m, float *v)
{
	struct immed *im = IMMED(m);
	im->uv.x = v[0];
	im->uv.y = v[1];
	im->attrmask |= TEXCOORD;
}

void mf_colorv(struct mf_mesh *m, float *v)
{
	struct immed *im = IMMED(m);
	im->col.x = v[0];
	im->col.y = v[1];
	im->col.z = v[2];
	im->col.w = v[3];
	im->attrmask |= COLOR;
}

int mf_calc_normals(struct mf_mesh *m)
{
	int i, j;
	mf_vec3 *v[3], *nptr, vab, vac, vn;
	struct mf_face *f;

	if(!m->num_verts || !m->num_faces) {
		return -1;
	}

	if(!m->normal) {
		if(!(m->normal = mf_dynarr_alloc(m->num_verts, sizeof *m->normal))) {
			return -1;
		}
	}
	memset(m->normal, 0, m->num_verts * sizeof *m->normal);

	for(i=0; i<m->num_faces; i++) {
		f = m->faces + i;

		for(j=0; j<3; j++) {
			v[j] = m->vertex + f->vidx[j];
		}
		vab.x = v[1]->x - v[0]->x;
		vab.y = v[1]->y - v[0]->y;
		vab.z = v[1]->z - v[0]->z;
		vac.x = v[2]->x - v[0]->x;
		vac.y = v[2]->y - v[0]->y;
		vac.z = v[2]->z - v[0]->z;
		mf_cross(&vn, &vab, &vac);
		mf_normalize(&vn);

		for(j=0; j<3; j++) {
			nptr = m->normal + f->vidx[j];
			nptr->x += vn.x;
			nptr->y += vn.y;
			nptr->z += vn.z;
		}
	}

	for(i=0; i<m->num_verts; i++) {
		mf_normalize(m->normal + i);
	}
	return 0;
}

/* adapted from: https://terathon.com/blog/tangent-space.html */
int mf_calc_tangents(struct mf_mesh *m)
{
	unsigned int i, j, vidx;
	float r, dot;
	mf_vec3 vpos[3], vnorm[3], *vtang[3], va, vb, udir, nprojt;
	mf_vec2 uv[3], ta, tb;
	struct mf_face *face;

	if(!m->num_verts || !m->num_faces || !m->texcoord) {
		return -1;
	}

	if(!m->normal) {
		if(mf_calc_normals(m) == -1) {
			return -1;
		}
	}

	if(!m->tangent) {
		if(!(m->tangent = mf_dynarr_alloc(m->num_verts, sizeof *m->tangent))) {
			return -1;
		}
	}
	memset(m->tangent, 0, m->num_verts * sizeof *m->normal);

	face = m->faces;
	for(i=0; i<m->num_faces; i++) {
		for(j=0; j<3; j++) {
			vidx = face->vidx[j];
			vpos[j] = m->vertex[vidx];
			uv[j] = m->texcoord[vidx];
			vtang[j] = m->tangent + vidx;
		}

		mf_vsub(&va, vpos + 1, vpos);
		mf_vsub(&vb, vpos + 2, vpos);
		ta.x = uv[1].x - uv[0].x;
		ta.y = uv[1].y - uv[0].y;
		tb.x = uv[2].x - uv[0].x;
		tb.y = uv[2].y - uv[0].y;
		r = 1.0f / (ta.x * tb.y - tb.x * ta.y);

		udir.x = (tb.y * va.x - ta.y * vb.x) * r;
		udir.y = (tb.y * va.y - ta.y * vb.y) * r;
		udir.z = (tb.y * va.z - ta.y * vb.z) * r;

		mf_vadd(vtang[0], vtang[0], &udir);
		mf_vadd(vtang[1], vtang[1], &udir);
		mf_vadd(vtang[2], vtang[2], &udir);
		face++;
	}

	face = m->faces;
	for(i=0; i<m->num_faces; i++) {
		for(j=0; j<3; j++) {
			vidx = face->vidx[j];
			vnorm[j] = m->normal[vidx];
			vtang[j] = m->tangent + vidx;
			dot = mf_dot(vnorm + j, vtang[j]);
			nprojt.x = vnorm[j].x * dot;
			nprojt.y = vnorm[j].y * dot;
			nprojt.z = vnorm[j].z * dot;
			mf_vsub(vtang[j], vtang[j], &nprojt);
			mf_normalize(vtang[j]);
		}
	}
	return 0;
}

void mf_transform_mesh(struct mf_mesh *m, const float *mat)
{
	unsigned int i;
	float dirmat[16];

	for(i=0; i<m->num_verts; i++) {
		mf_transform(m->vertex + i, m->vertex + i, mat);
	}
	if(!m->normal && !m->tangent) {
		return;
	}

	mf_inverse_matrix(dirmat, mat);
	mf_transpose_matrix(dirmat, dirmat);

	if(m->normal) {
		for(i=0; i<m->num_verts; i++) {
			mf_transform_dir(m->normal + i, m->normal + i, dirmat);
		}
	}
	if(m->tangent) {
		for(i=0; i<m->num_verts; i++) {
			mf_transform_dir(m->tangent + i, m->tangent + i, dirmat);
		}
	}
}

/* node functions */
int mf_node_add_mesh(struct mf_node *n, struct mf_mesh *m)
{
	int i;
	void *tmp;

	n->num_meshes = mf_dynarr_size(n->meshes);
	for(i=0; i<n->num_meshes; i++) {
		if(n->meshes[i] == m) {
			return 0;
		}
	}

	if(!(tmp = mf_dynarr_push(n->meshes, &m))) {
		return -1;
	}
	n->meshes = tmp;
	n->num_meshes++;
	return 0;
}

int mf_node_remove_mesh(struct mf_node *n, struct mf_mesh *m)
{
	int i;

	n->num_meshes = mf_dynarr_size(n->meshes);
	for(i=0; i<n->num_meshes; i++) {
		if(n->meshes[i] == m) {
			n->meshes[i] = n->meshes[--n->num_meshes];
			n->meshes = mf_dynarr_pop(n->meshes);
			break;
		}
	}
	return 0;
}

int mf_node_add_child(struct mf_node *n, struct mf_node *c)
{
	void *tmp;

	if(!(tmp = mf_dynarr_push(n->child, &c))) {
		return -1;
	}
	n->child = tmp;
	n->num_child = mf_dynarr_size(n->child);

	if(c->parent) {
		mf_node_remove_child(c->parent, c);
	}
	c->parent = n;
	return 0;
}

int mf_node_remove_child(struct mf_node *n, struct mf_node *c)
{
	int i;

	n->num_child = mf_dynarr_size(n->child);
	for(i=0; i<n->num_child; i++) {
		if(n->child[i] == c) {
			n->child[i] = n->child[--n->num_child];
			n->child = mf_dynarr_pop(n->child);
			break;
		}
	}

	if(c->parent == n) {
		c->parent = 0;
	}
	return 0;
}

void mf_node_update_xform(struct mf_node *n)
{
	int i;

	if(n->parent) {
		mf_mult_matrix(n->global_matrix, n->parent->global_matrix, n->matrix);
	} else {
		memcpy(n->global_matrix, n->matrix, sizeof n->global_matrix);
	}

	for(i=0; i<n->num_child; i++) {
		mf_node_update_xform(n->child[i]);
	}
}

/* utility functions */
const char *mf_find_asset(const struct mf_meshfile *mf, const char *fname)
{
	struct rbnode *rbn;
	char *key, *pathbuf;
	FILE *fp;

	if(!fname || !mf->dirname) {
		return fname;
	}

	if((rbn = rb_find(mf->assetpath, (void*)fname))) {
		return rbn->data;
	}

	if(!(key = strdup(fname))) {
		return fname;
	}
	if(!(pathbuf = malloc(strlen(fname) + strlen(mf->dirname) + 2))) {
		return fname;
	}

	sprintf(pathbuf, "%s/%s", mf->dirname, fname);
	if((fp = fopen(pathbuf, "r"))) {
		fclose(fp);
		rb_insert(mf->assetpath, key, pathbuf);
		return pathbuf;
	}

	strcpy(pathbuf, fname);
	if((fp = fopen(pathbuf, "r"))) {
		fclose(fp);
		rb_insert(mf->assetpath, key, pathbuf);
		return pathbuf;
	}

	return fname;
}

static void assetpath_rbdelnode(struct rbnode *n, void *cls)
{
	free(n->key);
	free(n->data);
}

static void init_aabox(mf_aabox *box)
{
	box->vmin.x = box->vmin.y = box->vmin.z = FLT_MAX;
	box->vmax.x = box->vmax.y = box->vmax.z = -FLT_MAX;
}

static void calc_aabox(struct mf_meshfile *mf)
{
	long k;
	int i, j, nnodes = mf_num_nodes(mf);
	struct mf_node *n;
	struct mf_mesh *m;
	mf_vec3 v;

	init_aabox(&mf->aabox);

	for(i=0; i<nnodes; i++) {
		n = mf_get_node(mf, i);
		for(j=0; j<n->num_meshes; j++) {
			m = n->meshes[j];
			for(k=0; k<m->num_verts; k++) {
				mf_transform(&v, m->vertex + k, n->global_matrix);
				expand_aabox(&mf->aabox, v);
			}
		}
	}
}

static void expand_aabox(mf_aabox *box, mf_vec3 v)
{
	if(v.x < box->vmin.x) box->vmin.x = v.x;
	if(v.y < box->vmin.y) box->vmin.y = v.y;
	if(v.z < box->vmin.z) box->vmin.z = v.z;
	if(v.x > box->vmax.x) box->vmax.x = v.x;
	if(v.y > box->vmax.y) box->vmax.y = v.y;
	if(v.z > box->vmax.z) box->vmax.z = v.z;
}

/* file I/O functions */

static void *io_open(const char *fname, const char *mode)
{
	return fopen(fname, mode);
}

static void io_close(void *file)
{
	fclose(file);
}

static int io_read(void *file, void *buf, int sz)
{
	size_t rdbytes = fread(buf, 1, sz, file);
	if(!rdbytes) return -1;
	return rdbytes;
}

static int io_write(void *file, const void *buf, int sz)
{
	size_t wrbytes = fwrite(buf, 1, sz, file);
	if(!wrbytes) return -1;
	return wrbytes;
}

static long io_seek(void *file, long offs, int from)
{
	fseek(file, offs, from);
	return ftell(file);
}

int mf_fgetc(const struct mf_userio *io)
{
	unsigned char c;
	if(io->read(io->file, &c, 1) == -1) {
		return -1;
	}
	return c;
}

char *mf_fgets(char *buf, int sz, const struct mf_userio *io)
{
	int c;
	char *dest = buf;
	char *endp = buf + sz - 1;
	while((c = mf_fgetc(io)) != -1) {
		if(dest < endp) {
			*dest++ = c;
		}
		if(c == '\n') break;
	}
	if(c == -1 && dest == buf) {
		return 0;
	}
	*dest = 0;
	return buf;
}

int mf_fputc(int c, const struct mf_userio *io)
{
	unsigned char ch = c;
	if(io->write(io->file, &ch, 1) < 1) {
		return -1;
	}
	return c;
}

int mf_fputs(const char *s, const struct mf_userio *io)
{
	int len = strlen(s);
	if((len = io->write(io->file, (void*)s, len)) <= 0) {
		return -1;
	}
	return len;
}

int mf_fprintf(const struct mf_userio *io, const char *fmt, ...)
{
	int len;
	va_list ap;
	static char *buf;
	static int bufsz;

	if(!buf) {
		bufsz = 256;
		if(!(buf = malloc(bufsz))) {
			return -1;
		}
	}

	for(;;) {
		va_start(ap, fmt);
		len = vsnprintf(buf, bufsz, fmt, ap);
		va_end(ap);

		if(len > bufsz) {
			/* C99-compliant vsnprintf, tells us how much space we need */
			free(buf);
			bufsz = len;
			if(!(buf = malloc(bufsz))) {
				return -1;
			}

		} else if(len == -1) {
			/* non-C99 vsnprintf, try doubling the buffer until it succeeds */
			free(buf);
			bufsz <<= 1;
			if(!(buf = malloc(bufsz))) {
				return -1;
			}

		} else {
			break;	/* success */
		}
	}

	mf_fputs(buf, io);
	return len;
}
