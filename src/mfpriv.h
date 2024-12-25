#ifndef MFPRIV_H_
#define MFPRIV_H_

#include "meshfile.h"

struct mf_meshfile {
	char *name;
	struct mf_mesh **meshes;
	struct mf_material **mtl;
	mf_aabox aabox;
};

int mf_load_obj(struct mf_meshfile *mf, const struct mf_userio *io);
int mf_save_obj(const struct mf_meshfile *mf, const struct mf_userio *io);

int mf_fgetc(const struct mf_userio *io);
char *mf_fgets(char *buf, int sz, const struct mf_userio *io);

#endif	/* MFPRIV_H_ */
