
Debian
====================
This directory contains files used to package chickadeed/chickadee-qt
for Debian-based Linux systems. If you compile chickadeed/chickadee-qt yourself, there are some useful files here.

## chickadee: URI support ##


chickadee-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install chickadee-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your chickadee-qt binary to `/usr/bin`
and the `../../share/pixmaps/chickadee128.png` to `/usr/share/pixmaps`

chickadee-qt.protocol (KDE)

