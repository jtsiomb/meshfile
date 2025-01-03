/* meshconv is a mesh file format converter, written as an example for how to use
 * the meshfile library. The license of meshfile does not apply to this example.
 * I disclaim all copyright for this code, and place it into the public domain.
 * You can use it as a starting point for your own meshfile programs if you wish.
 *
 * Author: John Tsiombikas <nuclear@mutantstargoat.com>
 */

#include <stdio.h>
#include <string.h>
#include "meshfile.h"

int main(int argc, char **argv)
{
	int i, j, fmt = -1;
	const char *typestr[] = {"obj", "jtf"};
	const char *typedesc[] = {"Wavefront OBJ", "Just Triangle Faces"};
	const char *srcfile = 0;
	const char *destfile = 0;
	struct mf_meshfile *mf;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-') {
			if(strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-format") == 0) {
				if(!argv[++i]) {
					fprintf(stderr, "%s must be followed by a file format id, see -l\n", argv[i - 1]);
					return 1;
				}
				for(j=0; j<MF_NUM_FMT; j++) {
					if(strcmp(argv[i], typestr[j]) == 0) {
						fmt = j;
						break;
					}
				}
				if(fmt == -1) {
					fprintf(stderr, "unknown file format: %s\n", argv[i]);
					return 1;
				}

			} else if(strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "-list") == 0) {
				printf("available file formats:\n");
				for(j=0; j<MF_NUM_FMT; j++) {
					printf(" - %s: %s\n", typestr[j], typedesc[j]);
				}
				return 0;

			} else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) {
				printf("Usage: %s [options] <fromfile> <tofile>\n", argv[0]);
				printf("Options:\n");
				printf(" -f,-format <id>: select output file format (default: auto)\n");
				printf(" -l,-list: list available file formats\n");
				printf(" -h,-help: print usage and exit\n\n");
				return 0;

			} else {
				fprintf(stderr, "invalid option: %s. See -h for usage\n", argv[i]);
				return 1;
			}

		} else {

			if(destfile) {
				fprintf(stderr, "unexpected argument: %s\n", argv[i]);
			} else if(srcfile) {
				destfile = argv[i];
			} else {
				srcfile = argv[i];
			}
		}
	}

	if(!srcfile || !destfile) {
		fprintf(stderr, "pass source and destination file paths. See -h for usage\n");
		return 1;
	}

	if(!(mf = mf_alloc())) {
		fprintf(stderr, "failed to allocate meshfile\n");
		return 1;
	}
	if(mf_load(mf, srcfile) == -1) {
		fprintf(stderr, "failed to load: %s\n", srcfile);
		return 1;
	}

	if(fmt != -1) {
		mf_save_format(mf, fmt);
	}
	if(mf_save(mf, destfile) == -1) {
		fprintf(stderr, "failed to save: %s\n", destfile);
		return 1;
	}
	mf_free(mf);
	return 0;
}
