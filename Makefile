run:
	./neon

make:
	gcc `pkg-config --cflags gtk4 gstreamer-1.0` neon.c -o neon `pkg-config --libs gtk4 gstreamer-1.0`


