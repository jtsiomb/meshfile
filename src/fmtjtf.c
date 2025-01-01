/*
JTF file format:
Offset	Description
0	Magic bytes 74, 84, 70 and 33 ('JTF!')
4	32bit integer for vertex format (only 0 for now)
8	32bit integer for face count
12	Triangle data (3*face_count vertices)

The vertex format is:
Offset	Description
0	Three 32bit floats for the position
12	Three 32bit floats for the normal
24	Two 32bit floats for the texture coordinate
*/
#include <stdio.h>
#include <string.h>
#include "mfpriv.h"

struct jtf_vertex {
	mf_vec3 pos, norm;
	mf_vec2 uv;
} PACKED;

struct jtf_face {
	struct jtf_vertex v[3];
} PACKED;

struct jtf_header {
	char magic[4];
	uint32_t fmt;
	uint32_t nfaces;
} PACKED;


int mf_load_jtf(struct mf_meshfile *mf, const struct mf_userio *io)
{
	unsigned int i, j, vidx;
	struct jtf_header hdr;
	struct jtf_face face;
	struct mf_mesh *mesh;

	if(io->read(io->file, &hdr, sizeof hdr) < sizeof hdr) {
		return -1;
	}
	if(memcmp(hdr.magic, "JTF!", 4) != 0) {
		return -1;
	}

	if(!(mesh = mf_alloc_mesh())) {
		fprintf(stderr, "jtf: failed to allocate mesh\n");
		return -1;
	}

	vidx = 0;
	for(i=0; i<hdr.nfaces; i++) {
		if(io->read(io->file, &face, sizeof face) < sizeof face) {
			fprintf(stderr, "jtf: unexpected EOF while reading faces\n");
			goto err;
		}

		for(j=0; j<3; j++) {
			if(mf_add_vertex(mesh, face.v[j].pos.x, face.v[j].pos.y, face.v[j].pos.z) == -1) {
				goto err;
			}
			if(mf_add_normal(mesh, face.v[j].norm.x, face.v[j].norm.y, face.v[j].norm.z) == -1) {
				goto err;
			}
			if(mf_add_texcoord(mesh, face.v[j].uv.x, face.v[j].uv.y) == -1) {
				goto err;
			}
		}
		mf_add_triangle(mesh, vidx, vidx + 1, vidx + 2);
		vidx += 3;
	}

	if(mf_add_mesh(mf, mesh) == -1) {
		goto err;
	}
	return 0;

err:
	mf_free_mesh(mesh);
	return -1;
}

int mf_save_jtf(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	unsigned int i, j, k, vidx, total_faces;
	struct jtf_header hdr;
	struct jtf_face face;
	struct mf_mesh *mesh;
	struct mf_face *mff;
	static const mf_vec3 defnorm = {0, 1, 0};
	static const mf_vec2 defuv;

	total_faces = 0;
	for(i=0; i<(unsigned int)mf_num_meshes(mf); i++) {
		mesh = mf_get_mesh(mf, i);
		total_faces += mesh->num_faces;
	}

	memcpy(hdr.magic, "JTF!", 4);
	hdr.fmt = 0;
	hdr.nfaces = total_faces;

	if(io->write(io->file, &hdr, sizeof hdr) < sizeof hdr) {
		fprintf(stderr, "jtf: failed to write header\n");
		return -1;
	}

	for(i=0; i<(unsigned int)mf_num_meshes(mf); i++) {
		mesh = mf_get_mesh(mf, i);
		mff = mesh->faces;
		for(j=0; j<mesh->num_faces; j++) {
			for(k=0; k<3; k++) {
				vidx = mff->vidx[k];
				face.v[k].pos = mesh->vertex[vidx];
				face.v[k].norm = mesh->normal ? mesh->normal[vidx] : defnorm;
				face.v[k].uv = mesh->texcoord ? mesh->texcoord[vidx] : defuv;
			}
			if(io->write(io->file, &face, sizeof face) < sizeof face) {
				fprintf(stderr, "jtf: failed to write faces\n");
				return -1;
			}
		}
	}
	return 0;
}
