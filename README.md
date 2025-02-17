meshfile library
================

About
-----
Meshfile is a simple C library for reading and writing 3D mesh file formats. It
reads/writes:

 - Indexed triangle meshes, multiple objects per file, with a number of optional
   pre-defined vertex attributes. No quads, n-gons, strips, or fans.
 - Materials with a number of common pre-defined material attributes, each
   optionally combined with a texture map (filename/path).
 - Optional transformation node hierarchy.

Animations are explicitly outside of the scope of this library.

File formats
------------
Here's a list of currently supported file formats, and any caveats for each one.

 - Wavefront OBJ
 - JTF (Just Triangle Faces): http://runtimeterror.com/tech/jtf
 - GLTF (OpenGL Transmission Format): currently read-only
 - 3DS (3D Studio)
 - STL (Stereolithography): binary only

Using meshfile
--------------
There are two ways to use meshfile:
 1. build and install it system-wide (`make && make install`). Then just include
    `meshfile.h` in your program and link with `-lmeshfile`.
 2. drop the contents of `src` and `include` directories into a subdir of your
    program and build it as part of your own build. It has no dependencies, so
    it should be as easy as just adding the list of `.c` files to your build.

Meshfile being released under the terms of the GNU LGPL, the second method is
only possible if your program is free software.

Please note that before v1.0 is released, there is no guarantee for API/ABI
stability.

License
-------
Copyright (C) 2025 John Tsiombikas <nuclear@mutantstargoat.com>

This program is free software. Feel free to run, modify and/or redistribute
under the terms of the GNU Lesser General Public License v3, or at your option
any later version published by the Free Software Foundation. 
See COPYING and COPYING.LESSER for details.

Build
-----
To build meshfile on UNIX, simply run `make`. The meshfile library has zero
dependencies other than libc. Run `make install` as root to install it under the
`/usr/local` prefix. You can change the prefix by editing the first line of the
`Makefile`.

There's also an example mesh viewer program as part of the meshfile
distribution. To build and install that change into `meshview` and run `make`
and `make install` again. For the meshview dependencies and build instructions,
refer to the `meshview/README.md` file.
