all: casted_spheres traced_spheres
debug: traced_sphere_debug casted_sphere_debug

casted_spheres: casted_spheres.c
        gcc -lm -ffast-math -O3 casted_spheres.c -o casted_spheres

traced_spheres: traced_spheres.c
        gcc -lm -ffast-math -O3 traced_spheres.c -o traced_spheres

casted_spheres_debug: casted_spheres.c
        gcc -lm -g casted_spheres.c -o casted_spheres

traced_spheres_debug: traced_spheres.c
        gcc -lm -g traced_spheres.c -o traced_spheres

clean:
        rm -f *_spheres *_spheres_debug
