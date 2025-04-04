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
#ifndef MESHFILE_H_
#define MESHFILE_H_

typedef struct mf_vec2 {
	float x, y;
} mf_vec2;

typedef struct mf_vec3 {
	float x, y, z;
} mf_vec3;

typedef struct mf_vec4 {
	float x, y, z, w;
} mf_vec4;

typedef struct mf_face {
	unsigned int vidx[3];
} mf_face;

typedef struct mf_aabox {
	mf_vec3 vmin, vmax;
} mf_aabox;

enum mf_mtlattr_type {
	MF_COLOR,		/* base color/albedo/diffuse */
	MF_SPECULAR,	/* phong/blinn specular color */
	MF_SHININESS,	/* phong/blinn specular exponent */
	MF_ROUGHNESS,	/* roughness for physically-based models */
	MF_METALLIC,	/* metallic for physically-based models */
	MF_EMISSIVE,	/* emitted light color */
	MF_REFLECT,		/* reflectivity */
	MF_TRANSMIT,	/* transmittance through material */
	MF_IOR,			/* index of refraction */
	MF_ALPHA,		/* 1 - transmit, value duplicate of color.w */
	MF_BUMP,		/* bump scale, where applicable */
	MF_NUM_MTLATTR
};

enum mf_texfilter { MF_TEX_NEAREST, MF_TEX_LINEAR };
enum mf_texwrap { MF_TEX_REPEAT, MF_TEX_CLAMP };
enum mf_primitive { MF_TRIANGLES = 3, MF_QUADS = 4 };

struct mf_texmap {
	char *name;		/* for 2D maps */
	char *cube[6];	/* for cubemaps */
	enum mf_texfilter ufilt, vfilt;
	enum mf_texwrap uwrap, vwrap;
	mf_vec3 offset, scale;
	float rot;
};

struct mf_mtlattr {
	enum mf_mtlattr_type type;
	mf_vec4 val;
	struct mf_texmap map;

	void *udata;
};

struct mf_material {
	char *name;
	struct mf_mtlattr attr[MF_NUM_MTLATTR];

	void *udata;
};

struct mf_node {
	char *name;
	struct mf_node *parent;
	struct mf_node **child;
	int num_child;

	float matrix[16];			/* transformation for this node */
	float global_matrix[16];	/* matrix * parent->global_matrix */

	struct mf_mesh **meshes;	/* meshes associated with this node */
	int num_meshes;

	void *udata;
};

struct mf_mesh {
	char *name;
	mf_vec3 *vertex;
	mf_vec3 *normal;
	mf_vec3 *tangent;
	mf_vec2	*texcoord;
	mf_vec4 *color;
	mf_face *faces;

	unsigned int num_verts, num_faces;

	mf_aabox aabox;
	struct mf_material *mtl;

	void *udata;
};

enum { MF_SEEK_SET, MF_SEEK_CUR, MF_SEEK_END };

struct mf_userio {
	void *file;
	void *(*open)(const char*, const char*);
	void (*close)(void*);
	int (*read)(void*, void*, int);
	int (*write)(void*, const void*, int);
	long (*seek)(void*, long, int);
};

/* load flags */
enum {
	MF_APPLY_XFORM		= 0x0001,	/* pre-transform to world space */
	MF_GEN_TANGENTS		= 0x0002,	/* compute tangents if missing */

	MF_NOPROC			= 0x8000	/* don't perform any processing on load */
};

/* file format part of the save flags */
enum {
	MF_FMT_AUTO,
	MF_FMT_OBJ,		/* wavefront OBJ */
	MF_FMT_JTF,		/* Just Triangle Faces: http://runtimeterror.com/tech/jtf */
	MF_FMT_GLTF,	/* GL Transmission Format */
	MF_FMT_3DS,		/* 3D Studio */
	MF_FMT_STL,		/* STL */
	MF_NUM_FMT
};

struct mf_meshfile;

struct mf_meshfile *mf_alloc(void);
void mf_free(struct mf_meshfile *mf);
int mf_init(struct mf_meshfile *mf);
void mf_destroy(struct mf_meshfile *mf);
void mf_clear(struct mf_meshfile *mf);

struct mf_mesh *mf_alloc_mesh(void);
void mf_free_mesh(struct mf_mesh *m);
int mf_init_mesh(struct mf_mesh *m);
void mf_destroy_mesh(struct mf_mesh *m);

struct mf_material *mf_alloc_mtl(void);
void mf_free_mtl(struct mf_material *mtl);
int mf_init_mtl(struct mf_material *mtl);
void mf_destroy_mtl(struct mf_material *mtl);

struct mf_node *mf_alloc_node(void);
void mf_free_node(struct mf_node *node);
int mf_init_node(struct mf_node *node);
void mf_destroy_node(struct mf_node *node);

const char *mf_get_name(const struct mf_meshfile *mf);

unsigned int mf_num_meshes(const struct mf_meshfile *mf);
unsigned int mf_num_materials(const struct mf_meshfile *mf);
unsigned int mf_num_nodes(const struct mf_meshfile *mf);
unsigned int mf_num_topnodes(const struct mf_meshfile *mf);

struct mf_mesh *mf_get_mesh(const struct mf_meshfile *mf, int idx);
struct mf_material *mf_get_material(const struct mf_meshfile *mf, int idx);
struct mf_node *mf_get_node(const struct mf_meshfile *mf, int idx);
struct mf_node *mf_get_topnode(const struct mf_meshfile *mf, int idx);

struct mf_mesh *mf_find_mesh(const struct mf_meshfile *mf, const char *name);
struct mf_material *mf_find_material(const struct mf_meshfile *mf, const char *name);
struct mf_node *mf_find_node(const struct mf_meshfile *mf, const char *name);

int mf_add_mesh(struct mf_meshfile *mf, struct mf_mesh *m);
int mf_add_material(struct mf_meshfile *mf, struct mf_material *mtl);
int mf_add_node(struct mf_meshfile *mf, struct mf_node *n);

int mf_bounds(const struct mf_meshfile *mf, mf_aabox *bb);
void mf_update_xform(struct mf_meshfile *mf);
int mf_apply_xform(struct mf_meshfile *mf);

int mf_load(struct mf_meshfile *mf, const char *fname, unsigned int flags);
int mf_load_userio(struct mf_meshfile *mf, const struct mf_userio *io, unsigned int flags);

int mf_save(const struct mf_meshfile *mf, const char *fname, unsigned int flags);
int mf_save_userio(const struct mf_meshfile *mf, const struct mf_userio *io, unsigned int flags);

/* mesh functions */
void mf_clear_mesh(struct mf_mesh *m);

int mf_add_vertex(struct mf_mesh *m, float x, float y, float z);
int mf_add_normal(struct mf_mesh *m, float x, float y, float z);
int mf_add_tangent(struct mf_mesh *m, float x, float y, float z);
int mf_add_texcoord(struct mf_mesh *m, float u, float v);
int mf_add_color(struct mf_mesh *m, float r, float g, float b, float a);
int mf_add_triangle(struct mf_mesh *m, int a, int b, int c);
int mf_add_quad(struct mf_mesh *m, int a, int b, int c, int d);

int mf_begin(struct mf_mesh *m, enum mf_primitive prim);
void mf_end(struct mf_mesh *m);
int mf_vertex(struct mf_mesh *m, float x, float y, float z);
void mf_normal(struct mf_mesh *m, float x, float y, float z);
void mf_tangent(struct mf_mesh *m, float x, float y, float z);
void mf_texcoord(struct mf_mesh *m, float u, float v);
void mf_color(struct mf_mesh *m, float r, float g, float b, float a);
void mf_vertexv(struct mf_mesh *m, float *v);
void mf_normalv(struct mf_mesh *m, float *v);
void mf_tangentv(struct mf_mesh *m, float *v);
void mf_texcooordv(struct mf_mesh *m, float *v);
void mf_colorv(struct mf_mesh *m, float *v);

int mf_calc_normals(struct mf_mesh *m);
int mf_calc_tangents(struct mf_mesh *m);
void mf_transform_mesh(struct mf_mesh *m, const float *mat);

/* node functions */
int mf_node_add_mesh(struct mf_node *n, struct mf_mesh *m);
int mf_node_remove_mesh(struct mf_node *n, struct mf_mesh *m);
int mf_node_add_child(struct mf_node *n, struct mf_node *c);
int mf_node_remove_child(struct mf_node *n, struct mf_node *c);
void mf_node_update_xform(struct mf_node *n);

/* utility functions */
const char *mf_find_asset(const struct mf_meshfile *mf, const char *fname);

#endif	/* MESHFILE_H_ */
