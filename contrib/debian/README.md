
Debian
====================
This directory contains files used to package yottafluxd/yottaflux-qt
for Debian-based Linux systems. If you compile yottafluxd/yottaflux-qt yourself, there are some useful files here.

## yottaflux: URI support ##


yottaflux-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install yottaflux-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your yottaflux-qt binary to `/usr/bin`
and the `../../share/pixmaps/yottaflux128.png` to `/usr/share/pixmaps`

yottaflux-qt.protocol (KDE)

