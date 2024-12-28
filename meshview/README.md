Mesh viewer example for the meshfile library
============================================

Pass a mesh file as a command-line argument to meshview to open it.

Controls
--------
### General
 - Quit: ESC
 - Hotkey help: F1

### View
 - Rotate: left mouse drag.
 - Zoom: right mouse drag or mousewheel.
 - Pan: middle mouse drag.
 - Recenter: backspace or home.

### Rendering
 - Toggle textures: T
 - Toggle wireframe: W

License
-------
Author: John Tsiombikas <nuclear@mutantstargoat.com>

The mesh viewer example code is placed into the public domain.

Build
-----
Make sure you have all dependencies installed:
 - OpenGL
 - GLUT (e.g. freeglut: https://github.com/freeglut/freeglut)
 - libimago: https://github.com/jtsiomb/libimago
   - libpng: http://www.libpng.org/pub/png/libpng.html
     - zlib: http://zlib.net
   - jpeglib: http://ijg.org

Type `make` to build, `make install` as root to install system-wide (default
prefix: `/usr/local` can be changed in the first line of the `Makefile`).
