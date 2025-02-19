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
#include <string.h>
#include <ctype.h>
#include "mfpriv.h"
#include "rbtree.h"
#include "dynarr.h"
#include "util.h"


struct facevertex {
	int vidx, tidx, nidx;
};

static int mesh_done(struct mf_meshfile *mf, struct mf_mesh *mesh);
static int load_mtl(struct mf_meshfile *mf, const struct mf_userio *io);
static char *clean_line(char *s);
static char *parse_face_vert(char *ptr, struct facevertex *fv, int numv, int numt, int numn);
static int cmp_facevert(const void *ap, const void *bp);
static void free_rbnode_key(struct rbnode *n, void *cls);


int mf_load_obj(struct mf_meshfile *mf, const struct mf_userio *io)
{
	char buf[128];
	int result = -1;
	int i, line_num = 0;
	mf_vec3 *varr = 0, *narr = 0;
	mf_vec2 *tarr = 0;
	struct rbtree *rbtree = 0;
	struct mf_mesh *mesh = 0;
	struct mf_userio subio = {0};

	subio.open = io->open;
	subio.close = io->close;
	subio.read = io->read;

	if(!mf->name && !(mf->name = strdup("<unknown>"))) {
		fprintf(stderr, "mf_load_userio: failed to allocate name\n");
		return -1;
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
	if(!(mesh->name = strdup(mf->name))) {
		fprintf(stderr, "mf_load: failed to allocate mesh name\n");
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
		case 'g':
			if(mesh_done(mf, mesh) != -1 && !(mesh = mf_alloc_mesh())) {
				fprintf(stderr, "mf_load: failed to allocate mesh\n");
				goto end;
			}
			mesh->name = clean_line(line + 1);
			if(!(mesh->name = strdup(mesh->name ? mesh->name : "unnamed mesh"))) {
				fprintf(stderr, "mf_load: failed to allocate mesh name\n");
				goto end;
			}
			break;

		default:
			if(memcmp(line, "mtllib", 6) == 0) {
				const char *mtlfile = clean_line(line + 6);
				if(!mtlfile) {
					fprintf(stderr, "mf_load: ignoring invalid mtllib\n");
					continue;
				}
				mtlfile = mf_find_asset(mf, mtlfile);

				if((subio.file = io->open(mtlfile, "rb"))) {
					load_mtl(mf, &subio);
					io->close(subio.file);
				} else {
					fprintf(stderr, "mf_load: failed to open material library: %s, ignoring\n", mtlfile);
				}

			} else if(memcmp(line, "usemtl", 6) == 0) {
				struct mf_material *mtl = mf_find_material(mf, clean_line(line + 6));
				if(mtl) mesh->mtl = mtl;
			}
			break;
		}
	}

	mesh_done(mf, mesh);
	mesh = 0;

	if(!mf_dynarr_empty(mf->meshes)) {
		result = 0;	/* success */
	}

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
	struct mf_node *node = 0;

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

	/* also allocate a node for it */
	if(!(node = mf_alloc_node())) {
		fprintf(stderr, "mf_load: failed to allocate mesh node\n");
		goto reset_mesh;
	}
	if(!(node->name = strdup(mesh->name))) {
		fprintf(stderr, "load_obj: failed to allocate node name\n");
		goto reset_mesh;
	}

	if(mf_node_add_mesh(node, mesh) == -1) {
		fprintf(stderr, "mf_load: failed to add mesh to node\n");
		goto reset_mesh;
	}

	if(mf_add_mesh(mf, mesh) == -1) {
		fprintf(stderr, "mf_load: failed to add mesh\n");
		goto reset_mesh;
	}
	mf_add_node(mf, node);

	return 0;

reset_mesh:
	mf_free_node(node);
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

static char *nextarg(char **s)
{
	char *arg, *end = *s;

	if(!*s || !**s) return 0;

	while(*end && !isspace(*end)) end++;
	while(*end && isspace(*end)) *end++ = 0;

	arg = *s;
	*s = end;
	return arg;
}

static int parse_bool(const char *s)
{
	if(strcmp(s, "on") == 0) return 1;
	if(strcmp(s, "off") == 0) return 0;
	return -1;
}

static int parse_float(const char *s, float *ret)
{
	char *endp;
	float x = strtod(s, &endp);
	if(endp == s) return -1;
	*ret = x;
	return 0;
}

static const char *facename[] = {
	"cube_top", "cube_bottom",
	"cube_front", "cube_back",
	"cube_left", "cube_right"
};

static int parse_map(struct mf_mtlattr *attr, char *args)
{
	int i, cubeface = -1;
	char *arg, *val, *prev;
	int bval;
	float fval;
	mf_vec3 pos = {0, 0, 0}, scale = {1, 1, 1};
	struct mf_texmap *map = &attr->map;

	while((arg = nextarg(&args))) {
		if(arg[0] == '-') {
			if(!(val = nextarg(&args))) {
invalopt:		fprintf(stderr, "ignoring invalid %s option in map: %s\n", arg, val);
				continue;
			}
			if(strcmp(arg, "-blendu") == 0) {
				if((bval = parse_bool(val)) == -1) goto invalopt;
				map->ufilt = bval ? MF_TEX_LINEAR : MF_TEX_NEAREST;

			} else if(strcmp(arg, "-blendv") == 0) {
				if((bval = parse_bool(val)) == -1) goto invalopt;
				map->vfilt = bval ? MF_TEX_LINEAR : MF_TEX_NEAREST;

			} else if(strcmp(arg, "-clamp") == 0) {
				if((bval = parse_bool(val)) == -1) goto invalopt;
				map->uwrap = map->vwrap = bval ? MF_TEX_CLAMP : MF_TEX_REPEAT;

			} else if(strcmp(arg, "-bm") == 0) {
				if(attr->type != MF_BUMP) {
					fprintf(stderr, "ignoring -bm option in non-bump attribute\n");
					continue;
				}
				if(parse_float(val, &fval) == -1) {
					fprintf(stderr, "ignoring invalid -bm value: %s\n", val);
					continue;
				}
				attr->val.x = attr->val.y = attr->val.z = fval;

			} else if(strcmp(arg, "-o") == 0 || strcmp(arg, "-s") == 0) {
				mf_vec3 *vptr = arg[1] == '0' ? &pos : &scale;
				if(parse_float(val, &vptr->x) == -1) {
					fprintf(stderr, "ignoring invalid %s value: %s\n", arg, val);
					continue;
				}
				prev = args;
				val = nextarg(&args);
				if(parse_float(val, &vptr->y) == -1) {
					args = prev;
					continue;
				}
				prev = args;
				val = nextarg(&args);
				if(parse_float(val, &vptr->z) == -1) {
					args = prev;
					continue;
				}

			} else if(strcmp(arg, "-type") == 0 && attr->type == MF_REFLECT) {
				cubeface = -1;
				for(i=0; i<6; i++) {
					if(strcmp(val, facename[i]) == 0) {
						cubeface = i;
						break;
					}
				}

			}


		} else {
			if(cubeface == -1) {
				free(map->name);
				if(!(map->name = strdup(arg))) {
					fprintf(stderr, "failed to allocate map name: %s\n", arg);
					continue;
				}
			} else {
				free(map->cube[cubeface]);
				if(!(map->cube[cubeface] = strdup(arg))) {
					fprintf(stderr, "failed to allocate cubemap name: %s\n", arg);
					continue;
				}
				cubeface = -1;
			}
		}
	}

	map->offset = pos;
	map->scale = scale;
	return 0;
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
		} else {
			if(!mtl) continue;
		}

		if(strcmp(cmd, "Kd") == 0) {
			parse_value(mtl->attr + MF_COLOR, args);

		} else if(strcmp(cmd, "Ks") == 0) {
			parse_value(mtl->attr + MF_SPECULAR, args);

		} else if(strcmp(cmd, "Ke") == 0) {
			parse_value(mtl->attr + MF_EMISSIVE, args);

		} else if(strcmp(cmd, "Ns") == 0) {
			parse_value(mtl->attr + MF_SHININESS, args);

		} else if(strcmp(cmd, "d") == 0) {
			if(parse_value(mtl->attr + MF_ALPHA, args) != -1) {
				mtl->attr[MF_TRANSMIT].val.x = 1.0f - mtl->attr[MF_ALPHA].val.x;
			}

		} else if(strcmp(cmd, "Ni") == 0) {
			parse_value(mtl->attr + MF_IOR, args);

		} else if(strcmp(cmd, "Pr") == 0) {
			parse_value(mtl->attr + MF_ROUGHNESS, args);

		} else if(strcmp(cmd, "Pm") == 0) {
			parse_value(mtl->attr + MF_METALLIC, args);

		} else if(strcmp(cmd, "map_Kd") == 0) {
			parse_map(mtl->attr + MF_COLOR, args);

		} else if(strcmp(cmd, "map_Ks") == 0) {
			parse_map(mtl->attr + MF_SHININESS, args);

		} else if(strcmp(cmd, "map_d") == 0) {
			parse_map(mtl->attr + MF_ALPHA, args);

		} else if(strcmp(cmd, "bump") == 0 || mf_strcasecmp(cmd, "map_bump") == 0) {
			parse_map(mtl->attr + MF_BUMP, args);

		} else if(strcmp(cmd, "refl") == 0) {
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
	fv->vidx = fv->tidx = fv->nidx = -1;

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

static void print_map(const char *cmd, const struct mf_mtlattr *attr, const struct mf_userio *io)
{
	int i;
	const struct mf_texmap *map = &attr->map;

	for(i=0; i<6; i++) {
		mf_fprintf(io, "%s", cmd);
		if(map->ufilt != MF_TEX_LINEAR) {
			mf_fputs(" -blendu off", io);
		}
		if(map->vfilt != MF_TEX_LINEAR) {
			mf_fputs(" -blendv off", io);
		}
		if(map->uwrap != MF_TEX_REPEAT) {
			mf_fputs(" -clamp on", io);
		}
		if(map->offset.x != 0.0f || map->offset.y != 0.0f || map->offset.z != 0.0f) {
			mf_fprintf(io, " -o %f %f %f", map->offset.x, map->offset.y, map->offset.z);
		}
		if(map->scale.x != 1.0f || map->scale.y != 1.0f || map->scale.z != 1.0f) {
			mf_fprintf(io, " -s %f %f %f", map->scale.x, map->scale.y, map->scale.z);
		}

		if(attr->type == MF_BUMP) {
			if(attr->val.x != 1.0f) {
				mf_fprintf(io, " -bm %f", attr->val.x);
			}
		}

		if(attr->type == MF_REFLECT) {
			if(map->name) {
				mf_fprintf(io, " -type sphere %s\n", map->name);
				break;
			}
			if(map->cube[i]) {
				mf_fprintf(io, " -type %s %s\n", facename[i], map->cube[i]);
			}
		} else {
			if(map->name) {
				mf_fprintf(io, " %s\n", map->name);
			}
			break;
		}
	}
}

#define NONZEROVEC(v)	((v).x != 0.0f || (v).y != 0.0f || (v).z != 0.0f)
#define PRINTVEC3(name, v) \
	mf_fprintf(io, "%s %f %f %f\n", (name), (v).x, (v).y, (v).z)

static int write_material(const struct mf_material *mtl, const struct mf_userio *io)
{
	mf_fprintf(io, "newmtl %s\n", mtl->name);
	PRINTVEC3("Kd", mtl->attr[MF_COLOR].val);
	PRINTVEC3("Ks", mtl->attr[MF_SPECULAR].val);
	mf_fprintf(io, "Ns %f\n", mtl->attr[MF_SHININESS].val.x);
	if(NONZEROVEC(mtl->attr[MF_EMISSIVE].val)) {
		PRINTVEC3("Ke", mtl->attr[MF_EMISSIVE].val);
	}
	if(NONZEROVEC(mtl->attr[MF_TRANSMIT].val)) {
		PRINTVEC3("Tf", mtl->attr[MF_TRANSMIT].val);
	}
	if(mtl->attr[MF_IOR].val.x != 1.0f) {
		mf_fprintf(io, "Ni %f\n", mtl->attr[MF_IOR].val.x);
	}
	mf_fprintf(io, "d %f\n", mtl->attr[MF_ALPHA].val.x);

	if(mtl->attr[MF_ROUGHNESS].val.x != 1.0) {
		mf_fprintf(io, "Pr %f\n", mtl->attr[MF_ROUGHNESS].val.x);
	}
	if(mtl->attr[MF_METALLIC].val.x != 0.0f) {
		mf_fprintf(io, "Pm %f\n", mtl->attr[MF_METALLIC].val.x);
	}

	if(mtl->attr[MF_COLOR].map.name) {
		print_map("map_Kd", mtl->attr + MF_COLOR, io);
	}
	if(mtl->attr[MF_SPECULAR].map.name) {
		print_map("map_Ks", mtl->attr + MF_SPECULAR, io);
	}
	if(mtl->attr[MF_EMISSIVE].map.name) {
		print_map("map_Ke", mtl->attr + MF_EMISSIVE, io);
	}
	if(mtl->attr[MF_SHININESS].map.name) {
		print_map("map_Ns", mtl->attr + MF_SHININESS, io);
	}
	if(mtl->attr[MF_ALPHA].map.name) {
		print_map("map_d", mtl->attr + MF_ALPHA, io);
	}
	if(mtl->attr[MF_REFLECT].map.name || mtl->attr[MF_REFLECT].map.cube[0]) {
		print_map("refl", mtl->attr + MF_REFLECT, io);
	}
	if(mtl->attr[MF_BUMP].map.name) {
		print_map("bump", mtl->attr + MF_BUMP, io);
	}
	if(mtl->attr[MF_ROUGHNESS].map.name) {
		print_map("map_Pr", mtl->attr + MF_ROUGHNESS, io);
	}
	if(mtl->attr[MF_METALLIC].map.name) {
		print_map("map_Pm", mtl->attr + MF_METALLIC, io);
	}
	mf_fputc('\n', io);
	return 0;
}

static int face_vref(const struct mf_mesh *m, unsigned long vidx, char *buf)
{
	int len = sprintf(buf, " %lu", ++vidx);
	if(m->texcoord) {
		len += sprintf(buf + len, "/%lu", vidx);
	}
	if(m->normal) {
		if(!m->texcoord) {
			buf[len++] = '/';
		}
		len += sprintf(buf + len, "/%lu", vidx);
	}
	return len;
}

static int write_mesh(const struct mf_mesh *m, unsigned long voffs, const struct mf_userio *io)
{
	int i, j;
	mf_vec3 *vptr = m->vertex;
	char buf[128], *ptr;

	mf_fprintf(io, "o %s\n", m->name);
	mf_fprintf(io, "usemtl %s\n", m->mtl->name);

	for(i=0; i<m->num_verts; i++) {
		mf_fprintf(io, "v %f %f %f\n", vptr->x, vptr->y, vptr->z);
		vptr++;
	}
	if(m->normal) {
		mf_vec3 *nptr = m->normal;
		for(i=0; i<m->num_verts; i++) {
			mf_fprintf(io, "vn %f %f %f\n", nptr->x, nptr->y, nptr->z);
			nptr++;
		}
	}
	if(m->texcoord) {
		mf_vec2 *tptr = m->texcoord;
		for(i=0; i<m->num_verts; i++) {
			mf_fprintf(io, "vt %f %f\n", tptr->x, tptr->y);
			tptr++;
		}
	}

	if(m->faces) {
		mf_face *fptr = m->faces;
		for(i=0; i<m->num_faces; i++) {
			ptr = buf;
			*ptr++ = 'f';

			for(j=0; j<3; j++) {
				ptr += face_vref(m, voffs + fptr->vidx[j], ptr);
			}
			*ptr++ = '\n';
			*ptr = 0;
			mf_fputs(buf, io);
			fptr++;
		}
	} else {
		for(i=0; i<m->num_faces; i++) {
			ptr = buf;
			*ptr++ = 'f';
			*ptr++ = ' ';

			for(j=0; j<3; j++) {
				ptr += face_vref(m, voffs++, ptr);
			}
			*ptr++ = '\n';
			*ptr = 0;
			mf_fputs(buf, io);
		}
	}
	return 0;
}

static const char *basename(const char *s)
{
	const char *res = strrchr(s, '/');
	return res ? res : s;
}

int mf_save_obj(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	int i;
	char *mtlpath, *fname, *suffix;
	unsigned long voffs = 0;
	struct mf_userio subio = {0};

	mf_fputs("# OBJ file written by libmeshfile: https://github.com/jtsiomb/meshfile\n", io);
	mf_fputs("csh -xeyes\n", io);

	if(mf_dynarr_empty(mf->mtl) || !io->open) {
		goto geom;	/* skip materials */
	}

	fname = (char*)basename(mf->name);
	if(mf->dirname) {
		if(!(mtlpath = malloc(strlen(mf->dirname) + strlen(fname) + 5))) {
			goto geom;
		}
		sprintf(mtlpath, "%s/%s", mf->dirname, fname);
	} else {
		if(!(mtlpath = malloc(strlen(fname) + 4))) {
			goto geom;
		}
		strcpy(mtlpath, fname);
	}
	if(!(suffix = strrchr(mtlpath, '.'))) {
		suffix = mtlpath + strlen(mtlpath);
	}
	strcpy(suffix, ".mtl");

	if(!(subio.file = io->open(mtlpath, "wb"))) {
		fprintf(stderr, "failed to open %s for writing\n", mtlpath);
		goto geom;
	}
	subio.close = io->close;
	subio.write = io->write;

	for(i=0; i<mf_dynarr_size(mf->mtl); i++) {
		if(write_material(mf->mtl[i], &subio) == -1) {
			io->close(subio.file);
			goto geom;
		}
	}

	io->close(subio.file);

	mf_fprintf(io, "mtllib %s\n", basename(mtlpath));

geom:
	for(i=0; i<mf_dynarr_size(mf->meshes); i++) {
		if(write_mesh(mf->meshes[i], voffs, io) == -1) {
			return -1;
		}
		voffs += mf->meshes[i]->num_verts;
	}
	return 0;
}
