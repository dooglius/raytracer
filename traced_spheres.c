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

typedef double ftype;

#define WIDTH 700
#define HEIGHT 700
#define GAMMA_CORRECTION (2.2)
#define DOUBLE_MAX_INT (2147483648.0)
#define PI 3.14159265358979
#define EPS 0.00000000001
#define MAX_DEPTH 10

#define HORIZ_RATIO 0.5
#define VERT_RATIO 0.5

#define BACKGROUND_LOW_RED .2
#define BACKGROUND_LOW_GREEN .2
#define BACKGROUND_LOW_BLUE .5

#define BACKGROUND_HIGH_RED .5
#define BACKGROUND_HIGH_GREEN .5
#define BACKGROUND_HIGH_BLUE .5

// Note 1/256, or a little less than 0.004, is the min bit difference
#define NEGLIGIBLE_SIGNIFICANCE 0.001

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
	ftype red;
	ftype green;
	ftype blue;
};

struct sphere{
	ftype x;
	ftype y;
	ftype z;
	ftype r;
	ftype reflectivity;
	struct color col;
};

struct light{
	ftype x;
	ftype y;
	ftype z;
	struct color power;
};

// MUST be unitary vectors
struct light_at_infinity{
	ftype dx;
	ftype dy;
	ftype dz;
	struct color power;
};

struct sphere objects[] =
{
	{-1.2, 1.1, 4.5, .9, 1, {.95, .95, .95}},
	{-1.2, -1, 4.5, .9, 1, {.95, .95, .95}},
	{1.2, 1, 4.5, .6, 0, {.5, .9, .5}},
	{-1.2, 1.1, 2, .6, 0, {.5, .9, .5}},
	{0.9, -1, 4.5, 1, .6, {.9, .5, .5}},
};

struct light lights[] =
{
	{0,.3,4.5, {.5,.5,.5}},
	{0,-100,0, {7000,7000,7000}},
};

struct light light_at_infinity[] =
{
	{1, 0, 0, {.5,.5,.5}},
};

// modify this to get different sample sizes based on significance
int sample_size(ftype significance){
	return (int)(2000*significance);
	// for quick computation and no lighting based off other surfaces:
	//return 0;
}


struct color gradient(ftype x){
	// logistic equation for each color
	ftype mult = 1/(1+exp(-x*10));
	return (struct color){
		BACKGROUND_LOW_RED + (BACKGROUND_HIGH_RED-BACKGROUND_LOW_RED)*mult,
		BACKGROUND_LOW_GREEN + (BACKGROUND_HIGH_GREEN-BACKGROUND_LOW_GREEN)*mult,
		BACKGROUND_LOW_BLUE + (BACKGROUND_HIGH_BLUE-BACKGROUND_LOW_BLUE)*mult
	};
}

struct pixel to_pixel(struct color raw){
	ftype max = fmax(fmax(raw.red,raw.green),raw.blue);
	double comp = 1.0-256*EPS-EPS;
	if(max >= comp){
		printf("Warning: color too bright to display, rounding down. Output brightness will be incorrect to preserve color.\n");
		raw.red *= comp/max;
		raw.green *= comp/max;
		raw.blue *= comp/max;
	}
	return (struct pixel){(uint8_t)(256*pow(raw.blue,GAMMA_CORRECTION)),(uint8_t)(256*pow(raw.green,GAMMA_CORRECTION)),(uint8_t)(256*pow(raw.red,GAMMA_CORRECTION))};
}

struct color colorsat(int objnum, ftype fx, ftype fy, ftype fz, ftype px, ftype py, ftype pz, int depth, ftype significance){
	struct color ans=(struct color){0,0,0};
	
	if(depth > MAX_DEPTH) return ans;

	ftype min_sig = fmin(
						fmin(
							objects[objnum].col.red,
							objects[objnum].col.green
						),
						objects[objnum].col.blue
					);
	significance *= min_sig;
	if(significance <= NEGLIGIBLE_SIGNIFICANCE) return ans;
	
	int ilight, iobj;
	
	// normal from sphere
	ftype sx = px-objects[objnum].x;
	ftype sy = py-objects[objnum].y;
	ftype sz = pz-objects[objnum].z;
	ftype snorm = objects[objnum].r;
	
	ftype reflectivity = objects[objnum].reflectivity;
	struct color reflection = (struct color){0,0,0};
	
	
	bool fully_reflective = (reflectivity > 1-EPS);
	bool fully_nonreflective = (reflectivity < EPS);
	bool compute_reflection = !fully_nonreflective;// && sample_size(significance*reflectivity) > 0;

	// We sample randomly from the hemisphere. 
	//  This becomes a better idea when there's a lot more stuff around.
	//  It also includes the reflective case as that's basically the same code.
	int i;
	ftype weightsum=0;
	struct color from_diffusion = (struct color){0,0,0};
	int endat = sample_size(significance);
	if(compute_reflection) endat++;
	for(i=0; i<endat; i++){
		ftype vx,vy,vz,vnormsq,weight,dot;
		
		bool reflective_case = compute_reflection && (i == endat-1);
		// optimize fully reflective and fully non-reflective surfaces
		if(!reflective_case && fully_reflective) continue;
		if(reflective_case && fully_nonreflective) continue;
		
		if(reflective_case){
			// for the reflective case
			vx = fx-px;
			vy = fy-py;
			vz = fz-pz;
			vnormsq = vx*vx+vy*vy+vz*vz;
			dot = vx*sx+vy*sy+vz*sz;
			
			// dot/(vnorm*snorm) is the proportion of v in the s direction
			// so, we want newv=-unitv+2*units*(unit dot)
			// newv=-v/vnormsq * 2s*dot/(snorm^2*vnormsq) [dividing by vnorm]
			ftype mult = 2*dot/(snorm*snorm*vnormsq);
			vx = -vx/vnormsq+sx*mult;
			vy = -vy/vnormsq+sy*mult;
			vz = -vz/vnormsq+sz*mult;
			vnormsq = vx*vx+vy*vy+vz*vz;
		} else {
			// extent to which this is a Laplacian surface
			do{
				// random vector...
				vx=(ftype)(rand() << 31 | rand());
				vy=(ftype)(rand() << 31 | rand());
				vz=(ftype)(rand() << 31 | rand());
				
				vnormsq = vx*vx+vy*vy+vz*vz;
			} while(vnormsq < EPS);
			
			dot = vx*sx+vy*sy+vz*sz;
			
			if(dot < 0){
				vx = -vx;
				vy = -vy;
				vz = -vz;
				dot = -dot;
			}
			weight = dot/(snorm*sqrt(vnormsq));
			weightsum += weight;
		}

		ftype mint;
		int bestobj = -1;
		for(iobj = 0; iobj < ARRAY_SIZE(objects); iobj++){
			if(iobj == objnum) continue;
			// line of sight for this ray is (t*vx,t*vy,t*vz)
			// equation for the sphere is (x-ox)^2+(y-oy)^2+(z-oz)^2=or^2
			// x^2+y^2+z^2-2ox*x-2oy*y-2oz*z+ox^2+oy^2+oz^2-or^2=0
			// t^2*(vx^2+vy^2+vz^2)-2(ox*vx+oy*vy+oz*vz)*t+(ox^2+oy^2+oz^2-or^2)=0

			// make this relative to p, so equations are easier
			ftype ox = objects[iobj].x-px;
			ftype oy = objects[iobj].y-py;
			ftype oz = objects[iobj].z-pz;
			ftype or = objects[iobj].r;
			
			ftype a = vnormsq;
			ftype b = -2*(vx*ox+vy*oy+vz*oz);
			ftype c = ox*ox+oy*oy+oz*oz-or*or;
			
			ftype desc = b*b-4*a*c;
			if(desc < 0) continue;
			
			ftype t = (-b-sqrt(desc))/(2*a);
			if(t <= 0) continue;
			if(bestobj == -1 || t < mint){
				mint = t;
				bestobj = iobj;
			}
		}
		if(bestobj == -1) {
			ftype nonvert = sqrt(vx*vx+vz*vz);
			struct color bg = (nonvert<EPS)?(struct color){0,0,0}:gradient(vy/nonvert);
			if(reflective_case){
				reflection = bg;
			} else {
				from_diffusion.red += weight*bg.red;
				from_diffusion.green += weight*bg.green;
				from_diffusion.blue += weight*bg.blue;
			}
			continue;
		}

		if(reflective_case){
			reflection = colorsat(bestobj, px,py,pz,mint*vx+px, mint*vy+py, mint*vz+pz, depth+1, significance*reflectivity);
		} else {
			struct color col = colorsat(bestobj, px,py,pz,mint*vx+px, mint*vy+py, mint*vz+pz, depth+1, significance*(1-reflectivity)*weight/weightsum);
			from_diffusion.red += col.red*weight;
			from_diffusion.green += col.green*weight;
			from_diffusion.blue += col.blue*weight;
		}
	}
	if(weightsum > EPS){
		ans.red += from_diffusion.red/weightsum;
		ans.green += from_diffusion.green/weightsum;
		ans.blue += from_diffusion.blue/weightsum;
	}

	// light sources are points, so we handle them separately from the randoms
	for(ilight = 0; ilight < ARRAY_SIZE(lights); ilight++){
		ftype lx = lights[ilight].x;
		ftype ly = lights[ilight].y;
		ftype lz = lights[ilight].z;
		
		ftype vx = px-lx;
		ftype vy = py-ly;
		ftype vz = pz-lz;

		bool shadow = false;
		// we need to check for shadows
		for(iobj=0; iobj<ARRAY_SIZE(objects); iobj++){
			if(iobj == objnum) continue;
			// use relative position to the light, makes things easier
			ftype ox = objects[iobj].x-lx;
			ftype oy = objects[iobj].y-ly;
			ftype oz = objects[iobj].z-lz;
			ftype or = objects[iobj].r;
			
			ftype a = vx*vx+vy*vy+vz*vz;
			ftype b = -2*(vx*ox+vy*oy+vz*oz);
			ftype c = ox*ox+oy*oy+oz*oz-or*or;
			
			ftype desc = b*b-4*a*c;
			if(desc < 0) continue;

			ftype at = (-b-sqrt(desc))/2;
			if(at >= 0 && at <= a){
				shadow=true;
				break;
			}
		}
		
		if(shadow) continue;
		
		ftype vnormsq = vx*vx+vy*vy+vz*vz;

		// vnormsq in twice: once for inverse square dissipation, once for uniting
		ftype light_from = -(sx*vx+sy*vy+sz*vz)/(vnormsq*snorm*sqrt(vnormsq));
		
		if(light_from>0){
			ans.red += light_from*lights[ilight].power.red;
			ans.green += light_from*lights[ilight].power.green;
			ans.blue += light_from*lights[ilight].power.blue;
		}
	}
	
	if(!fully_nonreflective){
		// put the reflective part in
		ans.red = reflectivity*reflection.red + (1-reflectivity)*ans.red;
		ans.green = reflectivity*reflection.green + (1-reflectivity)*ans.green;
		ans.blue = reflectivity*reflection.blue + (1-reflectivity)*ans.blue;
	}
	
	// color tinting based on object
	ans.red = ans.red*objects[objnum].col.red;
	ans.green = ans.green*objects[objnum].col.green;
	ans.blue = ans.blue*objects[objnum].col.blue;
	return ans;
}

int main(){
	struct bitmap *bmp = malloc(sizeof(struct bitmap));
	int i,j,iobj,ilight;
	for(i = 0; i < WIDTH; i++){
		ftype cx = HORIZ_RATIO*2.0*(i-WIDTH/2)/WIDTH;
		for(j = 0; j < HEIGHT; j++){
			ftype cy = VERT_RATIO*2.0*(j-HEIGHT/2)/HEIGHT;

			ftype mint;
			int bestobj = -1;
			for(iobj = 0; iobj < ARRAY_SIZE(objects); iobj++){
				// line of sight for this pixel is (t*(2i/WIDTH - 1),t*(2j/HEIGHT - 1),t)
				// equation for the sphere is (x-ox)^2+(y-oy)^2+(z-oz)^2=or
				// x^2+y^2+z^2-2ox*x-2oy*y-2oz*z+ox^2+oy^2+oz^2-or=0
				// t^2*(cx^2+cy^2+1)-2(ox*cx+oy*cy+oz)*t+(ox^2+oy^2+oz^2-or)=0
				
				ftype ox = objects[iobj].x;
				ftype oy = objects[iobj].y;
				ftype oz = objects[iobj].z;
				ftype or = objects[iobj].r;
				
				ftype a = cx*cx+cy*cy+1;
				ftype b = -2*(cx*ox+cy*oy+oz);
				ftype c = ox*ox+oy*oy+oz*oz-or*or;
				
				ftype desc = b*b-4*a*c;
				if(desc < 0) continue;
				
				ftype t = (-b-sqrt(desc))/(2*a);
				if(t <= 0) continue;
				if(bestobj == -1 || t < mint){
					mint = t;
					bestobj = iobj;
				}
			}
				
			if(bestobj == -1){
				ftype nonvert = sqrt(cx*cx+1);
				bmp->row[j].col[i] = to_pixel(gradient(cy/nonvert));
				continue;
			}
			
			// point on sphere
			ftype px = mint*cx;
			ftype py = mint*cy;
			ftype pz = mint;
			
			bmp->row[j].col[i] = to_pixel(colorsat(bestobj,0,0,0,px,py,pz,0,1));
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