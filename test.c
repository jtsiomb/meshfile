#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <GL/glut.h>
#include "imago2.h"
#include "meshfile.h"

static void display(void);
static void draw_mesh(struct mf_mesh *m);
static void setup_material(struct mf_material *mtl);
static void reshape(int x, int y);
static void keypress(unsigned char key, int x, int y);
static void mouse(int bn, int st, int x, int y);
static void motion(int x, int y);

static int win_width, win_height;
static float cam_theta, cam_phi, cam_dist;
static float znear = 0.5, zfar = 500.0;
static int mouse_x, mouse_y;
static int bnstate[8];
static int use_tex;
static struct mf_meshfile *mf;


int main(int argc, char **argv)
{
	int i, width, height;
	unsigned int tex;
	void *pixels;
	struct mf_material *mtl;
	const char *map;
	mf_aabox bbox;
	float dx, dy, dz;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutInitWindowSize(800, 600);
	glutCreateWindow("meshfile library test");

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keypress);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	if(!(mf = mf_alloc()) || mf_load(mf, argv[1] ? argv[1] : "test.obj") == -1) {
		fprintf(stderr, "failed to load test.obj\n");
		return 1;
	}
	mf_bounds(mf, &bbox);

	dx = bbox.vmax.x - bbox.vmin.x;
	dy = bbox.vmax.y - bbox.vmin.y;
	dz = bbox.vmax.z - bbox.vmin.z;

	cam_dist = sqrt(dx * dx + dy * dy + dz * dz) * 1.5f;
	zfar = cam_dist * 4.0f;
	znear = zfar * 0.001;
	if(znear < 0.1) znear = 0.1;
	/* force recalc projection */
	if(win_width > 0) reshape(win_width, win_height);

	/* load any textures */
	for(i=0; i<mf_num_materials(mf); i++) {
		mtl = mf_get_material(mf, i);
		map = mtl->attr[MF_COLOR].map.name;

		if(map && (pixels = img_load_pixels(map, &width, &height, IMG_FMT_RGBA32))) {
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

			img_free_pixels(pixels);
			mtl->attr[MF_COLOR].udata = (void*)(uintptr_t)tex;
		}
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);

	glutMainLoop();
	return 0;
}

static void display(void)
{
	int i;
	struct mf_mesh *mesh;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0, 0, -cam_dist);
	glRotatef(cam_phi, 1, 0, 0);
	glRotatef(cam_theta, 0, 1, 0);

	for(i=0; i<mf_num_meshes(mf); i++) {
		mesh = mf_get_mesh(mf, i);
		setup_material(mesh->mtl);
		draw_mesh(mf_get_mesh(mf, i));
	}

	glutSwapBuffers();
	assert(glGetError() == GL_NO_ERROR);
}

static void draw_mesh(struct mf_mesh *m)
{
	int i, j, vidx, dlist = (intptr_t)m->udata;
	mf_face *f;

	if(!dlist) {
		dlist = glGenLists(1);
		glNewList(dlist, GL_COMPILE);

		if(m->faces) {
			for(i=0; i<m->num_faces; i++) {
				f = m->faces + i;
				for(j=0; j<f->nverts; j++) {
					vidx = f->vidx[j];
					if(m->normal) {
						glNormal3fv(&m->normal[vidx].x);
					}
					if(m->texcoord) {
						glTexCoord2fv(&m->texcoord[vidx].x);
					}
					if(m->color) {
						glColor4fv(&m->color[vidx].x);
					}
					glVertex3fv(&m->vertex[vidx].x);
				}
			}
		} else {
			for(i=0; i<m->num_verts; i++) {
				if(m->normal) {
					glNormal3fv(&m->normal[i].x);
				}
				if(m->texcoord) {
					glTexCoord2fv(&m->texcoord[i].x);
				}
				if(m->color) {
					glColor4fv(&m->color[i].x);
				}
				glVertex3fv(&m->vertex[i].x);
			}
		}
		glEndList();
		m->udata = (void*)(intptr_t)dlist;
	}

	glCallList(dlist);
}

static void setup_material(struct mf_material *mtl)
{
	unsigned int tex;

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mtl->attr[MF_COLOR].val.x);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &mtl->attr[MF_SPECULAR].val.x);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mtl->attr[MF_SHININESS].val.x);

	if(mtl->attr[MF_COLOR].udata && use_tex) {
		tex = (uintptr_t)mtl->attr[MF_COLOR].udata;
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);
	} else {
		glDisable(GL_TEXTURE_2D);
	}
}

static void reshape(int x, int y)
{
	win_width = x;
	win_height = y;

	glViewport(0, 0, x, y);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(50.0, (float)x / (float)y, znear, zfar);
}

static void keypress(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);

	case 't':
		use_tex ^= 1;
		break;

	default:
		break;
	}
}

static void mouse(int bn, int st, int x, int y)
{
	int bidx = bn - GLUT_LEFT_BUTTON;
	int press = st == GLUT_DOWN;

	if(bidx < 8) {
		bnstate[bidx] = press;
	}
	mouse_x = x;
	mouse_y = y;
}

static void motion(int x, int y)
{
	int dx = x - mouse_x;
	int dy = y - mouse_y;
	mouse_x = x;
	mouse_y = y;

	if((dx | dy) == 0) return;

	if(bnstate[0]) {
		cam_theta += dx * 0.5f;
		cam_phi += dy * 0.5f;

		if(cam_phi < -90) cam_phi = -90;
		if(cam_phi > 90) cam_phi = 90;
		glutPostRedisplay();
	}
	if(bnstate[2]) {
		cam_dist += dy * 0.1f;

		if(cam_dist < 0) cam_dist = 0;
		glutPostRedisplay();
	}
}
