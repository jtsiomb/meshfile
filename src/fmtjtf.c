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
#include <string.h>
#include "mfpriv.h"

struct jtf_face {
	mf_vec3 pos, norm;
	mf_vec2 uv;
} PACKED;

struct jtf_header {
	char magic[4];
	uint32_t fmt;
	uint32_t nfaces;
} PACKED;


int mf_load_jtf(struct mf_meshfile *mf, const struct mf_userio *io)
{
	unsigned int i;
	struct jtf_header hdr;

	if(io->read(io->file, &hdr, sizeof hdr) < sizeof hdr) {
		return -1;
	}
	if(memcmp(hdr.magic, "JTF!", 4) != 0) {
		return -1;
	}

	return -1;
}

int mf_save_jtf(const struct mf_meshfile *mf, const struct mf_userio *io)
{
	return -1;
}
