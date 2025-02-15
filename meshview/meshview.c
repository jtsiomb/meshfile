/* meshview is a mesh file format viewer, written as an example for how to use
 * the meshfile library. The license of meshfile does not apply to this example.
 * I disclaim all copyright for this code, and place it into the public domain.
 * You can use it as a starting point for your own meshfile programs if you wish.
 *
 * Note that a typical non-trivial 3D program would grab the necessary data from
 * meshfile and put them in custom data structures. This simple viewer uses the
 * meshfile data structures directly instead.
 *
 * Author: John Tsiombikas <nuclear@mutantstargoat.com>
 */
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <GL/glut.h>
#include "imago2.h"
#include "meshfile.h"

#define FONT	GLUT_BITMAP_HELVETICA_18

static int init(void);
static void cleanup(void);
static void display(void);
static void draw_mesh(struct mf_mesh *m);
static int pre_draw(struct mf_mesh *m, int pass);
static void reset_view(void);
static void draw_aabox(const mf_aabox *aabb);
static void reshape(int x, int y);
static void keypress(unsigned char key, int x, int y);
static void skeypress(int key, int x, int y);
static void mouse(int bn, int st, int x, int y);
static void motion(int x, int y);
static unsigned int load_texture(const char *fname);
static void glprintf(int x, int y, const char *fmt, ...);
static int parse_args(int argc, char **argv);

static const char *fname, *basename;
static int win_width, win_height;
static float cam_theta, cam_phi, cam_dist;
static float cam_pos[3];
static float znear = 0.5, zfar = 500.0;
static int mouse_x, mouse_y;
static int bnstate[8];
static int wire, zup, use_tex = 1;
static int use_nodes = 1;
static int show_bounds;
static int tex_modulate;
static struct mf_meshfile *mf;

static long total_faces;


int main(int argc, char **argv)
{
	glutInit(&argc, argv);

	if(parse_args(argc, argv) == -1) {
		return 1;
	}

	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutInitWindowSize(800, 600);
	glutCreateWindow("meshfile 3D viewer");

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keypress);
	glutSpecialFunc(skeypress);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	if(init() == -1) {
		return 1;
	}
	atexit(cleanup);

	glutMainLoop();
	return 0;
}

static int init(void)
{
	int i;
	struct mf_material *mtl;
	const char *map;

	if(!fname) {
		fprintf(stderr, "pass mesh file to open\n");
		return -1;
	}

	if(!(mf = mf_alloc()) || mf_load(mf, fname, 0) == -1) {
		fprintf(stderr, "failed to load %s\n", fname);
		return -1;
	}
	reset_view();

	total_faces = 0;
	for(i=0; i<mf_num_meshes(mf); i++) {
		struct mf_mesh *m = mf_get_mesh(mf, i);
		total_faces += m->num_faces;
	}

	/* load any textures */
	for(i=0; i<mf_num_materials(mf); i++) {
		mtl = mf_get_material(mf, i);
		if((map = mf_find_asset(mf, mtl->attr[MF_COLOR].map.name))) {
			if(!(mtl->attr[MF_COLOR].udata = (void*)load_texture(map))) {
				fprintf(stderr, "failed to load texture: %s\n", map);
			} else {
				printf("loaded texture: %s\n", map);
			}
		}
		if((map = mf_find_asset(mf, mtl->attr[MF_REFLECT].map.name))) {
			if(!(mtl->attr[MF_REFLECT].udata = (void*)load_texture(map))) {
				fprintf(stderr, "failed to load envmap: %s\n", map);
			} else {
				printf("loaded envmap: %s\n", map);
			}
		}
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);

	return 0;
}

static void cleanup(void)
{
	mf_free(mf);
}

static void render_node_tree(struct mf_node *n)
{
	int i, pass;

	glPushMatrix();
	glMultMatrixf(n->matrix);

	for(i=0; i<n->num_meshes; i++) {
		pass = 0;
		while(pre_draw(n->meshes[i], pass++)) {
			draw_mesh(n->meshes[i]);
		}
	}

	for(i=0; i<n->num_child; i++) {
		render_node_tree(n->child[i]);
	}

	glPopMatrix();
}

static void display(void)
{
	int i, pass, x;
	struct mf_mesh *mesh;
	mf_aabox aabb;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0, 0, -cam_dist);
	glRotatef(cam_phi, 1, 0, 0);
	glRotatef(cam_theta, 0, 1, 0);
	if(zup) glRotatef(90, 1, 0, 0);
	glTranslatef(-cam_pos[0], -cam_pos[1], -cam_pos[2]);

	if(use_nodes) {
		for(i=0; i<mf_num_topnodes(mf); i++) {
			render_node_tree(mf_get_topnode(mf, i));
		}
	} else {
		for(i=0; i<mf_num_meshes(mf); i++) {
			pass = 0;
			mesh = mf_get_mesh(mf, i);
			while(pre_draw(mesh, pass++)) {
				draw_mesh(mesh);
			}
		}
	}

	if(show_bounds) {
		glPushAttrib(GL_ENABLE_BIT);
		glDisable(GL_LIGHTING);

		glColor3f(1, 0.6, 0.1);
		mf_bounds(mf, &aabb);
		draw_aabox(&aabb);

		glPopAttrib();
	}

	glColor3f(0, 1, 0);
	glprintf(10, 20, "%s - %d meshes, %ld polygons", basename, mf_num_meshes(mf),
			total_faces);
	glprintf(10, 40, "nodes: %ld roots (%ld total)", mf_num_topnodes(mf), mf_num_nodes(mf));

	x = -(win_width - 10);	/* negative for right-align */
	glprintf(x, 20, "draw mode: %s", use_nodes ? "nodes" : "meshes");
	glprintf(x, 40, "view pos: %.2f %.2f %.2f, dist: %.1f", cam_pos[0], cam_pos[1], cam_pos[2], cam_dist);

	glutSwapBuffers();
	assert(glGetError() == GL_NO_ERROR);
}

static void draw_mesh(struct mf_mesh *m)
{
	int i, j, vidx, dlist = (int)m->udata;
	mf_face *f;

	if(!dlist) {
		dlist = glGenLists(1);
		glNewList(dlist, GL_COMPILE);

		glBegin(GL_TRIANGLES);
		if(m->faces) {
			for(i=0; i<m->num_faces; i++) {
				f = m->faces + i;
				for(j=0; j<3; j++) {
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
		glEnd();
		glEndList();
		m->udata = (void*)dlist;
	}

	glCallList(dlist);
}

static int pre_draw(struct mf_mesh *m, int pass)
{
	static const float white[] = {1, 1, 1, 1};
	static const float black[] = {0, 0, 0, 1};
	unsigned int tex;
	struct mf_material *mtl = m->mtl;

	switch(pass) {
	case 0:
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mtl->attr[MF_COLOR].val.x);
		glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &mtl->attr[MF_SPECULAR].val.x);
		glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mtl->attr[MF_SHININESS].val.x);

		if(mtl->attr[MF_COLOR].udata && use_tex) {
			tex = (unsigned int)mtl->attr[MF_COLOR].udata;
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, tex);

			if(!tex_modulate) {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white);
			}
		}
		return 1;

	case 1:
		if(mtl->attr[MF_REFLECT].udata && use_tex) {
			/* envmap pass */
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			glDepthFunc(GL_LEQUAL);

			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, black);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
			glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, white);

			tex = (unsigned int)mtl->attr[MF_REFLECT].udata;
			glEnable(GL_TEXTURE_2D);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			return 1;
		}
		glDisable(GL_TEXTURE_2D);
		break;

	case 2:
		glDisable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
		glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
		glDepthFunc(GL_LESS);
		break;

	default:
		break;
	}
	return 0;
}

static void reset_view(void)
{
	mf_aabox bbox;
	float dx, dy, dz;

	cam_theta = cam_phi = 0;

	if(mf_bounds(mf, &bbox) != -1) {
		cam_pos[0] = (bbox.vmin.x + bbox.vmax.x) * 0.5f;
		cam_pos[1] = (bbox.vmin.y + bbox.vmax.y) * 0.5f;
		cam_pos[2] = (bbox.vmin.z + bbox.vmax.z) * 0.5f;

		dx = bbox.vmax.x - bbox.vmin.x;
		dy = bbox.vmax.y - bbox.vmin.y;
		dz = bbox.vmax.z - bbox.vmin.z;

		cam_dist = sqrt(dx * dx + dy * dy + dz * dz) * 0.75f;
		zfar = cam_dist * 50.0f;
		znear = zfar * 0.001;
	}
	/* force recalc projection */
	if(win_width > 0) reshape(win_width, win_height);
}

static void draw_aabox(const mf_aabox *aabb)
{
	glBegin(GL_LINES);
	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmax.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmax.z);

	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmax.z);
	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmin.x, aabb->vmax.y, aabb->vmax.z);

	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmax.z);
	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmin.y, aabb->vmax.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmin.z);
	glVertex3f(aabb->vmax.x, aabb->vmax.y, aabb->vmax.z);
	glEnd();
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
	int i;
	static int saved_win_xsz, saved_win_ysz;

	switch(key) {
	case 27:
		exit(0);

	case '\r':
		if(glutGetModifiers() & GLUT_ACTIVE_ALT) {
			if(saved_win_xsz) {
				glutReshapeWindow(saved_win_xsz, saved_win_ysz);
				saved_win_xsz = saved_win_ysz = 0;
			} else {
				saved_win_xsz = win_width;
				saved_win_ysz = win_height;
				glutFullScreen();
			}
		}
		break;

	case 't':
		use_tex ^= 1;
		glutPostRedisplay();
		break;

	case 'T':
		tex_modulate ^= 1;
		glutPostRedisplay();
		break;

	case 'w':
		wire ^= 1;
		glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
		glutPostRedisplay();
		break;

	case '\b':
		reset_view();
		glutPostRedisplay();
		break;

	case 0x13:
	case 's':
		if(glutGetModifiers() & GLUT_ACTIVE_CTRL) {
			mf_save(mf, "foo.obj", MF_FMT_OBJ);
		}
		break;

	case 'z':
		zup ^= 1;
		glutPostRedisplay();
		break;

	case 'n':
		use_nodes ^= 1;
		glutPostRedisplay();
		break;

	case 'b':
		show_bounds ^= 1;
		glutPostRedisplay();
		break;

	case 'p':
		mf_apply_xform(mf);
		for(i=0; i<mf_num_meshes(mf); i++) {
			struct mf_mesh *mesh = mf_get_mesh(mf, i);
			if(mesh->udata) {
				glDeleteLists((unsigned int)mesh->udata, 1);
				mesh->udata = 0;
			}
		}
		glutPostRedisplay();
		break;

	default:
		break;
	}
}

static void skeypress(int key, int x, int y)
{
	switch(key) {
	case GLUT_KEY_HOME:
		reset_view();
		glutPostRedisplay();
		break;
	}
}

static void zoom(float delta)
{
	float zoomspeed = cam_dist * 0.01f;
	if(zoomspeed > 5.0f) zoomspeed = 5.0f;
	cam_dist += delta * zoomspeed;

	if(cam_dist < 0.001) cam_dist = 0.001;
	glutPostRedisplay();
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

	if(press) {
		if(bidx == 3) {
			zoom(-10);
		} else if(bidx == 4) {
			zoom(10);
		}
	}
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
	if(bnstate[1]) {
		float up[3], right[3];
		float theta = cam_theta * M_PI / 180.0f;
		float phi = cam_phi * M_PI / 180.0f;
		float pan_scale = (cam_dist * 0.001) + 0.25;

		up[0] = -sin(theta) * sin(phi);
		up[1] = -cos(phi);
		up[2] = cos(theta) * sin(phi);
		right[0] = cos(theta);
		right[1] = 0;
		right[2] = sin(theta);

		/* pos += dx * right + dy * up */
		cam_pos[0] -= (dx * right[0] + dy * up[0]) * pan_scale;
		cam_pos[1] -= (dx * right[1] + dy * up[1]) * pan_scale;
		cam_pos[2] -= (dx * right[2] + dy * up[2]) * pan_scale;
		glutPostRedisplay();
	}
	if(bnstate[2]) {
		zoom(dy);
	}
}

static unsigned int load_texture(const char *fname)
{
	int width, height;
	unsigned int tex;
	void *pixels;

	if(!(pixels = img_load_pixels(fname, &width, &height, IMG_FMT_RGBA32))) {
		return 0;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	img_free_pixels(pixels);
	return tex;
}

static int text_width(const char *s)
{
	int res = 0;
	while(*s) {
		res += glutBitmapWidth(FONT, *s++);
	}
	return res;
}

static void glprintf(int x, int y, const char *fmt, ...)
{
	va_list ap;
	char buf[256];
	char *s;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	s = buf;

	if(x < 0) {
		x = -x - text_width(buf);
	}

	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, win_width, win_height, 0, -1, 1);

	glRasterPos2i(x, y);
	while(*s) {
		glutBitmapCharacter(FONT, *s++);
	}

	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glPopAttrib();
}

static int parse_args(int argc, char **argv)
{
	int i;
	static const char *usage_fmt = "Usage: %s [options] <mesh file>\n"
		"Options:\n"
		" -h,-help: print usage and exit\n";

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-') {
			if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) {
				printf(usage_fmt, argv[0]);
				exit(0);
			} else {
				fprintf(stderr, "invalid option: %s\n", argv[i]);
				return -1;
			}
		} else {
			if(fname) {
				fprintf(stderr, "unexpected argument: %s\n", argv[i]);
				return -1;
			}
			fname = argv[i];
			if((basename = strrchr(fname, '/'))) {
				basename++;
			} else {
				basename = fname;
			}
		}
	}
	return 0;
}
