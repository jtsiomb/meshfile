#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "mfpriv.h"
#include "rbtree.h"
#include "dynarr.h"


struct facevertex {
	int vidx, tidx, nidx;
};

static int mesh_done(struct mf_meshfile *mf, struct mf_mesh *mesh);
static int load_mtl(struct mf_meshfile *mf, const struct mf_userio *io);
static char *clean_line(char *s);
static char *parse_face_vert(char *ptr, struct facevertex *fv, int numv, int numt, int numn);
static int cmp_facevert(const void *ap, const void *bp);
static void free_rbnode_key(struct rbnode *n, void *cls);

static int write_material(const struct mf_material *mtl, const struct mf_userio *io);
static int write_mesh(const struct mf_mesh *m, const struct mf_userio *io);


int mf_load_obj(struct mf_meshfile *mf, const struct mf_userio *io)
{
	char buf[128];
	int result = -1;
	int i, line_num = 0;
	mf_vec3 *varr = 0, *narr = 0;
	mf_vec2 *tarr = 0;
	struct rbtree *rbtree = 0;
	struct mf_mesh *mesh = 0;
	char *mesh_name = 0;
	struct mf_userio subio = {0};

	subio.open = io->open;
	subio.close = io->close;
	subio.read = io->read;

	if(!mf->name && !(mf->name = strdup("<unknown>"))) {
		fprintf(stderr, "mf_load_userio: failed to allocate name\n");
		return -1;
	}
	if(!(mesh_name = strdup(mf->name))) {
		fprintf(stderr, "mf_load: failed to allocate mesh name\n");
		goto end;
	}

	if(!(rbtree = rb_create(cmp_facevert))) {
		fprintf(stderr, "mf_load: failed to create rbtree\n");
		goto end;
	}
	rb_set_delete_func(rbtree, free_rbnode_key, 0);

	if(!(varr = mf_dynarr_alloc(0, sizeof *varr)) ||
			!(narr = mf_dynarr_alloc(0, sizeof *narr)) ||
			!(tarr = mf_dynarr_alloc(0, sizeof *tarr))) {
		fprintf(stderr, "mf_load: failed to allocate vertex attribute arrays\n");
		goto end;
	}

	if(!(mesh = mf_alloc_mesh())) {
		goto end;
	}

	while(mf_fgets(buf, sizeof buf, io)) {
		char *line = clean_line(buf);
		++line_num;

		if(!line || !*line) continue;

		switch(line[0]) {
		case 'v':
			if(isspace(line[1])) {
				/* vertex */
				mf_vec3 v;
				int num;

				num = sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z);
				if(num < 3) {
					fprintf(stderr, "%s:%d: invalid vertex definition: \"%s\"\n", mf->name, line_num, line);
					goto end;
				}
				if(!(varr = mf_dynarr_push(varr, &v))) {
					fprintf(stderr, "mf_load: failed to resize vertex buffer\n");
					goto end;
				}

			} else if(line[1] == 't' && isspace(line[2])) {
				/* texcoord */
				mf_vec2 tc;
				if(sscanf(line + 3, "%f %f", &tc.x, &tc.y) != 2) {
					fprintf(stderr, "%s:%d: invalid texcoord definition: \"%s\"\n", mf->name, line_num, line);
					goto end;
				}
				tc.y = 1.0f - tc.y;
				if(!(tarr = mf_dynarr_push(tarr, &tc))) {
					fprintf(stderr, "mf_load: failed to resize texcoord buffer\n");
					goto end;
				}

			} else if(line[1] == 'n' && isspace(line[2])) {
				/* normal */
				mf_vec3 norm;
				if(sscanf(line + 3, "%f %f %f", &norm.x, &norm.y, &norm.z) != 3) {
					fprintf(stderr, "%s:%d: invalid normal definition: \"%s\"\n", mf->name, line_num, line);
					goto end;
				}
				if(!(narr = mf_dynarr_push(narr, &norm))) {
					fprintf(stderr, "mf_load: failed to resize normal buffer\n");
					goto end;
				}
			}
			break;

		case 'f':
			if(isspace(line[1])) {
				/* face */
				int res;
				char *ptr = line + 2;
				struct facevertex fv;
				unsigned int vidx[4];
				struct rbnode *node;
				int vsz = mf_dynarr_size(varr);
				int tsz = mf_dynarr_size(tarr);
				int nsz = mf_dynarr_size(narr);

				if(!vsz) {
					fprintf(stderr, "%s:%d: encountered face before any vertices\n", mf->name, line_num);
					goto end;
				}

				for(i=0; i<4; i++) {
					if(!(ptr = parse_face_vert(ptr, &fv, vsz, tsz, nsz))) {
						if(i < 3) {
							fprintf(stderr, "%s:%d: invalid face definition: \"%s\"\n", mf->name, line_num, line);
							goto end;
						} else {
							break;
						}
					}

					if((node = rb_find(rbtree, &fv))) {
						vidx[i] = (uintptr_t)node->data;
					} else {
						unsigned int newidx = mesh->num_verts;
						struct facevertex *newfv;
						mf_vec3 *vptr = varr + fv.vidx;

						if(mf_add_vertex(mesh, vptr->x, vptr->y, vptr->z) == -1) {
							fprintf(stderr, "mf_load: failed to resize vertex array\n");
							goto end;
						}
						if(fv.nidx >= 0) {
							if(mf_add_normal(mesh, narr[fv.nidx].x, narr[fv.nidx].y, narr[fv.nidx].z) == -1) {
								fprintf(stderr, "mf_load: failed to resize normal array\n");
								goto end;
							}
						}
						if(fv.tidx >= 0) {
							if(mf_add_texcoord(mesh, tarr[fv.tidx].x, tarr[fv.tidx].y) == -1) {
								fprintf(stderr, "mf_load: failed to resize texcoord array\n");
								goto end;
							}
						}
						vidx[i] = newidx;

						if((newfv = malloc(sizeof *newfv))) {
							*newfv = fv;
						}
						if(!newfv || rb_insert(rbtree, newfv, (void*)(uintptr_t)newidx) == -1) {
							fprintf(stderr, "mf_load: failed to insert facevertex to the binary search tree\n");
							goto end;
						}
					}
				}

				if(i == 4) {
					res = mf_add_quad(mesh, vidx[0], vidx[1], vidx[2], vidx[3]);
				} else {
					res = mf_add_triangle(mesh, vidx[0], vidx[1], vidx[2]);
				}
				if(res == -1) {
					fprintf(stderr, "mf_load: failed to resize index array\n");
					goto end;
				}
			}
			break;

		case 'o':
			mesh->name = mesh_name;
			if(mesh_done(mf, mesh) != -1 && !(mesh = mf_alloc_mesh())) {
				fprintf(stderr, "mf_load: failed to allocate mesh\n");
				goto end;
			}
			if(!(mesh_name = strdup(clean_line(line + 2)))) {
				fprintf(stderr, "mf_load: failed to allocate mesh name\n");
				goto end;
			}
			break;

		default:
			if(memcmp(line, "mtllib", 6) == 0) {
				char *slash, *path;
				char *mtl_fname = clean_line(line + 6);
				if((slash = strrchr(mf->name, '/')) &&
						(path = malloc(strlen(mf->name) + strlen(mtl_fname)))) {
					memcpy(path, mf->name, slash - mf->name + 1);
					strcpy(path + (slash - mf->name + 1), mtl_fname);
				} else {
					path = mtl_fname;
				}
				if((subio.file = io->open(path, "rb"))) {
					load_mtl(mf, &subio);
					io->close(subio.file);
				} else {
					fprintf(stderr, "mf_load: failed to open material library: %s, ignoring\n", path);
				}
				if(path != mtl_fname) {
					free(path);
				}
			} else if(memcmp(line, "usemtl", 6) == 0) {
				struct mf_material *mtl = mf_find_material(mf, clean_line(line + 6));
				if(mtl) mesh->mtl = mtl;
			}
			break;
		}
	}

	mesh->name = mesh_name;
	mesh_name = 0;
	mesh_done(mf, mesh);
	mesh = 0;

	result = 0;	/* success */

end:
	mf_dynarr_free(varr);
	mf_dynarr_free(narr);
	mf_dynarr_free(tarr);
	mf_free_mesh(mesh);
	rb_free(rbtree);
	return result;
}

static int mesh_done(struct mf_meshfile *mf, struct mf_mesh *mesh)
{
	if(!mesh->faces || mf_dynarr_empty(mesh->faces)) {
		return -1;
	}

	if(mesh->normal) {
		if(mf_dynarr_size(mesh->normal) != mf_dynarr_size(mesh->vertex)) {
			fprintf(stderr, "mf_load: ignoring mesh with inconsistent attributes\n");
			goto reset_mesh;
		}
	}
	if(mesh->texcoord) {
		if(mf_dynarr_size(mesh->texcoord) != mf_dynarr_size(mesh->vertex)) {
			fprintf(stderr, "mf_load: ignoring mesh with inconsistent attributes\n");
			goto reset_mesh;
		}
	}

	if(mf_add_mesh(mf, mesh) == -1) {
		fprintf(stderr, "mf_load: failed to add mesh\n");
		goto reset_mesh;
	}
	return 0;

reset_mesh:
	mf_destroy_mesh(mesh);
	mf_init_mesh(mesh);
	return -1;
}

static int parse_value(struct mf_mtlattr *attr, const char *args)
{
	int n;
	mf_vec4 *valptr = &attr->val;
	if((n = sscanf(args, "%f %f %f", &valptr->x, &valptr->y, &valptr->z)) != 3) {
		if(n == 1) {
			valptr->y = valptr->z = valptr->x;
		} else {
			fprintf(stderr, "ignoring invalid or unsupported mtl value: \"%s\"\n", args);
			return -1;
		}
	}
	return 0;
}

static int parse_map(struct mf_mtlattr *attr, const char *args)
{
	return 0;	/* TODO */
}

static int load_mtl(struct mf_meshfile *mf, const struct mf_userio *io)
{
	char buf[128];
	char *ptr, *cmd, *args;
	int line_num = 0;
	struct mf_material *mtl = 0;

	while(mf_fgets(buf, sizeof buf, io)) {
		char *line = clean_line(buf);
		++line_num;

		if(!line || !*line) continue;

		cmd = ptr = line;
		while(*ptr && !isspace(*ptr)) ptr++;
		args = *ptr ? clean_line(ptr + 1) : 0;
		*ptr = 0;

		if(strcmp(cmd, "newmtl") == 0) {
			if(mtl) {
				mf_add_material(mf, mtl);
			}
			if(!(mtl = mf_alloc_mtl()) || !(mtl->name = strdup(args))) {
				fprintf(stderr, "failed to allocate material\n");
				mf_free_mtl(mtl);
				mtl = 0;
				return -1;
			}

		} else if(strcmp(cmd, "Kd") == 0) {
			if(!mtl) continue;
			parse_value(mtl->attr + MF_COLOR, args);

		} else if(strcmp(cmd, "Ks") == 0) {
			if(!mtl) continue;
			parse_value(mtl->attr + MF_SPECULAR, args);

		} else if(strcmp(cmd, "Ns") == 0) {
			if(!mtl) continue;
			parse_value(mtl->attr + MF_SHININESS, args);
			/* TODO compute roughness */

		} else if(strcmp(cmd, "d") == 0) {
			if(!mtl) continue;
			if(parse_value(mtl->attr + MF_ALPHA, args) != -1) {
				mtl->attr[MF_TRANSMIT].val.x = 1.0f - mtl->attr[MF_ALPHA].val.x;
			}

		} else if(strcmp(cmd, "Ni") == 0) {
			if(!mtl) continue;
			parse_value(mtl->attr + MF_IOR, args);

		} else if(strcmp(cmd, "map_Kd") == 0) {
			if(!mtl) continue;
			parse_map(mtl->attr + MF_COLOR, args);

		} else if(strcmp(cmd, "map_Ks") == 0) {
			if(!mtl) continue;
			parse_map(mtl->attr + MF_SHININESS, args);

		} else if(strcmp(cmd, "map_d") == 0) {
			if(!mtl) continue;
			parse_map(mtl->attr + MF_ALPHA, args);

		} else if(strcmp(cmd, "bump") == 0) {
			if(!mtl) continue;
			parse_map(mtl->attr + MF_BUMP, args);

		} else if(strcmp(cmd, "refl") == 0) {
			if(!mtl) continue;
			parse_map(mtl->attr + MF_REFLECT, args);

		}
	}

	if(mtl) {
		if(mtl->attr[MF_SHININESS].val.x < 1.0f) {
			mf_vec4 *v = &mtl->attr[MF_SPECULAR].val;
			v->x = v->y = v->z = 0.0f;
		}
		mf_add_material(mf, mtl);
	}
	return 0;
}

static char *clean_line(char *s)
{
	char *end;

	while(*s && isspace(*s)) ++s;
	if(!*s) return 0;

	end = s;
	while(*end && *end != '#') ++end;
	*end-- = 0;

	while(end > s && isspace(*end)) {
		*end-- = 0;
	}

	return s;
}

static char *parse_idx(char *ptr, int *idx, int arrsz)
{
	char *endp;
	int val = strtol(ptr, &endp, 10);
	if(endp == ptr) return 0;

	if(val < 0) {	/* convert negative indices */
		*idx = arrsz + val;
	} else {
		*idx = val - 1;	/* indices in obj are 1-based */
	}
	return endp;
}

/* possible face-vertex definitions:
 * 1. vertex
 * 2. vertex/texcoord
 * 3. vertex//normal
 * 4. vertex/texcoord/normal
 */
static char *parse_face_vert(char *ptr, struct facevertex *fv, int numv, int numt, int numn)
{
	if(!(ptr = parse_idx(ptr, &fv->vidx, numv)))
		return 0;
	if(*ptr != '/') return (!*ptr || isspace(*ptr)) ? ptr : 0;

	if(*++ptr == '/') {	/* no texcoord */
		fv->tidx = -1;
		++ptr;
	} else {
		if(!(ptr = parse_idx(ptr, &fv->tidx, numt)))
			return 0;
		if(*ptr != '/') return (!*ptr || isspace(*ptr)) ? ptr : 0;
		++ptr;
	}

	if(!(ptr = parse_idx(ptr, &fv->nidx, numn)))
		return 0;
	return (!*ptr || isspace(*ptr)) ? ptr : 0;
}

static int cmp_facevert(const void *ap, const void *bp)
{
	const struct facevertex *a = ap;
	const struct facevertex *b = bp;

	if(a->vidx == b->vidx) {
		if(a->tidx == b->tidx) {
			return a->nidx - b->nidx;
		}
		return a->tidx - b->tidx;
	}
	return a->vidx - b->vidx;
}

static void free_rbnode_key(struct rbnode *n, void *cls)
{
	free(n->key);
}


static int write_material(const struct mf_material *mtl, const struct mf_userio *io)
{
	return -1;
}

static int write_mesh(const struct mf_mesh *m, const struct mf_userio *io)
{
	int i;

	for(i=0; i<m->num_verts; i++) {
	}
	return 0;
}

int mf_save_obj(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	int i;

	/*
	for(i=0; i<mf_dynarr_size(mf->mtl); i++) {
		if(write_material(mf->mtl[i], io) == -1) {
			return -1;
		}
	}*/

	for(i=0; i<mf_dynarr_size(mf->meshes); i++) {
		if(write_mesh(mf->meshes[i], io) == -1) {
			return -1;
		}
	}
	return 0;
}
