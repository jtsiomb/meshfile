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
#include "mfpriv.h"
#include "dynarr.h"
#include "util.h"


static int read_float(float *ret, const struct mf_userio *io);
static int read_vector(mf_vec3 *vec, const struct mf_userio *io);

static int write_vec(mf_vec3 v, const struct mf_userio *io);
static int write_mesh(const struct mf_mesh *mesh, const float *mat, const struct mf_userio *io);

int mf_load_stl(struct mf_meshfile *mf, const struct mf_userio *io)
{
	long filesz;
	uint32_t i, j, nfaces, vidx = 0;
	struct mf_mesh *mesh = 0;
	struct mf_node *node = 0;
	mf_vec3 norm, vpos;

	filesz = io->seek(io->file, 0, MF_SEEK_END);
	io->seek(io->file, 80, MF_SEEK_SET);	/* skip header */

	if(io->read(io->file, &nfaces, sizeof nfaces) < sizeof nfaces) {
		return -1;
	}
	CONV_LE32(nfaces);

	if(nfaces * 50 + 84 != filesz) {
		return -1;
	}

	if(!(mesh = mf_alloc_mesh())) {
		fprintf(stderr, "load_stl: failed to allocate mesh\n");
		goto err;
	}
	if(!(node = mf_alloc_node())) {
		fprintf(stderr, "load_stl: failed to allocate node\n");
		goto err;
	}

	for(i=0; i<nfaces; i++) {
		if(read_vector(&norm, io) == -1) {
			fprintf(stderr, "load_stl: failed to read normal\n");
			goto err;
		}
		for(j=0; j<3; j++) {
			if(mf_add_normal(mesh, norm.x, norm.y, norm.z) == -1) {
				fprintf(stderr, "load_stl: failed to add normal\n");
				goto err;
			}
			if(read_vector(&vpos, io) == -1) {
				fprintf(stderr, "load_stl: failed to read vertex\n");
				goto err;
			}
			if(mf_add_vertex(mesh, vpos.x, vpos.y, vpos.z) == -1) {
				fprintf(stderr, "load_stl: failed to add vertex\n");
				goto err;
			}
		}

		if(mf_add_triangle(mesh, vidx, vidx + 2, vidx + 1) == -1) {
			fprintf(stderr, "load_stl: failed to add face\n");
		}
		vidx += 3;
		io->seek(io->file, 2, MF_SEEK_CUR);	/* skip attribute byte count */
	}

	if(mf_node_add_mesh(node, mesh) == -1) {
		fprintf(stderr, "load_stl: failed to add mesh to node\n");
		goto err;
	}
	if(mf_add_mesh(mf, mesh) == -1) {
		fprintf(stderr, "load_stl: failed to add mesh\n");
		goto err;
	}
	if(mf_add_node(mf, node) == -1) {
		fprintf(stderr, "load_stl: failed to add node\n");
		goto err;
	}
	return 0;

err:
	mf_free_mesh(mesh);
	mf_free_node(node);
	return -1;
}


static int read_float(float *ret, const struct mf_userio *io)
{
	if(io->read(io->file, ret, sizeof(float)) < sizeof(float)) {
		return -1;
	}
	CONV_LEFLT(ret);
	return 0;
}

static int read_vector(mf_vec3 *vec, const struct mf_userio *io)
{
	if(read_float(&vec->x, io) == -1 || read_float(&vec->z, io) == -1 ||
			read_float(&vec->y, io) == -1) {
		return -1;
	}
	return 0;
}

static const char id[] = "STL written by meshfile";
int mf_save_stl(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	unsigned int i, j, num_nodes;
	char buf[80];
	uint32_t total_faces = 0;
	struct mf_node *node;

	for(i=0; i<80; i++) {
		buf[i] = id[i % sizeof id] ? id[i % sizeof id] : ' ';
	}
	if(io->write(io->file, buf, sizeof buf) < sizeof buf) {
		fprintf(stderr, "save_stl: failed to write header\n");
		return -1;
	}

	num_nodes = mf_num_nodes(mf);
	for(i=0; i<num_nodes; i++) {
		node = mf_get_node(mf, i);
		for(j=0; j<node->num_meshes; j++) {
			total_faces += node->meshes[j]->num_faces;
		}
	}
	CONV_LE32(total_faces);
	if(io->write(io->file, &total_faces, sizeof total_faces) < sizeof total_faces) {
		fprintf(stderr, "save_stl: failed to write face count\n");
		return -1;
	}

	for(i=0; i<num_nodes; i++) {
		node = mf_get_node(mf, i);
		for(j=0; j<node->num_meshes; j++) {
			if(write_mesh(node->meshes[j], node->global_matrix, io) == -1) {
				fprintf(stderr, "save_stl: failed to write mesh\n");
				return -1;
			}
		}
	}
	return 0;
}

static int write_vec(mf_vec3 v, const struct mf_userio *io)
{
	CONV_LEFLT(v.x);
	CONV_LEFLT(v.y);
	CONV_LEFLT(v.z);
	if(io->write(io->file, &v.x, sizeof v.x) < sizeof v.x) {
		return -1;
	}
	if(io->write(io->file, &v.z, sizeof v.z) < sizeof v.z) {
		return -1;
	}
	if(io->write(io->file, &v.y, sizeof v.y) < sizeof v.y) {
		return -1;
	}
	return 0;
}

static int write_mesh(const struct mf_mesh *mesh, const float *mat, const struct mf_userio *io)
{
	unsigned int i, j;
	mf_vec3 va, vb, v[3], norm;
	struct mf_face *face;
	static uint16_t zero;

	for(i=0; i<mesh->num_faces; i++) {
		face = mesh->faces + i;
		for(j=0; j<3; j++) {
			mf_transform(v + j, mesh->vertex + face->vidx[j], mat);
		}
		mf_vsub(&va, v + 1, v);
		mf_vsub(&vb, v + 2, v);
		mf_cross(&norm, &va, &vb);
		mf_normalize(&norm);

		if(write_vec(norm, io) == -1) {
			return -1;
		}
		if(write_vec(v[0], io) == -1) return -1;
		if(write_vec(v[2], io) == -1) return -1;
		if(write_vec(v[1], io) == -1) return -1;
		io->write(io->file, &zero, 2);
	}
	return 0;
}
