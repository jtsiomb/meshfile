#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include <errno.h>
#include "meshfile.h"
#include "mfpriv.h"
#include "dynarr.h"


static void *io_open(const char *fname, const char *mode);
static void io_close(void *file);
static int io_read(void *file, void *buf, int sz);
static int io_write(void *file, void *buf, int sz);

static struct mf_material defmtl = {
	"default material",
	{
		{MF_RGBA, {1, 1, 1, 1}, {0}, 0},
		{MF_RGB},
		{MF_SCALAR},
		{MF_SCALAR, {1, 1, 1, 1}},
		{MF_RGB},
		{MF_SCALAR},
		{MF_SCALAR},
		{MF_SCALAR, {1.32}},
		{MF_SCALAR, {1, 1, 1, 1}},
		{MF_SCALAR}
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
		return -1;
	}
	if(!(mf->mtl = mf_dynarr_alloc(0, sizeof *mf->mtl))) {
		mf_dynarr_free(mf->meshes);
		mf->meshes = 0;
		return -1;
	}

	mf->aabox.vmin.x = mf->aabox.vmin.y = mf->aabox.vmin.z = FLT_MAX;
	mf->aabox.vmax.x = mf->aabox.vmax.y = mf->aabox.vmax.z = -FLT_MAX;
	return 0;
}

void mf_destroy(struct mf_meshfile *mf)
{
	mf_clear(mf);
	mf_dynarr_free(mf->meshes);
	mf_dynarr_free(mf->mtl);
	free(mf->name);
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
	memset(mtl, 0, sizeof *mtl);
	return 0;
}

void mf_destroy_mtl(struct mf_material *mtl)
{
	free(mtl->name);
}

int mf_num_meshes(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->meshes);
}

int mf_num_materials(const struct mf_meshfile *mf)
{
	return mf_dynarr_size(mf->mtl);
}

struct mf_mesh *mf_get_mesh(const struct mf_meshfile *mf, int idx)
{
	return mf->meshes[idx];
}

struct mf_material *mf_get_material(const struct mf_meshfile *mf, int idx)
{
	return mf->mtl[idx];
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

int mf_add_mesh(struct mf_meshfile *mf, struct mf_mesh *m)
{
	if(!(mf->meshes = mf_dynarr_push(mf->meshes, &m))) {
		return -1;
	}

	if(m->aabox.vmin.x < mf->aabox.vmin.x) mf->aabox.vmin.x = m->aabox.vmin.x;
	if(m->aabox.vmin.y < mf->aabox.vmin.y) mf->aabox.vmin.y = m->aabox.vmin.y;
	if(m->aabox.vmin.z < mf->aabox.vmin.z) mf->aabox.vmin.z = m->aabox.vmin.z;
	if(m->aabox.vmax.x > mf->aabox.vmax.x) mf->aabox.vmax.x = m->aabox.vmax.x;
	if(m->aabox.vmax.y > mf->aabox.vmax.y) mf->aabox.vmax.y = m->aabox.vmax.y;
	if(m->aabox.vmax.z > mf->aabox.vmax.z) mf->aabox.vmax.z = m->aabox.vmax.z;
	return 0;
}

int mf_add_material(struct mf_meshfile *mf, struct mf_material *mtl)
{
	if(!(mf->mtl = mf_dynarr_push(mf->mtl, &mtl))) {
		return -1;
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

int mf_load(struct mf_meshfile *mf, const char *fname)
{
	int res;
	FILE *fp;
	struct mf_userio io = {0};

	if(!(fp = fopen(fname, "rb"))) {
		fprintf(stderr, "mf_load: failed to open: %s: %s\n", fname, strerror(errno));
		return -1;
	}
	io.file = fp;
	io.open = io_open;
	io.close = io_close;
	io.read = io_read;

	mf->name = strdup(fname);
	res = mf_load_userio(mf, &io);
	fclose(fp);
	return res;
}

int mf_load_userio(struct mf_meshfile *mf, const struct mf_userio *io)
{
	return mf_load_obj(mf, io);
}

int mf_save(const struct mf_meshfile *mf, const char *fname)
{
	int res;
	FILE *fp;
	struct mf_userio io = {0};

	if(!(fp = fopen(fname, "wb"))) {
		fprintf(stderr, "mf_save: failed to open %s for writing: %s\n", fname, strerror(errno));
		return -1;
	}
	io.file = fp;
	io.write = io_write;

	res = mf_save_userio(mf, &io);
	fclose(fp);
	return res;
}

int mf_save_userio(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	return mf_save_obj(mf, io);
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

	m->aabox.vmin.x = m->aabox.vmin.y = m->aabox.vmin.z = FLT_MAX;
	m->aabox.vmax.x = m->aabox.vmax.y = m->aabox.vmax.z = -FLT_MAX;

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

static int io_write(void *file, void *buf, int sz)
{
	size_t wrbytes = fwrite(buf, 1, sz, file);
	if(!wrbytes) return -1;
	return wrbytes;
}

int mf_fgetc(const struct mf_userio *io)
{
	unsigned char c;
	if(io_read(io->file, &c, 1) == -1) {
		return -1;
	}
	return c;
}

char *mf_fgets(char *buf, int sz, const struct mf_userio *io)
{
	int c;
	char *dest = buf;
	while(sz > 1 && (c = mf_fgetc(io)) != -1) {
		*dest++ = c;
		if(c == '\n') break;
	}
	if(c == -1 && dest == buf) {
		return 0;
	}
	*dest = 0;
	return buf;
}

