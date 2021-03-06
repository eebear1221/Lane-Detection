
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#include "main.h"

#include "lanekar4.h"
#include "lanekar4.c"

IMAGESHM *img;

#define TRUE 1
#define FALSE 0

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
} RGBTriple;

SDL_Surface *screen,*off_screen,*text,*off_screen2;
SDL_Overlay *yuv_overlay;
SDL_Rect rect,src_rect,dest_rect,src_rect_orig;
void set_point(SDL_Surface *screen,int x,int y,int red,int green,int blue);
void Line (int x1, int y1, int x2, int y2, int red,int green,int blue,
           SDL_Surface *screen);
void Rect(int x1,int y1,int w,int h,
          int red,int green,int blue,
          SDL_Surface *screen);
void LineB (int x1, int y1, int x2, int y2, int red, int green, int blue,
	    SDL_Surface *screen);

// Basic pinhole camera model, using known camera parameters
//   tweaked slightly to obtain a better result
// For this model, lateral is zero (Xpix=0, X=0) in center of image

void pixeltoinch(double xpix, double ypix, double *x, double *y) {
   double d1;
   *y= (-220000.0+70.0*ypix)/(448.0-5.2*ypix);
   *x= *y * xpix /800.0;  
}

void inchtopixel(double x, double y, double *xpix, double *ypix) {
   *ypix= 240.0- 800.0*(y-254.0)/(70.0+364.0/70.0*y);
   *xpix= 800*x/y;
}


#include "testkar1.c"

__inline__ unsigned long long int rdtsc()
{
  unsigned long long int x;
  __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
  return x;
}

extern "C" {
 int mymain(int);
}

double getmidnight() {
  double time1;
  struct tm *tmidnight;
  time_t ttmidnight;

  ttmidnight= time(NULL);
  tmidnight= localtime(&ttmidnight);
  tmidnight->tm_sec=0; tmidnight->tm_min=0; tmidnight->tm_hour=0;
  return(mktime(tmidnight));
}

int mymain(int duration) {
  int i,j,k, shmFD;
  double time_start, time_end;
  struct timespec timep;
  int screen_width=640, screen_height=480;
  unsigned char *bufp2, *camera;
  Uint32 bw_colormap[256];
  struct LaneIO X;

  framecount= 0;
  sdl_thread_die= 0;
  Gacqframe= -1;
  Gtimemidnight= getmidnight();

  shmFD= shm_open(IMAGESHM_NAME, O_RDWR|O_CREAT, 000777);
  if (shmFD == -1) {
     fprintf(stderr, "SHM Open failed: %s\n", strerror(errno));
     exit(1);
  }
  if (ftruncate(shmFD, sizeof(IMAGESHM)) == -1) {
     fprintf(stderr, "ftruncate: %s\n", strerror(errno));
     exit(1);
  }
  img= (IMAGESHM *) mmap(0, sizeof(IMAGESHM), PROT_READ|PROT_WRITE, 
                         MAP_SHARED, shmFD,0);
  if (img == (void *) -1) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      exit(1);
  }

  char sdlenv[] = "SDL_VIDEO_WINDOW_POS=100,600";
  SDL_putenv(sdlenv);
  if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
    exit(1);
  }

//  screen = SDL_SetVideoMode(screen_width, screen_height, 0, SDL_HWSURFACE|SDL_DOUBLEBUF);
  screen = SDL_SetVideoMode(screen_width, screen_height, 0, SDL_SWSURFACE);
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }
  off_screen=SDL_DisplayFormat(screen);

  camera= (unsigned char *) calloc(640*480, 1);
  for(i=0;i<256;i++) 
    bw_colormap[i]=SDL_MapRGB(screen->format,i,i,i);
  SDL_ShowCursor(SDL_ENABLE);

  atexit(SDL_Quit);

#ifndef BENCHTEST
  system("/home/elshaerm/qtSDL/gigecamera &"); 
#endif

  initLaneIO(&X);
 
  do {
    clock_gettime(CLOCK_REALTIME, &timep);
    time_start= (timep.tv_sec-Gtimemidnight) +
                (double) timep.tv_nsec/(double)1000000000;

#ifndef BENCHTEST
    while (!img->newframe) usleep(1000);
    img->newframe= 0;
#endif
    while (!img->newframe) usleep(1000);
    img->newframe= 0;
    framecount++;
    memcpy(camera, img->image, 640*480);
   
    if (Gacqframe>-1) {
       char filename[120];  FILE *fpkar;
       Gacqframe++;
       sprintf(filename, "%s%5.5d.pgm", "A", Gacqframe);
       fpkar= fopen(filename, "w");
       fprintf(fpkar,"P5\n# KAR Test %d\n640 480\n255\n", Gacqframe);
       //fwrite((const char *)camera.capture_buffer, 1, camera.frame_height*camera.frame_width, fpkar);
       for (j=0; j<480; j++) {
          for (i=0; i<640; i++) {
             fprintf(fpkar,"%c", camera[i+j*640]);
           }
       }
       fclose(fpkar);
       if (Gacqframe>50000) Gacqframe= -1;
    }

#if 0
    if ( SDL_MUSTLOCK(screen) ) {
      if ( SDL_LockSurface(screen) < 0 ) {
        return(0);
      }
    }
#endif

#if 0   // this is the standard algorithm, including marking display
    extractlane(camera, &X);
    drawmarkers (camera);   // the original alg draws marks on the image buffer
    outputresults(camera, X, framecount);
#endif

// copy camera image buffer to screen (display) buffer
    bufp2=(unsigned char *) screen->pixels;
    for(j=0;j<480;j++) {
      for(i=0;i<640;i++) {      
         memcpy(bufp2,&(bw_colormap[camera[j*640+i]]),
                    screen->format->BytesPerPixel);
         bufp2+=screen->format->BytesPerPixel;
      }   
       bufp2+=(screen_width-640)*screen->format->BytesPerPixel;
    }

#if 0
    testkarBrightRansac(camera, screen);
#endif

#if 1
    testkarBrightRansac_ME(camera, screen);
#endif

#if 0
    SDL_FillRect(screen, NULL, 0);
    SDL_Surface *image = SDL_LoadBMP("sample.bmp");
    if ( image ) {
         SDL_Rect dst;
         dst.x = (screen->w - image->w)/2;
         dst.y = (screen->h - image->h)/2;
         dst.w = image->w;
         dst.h = image->h;
         SDL_BlitSurface(image, NULL, screen, &dst);
    }
    Line(0,FIRSTSCAN,639,FIRSTSCAN,0,0,255,screen);
    Line(0,FIRSTSCAN-NUMSCANS*SCANINT,639,FIRSTSCAN-NUMSCANS*SCANINT,0,0,255,screen);
    Line(320,0,320,479,0,0,255,screen);
#endif

#if 0
    set_point(screen,100,10,255,0,0);
    Line(10,10,250,100,255,0,0,screen);
    Line(10,10,250,200,0,255,0,screen);
    Line(10,10,250,350,0,0,255,screen);
    Rect(10,10,20,20,0,255,0,screen);
    Rect(200,500,100,12,0,0,255,screen);
#endif
 
#if 0
    if ( SDL_MUSTLOCK(screen) ) {
      SDL_UnlockSurface(screen);
    }
#endif
    SDL_Flip(screen);
//    SDL_UpdateRect(screen, 0, 0,screen_width, screen_height);

if (Goffsetpixels<-80 && vehshm->turnsignal!=0x20) { //right
//   system("play -q beep-05.wav &"); 
   printf("right\n");
  }
if (Goffsetpixels>80 && vehshm->turnsignal!=0x10) { //left
//    system("play -q beep-05.wav &");
    printf("left\n");
  }

    clock_gettime(CLOCK_REALTIME, &timep);
    time_end= (timep.tv_sec-Gtimemidnight) +
                (double) timep.tv_nsec/(double)1000000000;
    framerate= time_end-time_start;
  } while (!sdl_thread_die);

  img->camthreadstop= 1;
  free(camera);
  SDL_Quit();

}

void set_point(SDL_Surface *screen,int x,int y,int red,int green,int blue)
{
  Uint32 color;
  unsigned char *bufp2;
  color=SDL_MapRGB(screen->format,red,green,blue);
  bufp2=(unsigned char *) 
       screen->pixels+screen->format->BytesPerPixel*(x+y*screen->w);  
  memcpy(bufp2,&color,screen->format->BytesPerPixel);
}


void Rect(int x1,int y1,int w,int h,
          int red,int green,int blue,
          SDL_Surface *screen)
{
  int i,j;
  Uint32 color;
  unsigned char *bufp2;
 
  color=SDL_MapRGB(screen->format,red,green,blue);
  for(j=y1;j<(y1+h);j++)
    for(i=x1;i<(x1+w);i++) {
      if ((i>=0)&&(j>=0)&&(i<screen->w)&&(j<screen->h)) {
        bufp2=(unsigned char *) 
            screen->pixels+screen->format->BytesPerPixel*(i+j*screen->w);
        memcpy(bufp2,&color,screen->format->BytesPerPixel);
      }      
    }
}


void Line (int x1, int y1, int x2, int y2, int red, int green, int blue,
	    SDL_Surface *screen) {
  double slope, x, y, temp;

  if (x2<x1) {
    temp=x1;
    x1=x2;
    x2=temp;
    temp=y1;
    y1=y2;
    y2=temp;
  }
  slope= (double) (y1-y2)/(x1-x2);
  if (fabs(slope)<3){
    y= y1;
    for (x=x1; x<=x2; x++) {
      if (x>0.0&&x<640.0&&y>0.0&&y<480.0) {
        set_point(screen, x, y, red, green, blue);
        set_point(screen, x-1, y, red, green, blue);
        set_point(screen, x+1, y, red, green, blue);
      }
      y+= slope;
    }
  } else {
    if (y2<y1) {
      temp=x1;
      x1=x2;
      x2=temp;
      temp=y1;
      y1=y2;
      y2=temp;
    }
    slope= (double) (x1-x2)/(y1-y2);
    x=x1;
    for (y=y1; y<=y2; y++) {
      if (x>0.0&&x<640.0&&y>0.0&&y<480.0) {
        set_point(screen, x, y, red, green, blue);
        set_point(screen, x-1, y, red, green, blue);
        set_point(screen, x+1, y, red, green, blue);
      }
      x+= slope;
    }
  }
}

void LineB (int x1, int y1, int x2, int y2, 
           int red,int green,int blue,
           SDL_Surface *screen)
{
  int i,j,x,y,d;
  int deltax,deltay;
  int steep,temp,negate_y_axis;
  int width,height;

  width=screen->w;
  height=screen->h;
 
  negate_y_axis=FALSE;
  steep=FALSE;
  deltay=y2-y1;
  deltax=x2-x1;

  if (deltay>deltax) {
    temp=x1;
    x1=y1;
    y1=temp;

    temp=x2;
    x2=y2;
    y2=temp;

    deltay=y2-y1;
    deltax=x2-x1;
    steep=TRUE;
  }


  if (deltax<0) {
    temp=x1;
    x1=x2;
    x2=temp;
    temp=y1;
    y1=y2;
    y2=temp;
    deltay=y2-y1;
    deltax=x2-x1;

  }

  if (deltay<0) {
    deltay=abs(deltay);
    negate_y_axis=TRUE;
  }

  y=y1;
  d=(deltay<<1)-deltax;
  
  for(x=x1;x<x2;x++) {
    if (steep) set_point(screen,y,x,red,green,blue); //screen[y+x*width]=color;
    else set_point(screen,x,y,red,green,blue);       //screen[x+y*width]=color;
    // screen[x+y*width]=color;
    if (d<0) d=d+(deltay << 1);
    else {
      d=d+((deltay-deltax)<<1);
      if (!negate_y_axis) y=y+1;
      else y=y-1;
    }    
  }
}

