/*
x is left-to-right, y is up, and z is going out
There is a point light at (0,1000,0)
Your eye is at (0,0,0), with 45 degree angles all around
There is a sphere at (0,0,D) of radius 1
There is a sphere at (0.5,0.5,1) of radius 0.1
There is another light point at (1,1,0)
*/

#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DIST 3
#define WIDTH 700
#define HEIGHT 700
#define GAMMA_CORRECTION (2.2)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
  
struct pixel{
	uint8_t b;
	uint8_t g;
	uint8_t r;
};

struct pixel_row{
	struct pixel col[WIDTH];
	uint8_t pad[((WIDTH*sizeof(struct pixel)+3) & ~3)-WIDTH*sizeof(struct pixel)];
};

struct bmp_core_header{
	uint32_t size;
	uint32_t width;
	uint32_t height;
	uint16_t num_planes;
	uint16_t bits_per_pixel;
	uint32_t compression;
	uint32_t compressed_size;
	uint32_t hres;
	uint32_t vres;
	uint32_t palette_size;
	uint32_t important_colors;
};

struct __attribute__ ((__packed__)) bitmap{
	uint8_t ident[2];
	uint32_t size;
	uint32_t reserved;
	uint32_t array_start;
	struct bmp_core_header core_header;
	struct pixel_row row[HEIGHT];
};

struct color{
	double red;
	double green;
	double blue;
};

struct sphere{
	double x;
	double y;
	double z;
	double r;
	struct color col;
};

struct light{
	double x;
	double y;
	double z;
	struct color power;
};

uint8_t correct(double raw_val){
	if(raw_val >= 1.0){
		return 255;
	}
	return (uint8_t)(255*pow(raw_val,GAMMA_CORRECTION));
}

struct sphere objects[] =
{
	{0, 0, 3, 1, {.9, .9, .9}},
	{.4, .8, 2.3, .1, {.5, .5, 1}}
};

struct light lights[] = 
{
	{0, 100, 0, {8000,8000,8000}},
	{1, -.2, 0, {5,5,5}}
};
	

int main(){
	struct bitmap *bmp = malloc(sizeof(struct bitmap));
	int i,j,iobj,ilight;
	for(i = 0; i < WIDTH; i++){
		for(j = 0; j < HEIGHT; j++){
			double mint;
			int bestobj = -1;
			for(iobj = 0; iobj < ARRAY_SIZE(objects); iobj++){
				// line of sight for this pixel is (t*(2i/WIDTH - 1),t*(2j/HEIGHT - 1),t)
				// equation for the sphere is (x-ox)^2+(y-oy)^2+(z-oz)^2=or
				// x^2+y^2+z^2-2ox*x-2oy*y-2oz*z+ox^2+oy^2+oz^2-or=0
				// t^2*(cx^2+cy^2+1)-2(ox*cx+oy*cy+oz)*t+(ox^2+oy^2+oz^2-or)=0
				double cx = 2.0*i/WIDTH - 1;
				double cy = 2.0*j/HEIGHT - 1;
				
				double ox = objects[iobj].x;
				double oy = objects[iobj].y;
				double oz = objects[iobj].z;
				double or = objects[iobj].r;
				
				double a = cx*cx+cy*cy+1;
				double b = -2*(cx*ox+cy*oy+oz);
				double c = ox*ox+oy*oy+oz*oz-or*or;
				
				double desc = b*b-4*a*c;
				if(desc < 0) continue;
				
				double t = (-b-sqrt(desc))/(2*a);
				if(t <= 0) continue;
				if(bestobj == -1 || t < mint){
					mint = t;
					bestobj = iobj;
				}
			}
				
			if(bestobj == -1){
				bmp->row[j].col[i].r = 0;
				bmp->row[j].col[i].g = 0;
				bmp->row[j].col[i].b = 0;
				continue;
			}
			
			// point on sphere
			double px = mint*(i*2.0/WIDTH - 1);
			double py = mint*(j*2.0/HEIGHT - 1);
			double pz = mint;
			
			// normal from sphere
			double sx = objects[bestobj].x-px;
			double sy = objects[bestobj].y-py;
			double sz = objects[bestobj].z-pz;
			double snorm = objects[bestobj].r;
			
			struct color raw_light;
			raw_light.red = 0;
			raw_light.green = 0;
			raw_light.blue = 0;
		
			for(ilight = 0; ilight < ARRAY_SIZE(lights);ilight++){
				double lx = lights[ilight].x;
				double ly = lights[ilight].y;
				double lz = lights[ilight].z;
				
				double vx = px-lx;
				double vy = py-ly;
				double vz = pz-lz;

				bool shadow = false;
				// we need to check for shadows
				for(iobj=0; iobj<ARRAY_SIZE(objects); iobj++){
					if(iobj == bestobj) continue;
					// use relative position to the light, makes things easier
					double ox = objects[iobj].x-lx;
					double oy = objects[iobj].y-ly;
					double oz = objects[iobj].z-lz;
					double or = objects[iobj].r;
					
					double a = vx*vx+vy*vy+vz*vz;
					double b = -2*(vx*ox+vy*oy+vz*oz);
					double c = ox*ox+oy*oy+oz*oz-or*or;
					
					double desc = b*b-4*a*c;
					if(desc < 0) continue;

					double at = (-b-sqrt(desc))/2;
					if(at >= 0 && at <= a){
						shadow=true;
						break;
					}
				}
				
				if(shadow) continue;
				
				double vnormsq = vx*vx+vy*vy+vz*vz;

				// vnormsq in twice: once for light dissipation, once for uniting
				double light_from = (sx*vx+sy*vy+sz*vz)/(vnormsq*snorm*sqrt(vnormsq));
				
				if(light_from>0){
					raw_light.red += light_from*lights[ilight].power.red;
					raw_light.green += light_from*lights[ilight].power.green;
					raw_light.blue += light_from*lights[ilight].power.blue;
				}
			}
			
			bmp->row[j].col[i].r = correct(raw_light.red*objects[bestobj].col.red);
			bmp->row[j].col[i].g = correct(raw_light.green*objects[bestobj].col.green);
			bmp->row[j].col[i].b = correct(raw_light.blue*objects[bestobj].col.blue);
		}
	}
	
	// make bmp header
	bmp->ident[0] = 'B';
	bmp->ident[1] = 'M';
	bmp->size = sizeof(struct bitmap);
	bmp->reserved=0;
	bmp->array_start=(int)(((void*)&(bmp->row)) - ((void*)bmp));
	bmp->core_header.size = sizeof(struct bmp_core_header);
	bmp->core_header.width = WIDTH;
	bmp->core_header.height = HEIGHT;
	bmp->core_header.num_planes = 1;
	bmp->core_header.bits_per_pixel = 24;
	bmp->core_header.compression = 0;
	bmp->core_header.compressed_size = sizeof(bmp->row);
	bmp->core_header.hres=2835;
	bmp->core_header.vres=2835;
	bmp->core_header.palette_size=0;
	bmp->core_header.important_colors=0;

	// now, actually output the image
	int fd = creat("out.bmp", S_IRUSR|S_IWUSR);
	if(fd == -1){
		printf("Error creating: 0x%x\n",errno);
		return errno;
	}
	
	int written = write(fd, bmp, sizeof(struct bitmap));
	if(written == -1){
		printf("Error writing: 0x%x\n",errno);
		return errno;
	}
	if(written != sizeof(struct bitmap)){
		printf("Only 0x%x of 0x%X written\n",written, sizeof(struct bitmap));
		return 1;
	}
	
	if(close(fd) == -1){
		printf("Error closing: 0x%x\n",errno);
		return errno;
	}
	return 0;
}