#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_video.h>
#include <bits/types/struct_timeval.h>
#include <pthread.h>
#include <sys/select.h>
#include <v4l2_device.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "config.h"

#if MJPG
#include <jpeglib.h>
#endif

typedef void (*framehandler)(void *pframe, int length);

pthread_t thread_stream;
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
SDL_Rect rect;

int thread_exit_sig = 0;

struct stream_handler {
	int fd;
	framehandler framehandler;
};

#if MJPG 
unsigned char rgbdata[FORMAT_WIDTH * FORMAT_HEIGHT * 3];

int read_JPEG_file (const unsigned char *jpegData, unsigned char *rgbdata, unsigned long jpegsize)
{
	struct jpeg_error_mgr jerr;
	struct jpeg_decompress_struct cinfo;
	cinfo.err = jpeg_std_error(&jerr);
	
	jpeg_create_decompress(&cinfo);

	jpeg_mem_src(&cinfo, jpegData, jpegsize);

	(void) jpeg_read_header(&cinfo, TRUE);

	(void) jpeg_start_decompress(&cinfo);
	
	int row_stride = cinfo.output_width * cinfo.output_components;
	unsigned char * buffer = malloc(row_stride);
	int i = 0;
	while(cinfo.output_scanline < cinfo.output_height) {
		(void) jpeg_read_scanlines(&cinfo, &buffer, 1);
		memcpy(rgbdata + i*FORMAT_WIDTH*3, buffer, row_stride);
		i++;
	}
	(void) jpeg_finish_decompress(&cinfo);
	
	jpeg_destroy_decompress(&cinfo);
	return 1;
}
#endif

static void frame_handler(void *pframe, int length)
{
#if MJPG
	read_JPEG_file(pframe, rgbdata, length);
	SDL_UpdateTexture(texture, &rect, rgbdata, FORMAT_WIDTH *3);
#elif YUYV
	SDL_UpdateTexture(texture, &rect, pframe, FORMAT_WIDTH * 2);
#endif
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, &rect);
	SDL_RenderPresent(renderer);
}

static void *v4l2_streaming(void *argv) 
{
	CLEAR(rect);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return NULL;
	}
	
	window = SDL_CreateWindow("SDL2_V4l2_Camera", SDL_WINDOWPOS_UNDEFINED, 
			SDL_WINDOWPOS_UNDEFINED,FORMAT_WIDTH, 
			FORMAT_HEIGHT, SDL_WINDOW_SHOWN);
	if (!window) {
		fprintf(stderr, "SDL: could not crate window - exiting:%s\n",
				SDL_GetError());
		return NULL;
	}
	
	renderer = SDL_CreateRenderer(window, -1, 
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		fprintf(stderr, "SDL: Create renderer failed.\n");
		return NULL;
	}	

#if MJPG
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, 
			SDL_TEXTUREACCESS_STREAMING, FORMAT_WIDTH, FORMAT_HEIGHT);
#elif YUYV
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2, 
			SDL_TEXTUREACCESS_STREAMING, FORMAT_WIDTH, FORMAT_HEIGHT);
#endif
	rect.w = FORMAT_WIDTH;
	rect.h = FORMAT_HEIGHT;

	int fd = ((struct stream_handler*)argv)->fd;
	framehandler handler = ((struct stream_handler*)argv)->framehandler;

	fd_set fds;
	struct v4l2_buffer buf;
	while (!thread_exit_sig) {
		int ret;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
		ret = select(fd + 1, &fds, NULL, NULL, &tv);
		if ( ret == -1) {
			fprintf(stderr, "select error\n");
			return NULL;
		} else if (ret == 0) {
			fprintf(stderr, "timeout waiting for frame\n");
			continue;
		}

		if (FD_ISSET(fd, &fds)) {
			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
				fprintf(stderr, "VIDIOC_DQBUF failed\n");
				return NULL;
			}
#if DEBUG
			printf("deque buffer %d\n", buf.index);
#endif
			if (handler)
				(*handler)(v4l2_mapbufs[buf.index].start,
						v4l2_mapbufs[buf.index].length);

			//printf("%d\n", v4l2_mapbufs[buf.index].length);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
				fprintf(stderr, "VIDIOC_QBUF failed\n");
				return NULL;
			}
#if DEBUG
			printf("queue buffer %d\n", buf.index);
#endif
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{	
	char *device = "/dev/video0";

	int v_fd = v4l2_open(device);
	if (v_fd == -1) {
		fprintf(stderr, "can't open %s\n", device);
		exit(-1);
	}

	if (v4l2_query_cap(v_fd, device) == -1) {
		perror("v4l2_query_cap");
		goto exit_;
	}

#if MJPG
	if (v4l2_set_format(v_fd, V4L2_PIX_FMT_MJPEG) == -1) {
#elif YUYV
	if (v4l2_set_format(v_fd, V4L2_PIX_FMT_YUYV) == -1) {
#endif
		perror("v4l2_set_format");
		goto exit_;
	}

	if (v4l2_get_format(v_fd) == -1) {
		perror("v4l2_get_format");
		goto exit_;
	}
	
	if (v4l2_set_fps(v_fd, 30) == -1) {
		perror("v4l2_set_fps");
		goto exit_;
	}

	if (v4l2_mmap(v_fd) == -1) {
		perror("v4l2_mmap");
		goto exit_;
	}

	if (v4l2_stream_on(v_fd) == -1) {
		perror("v4l2_stream_on");
		goto exit_;
	}

	/*start frame thread*/
	struct stream_handler sh = {v_fd,  frame_handler};
	if (pthread_create(&thread_stream, NULL, v4l2_streaming, (void *)(&sh))) {
		fprintf(stderr, "create thread failed\n");
		goto exit_;
	}

	int quit = 0;
	SDL_Event event;
	while (!quit) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				quit = 1;
				break;
			case SDL_KEYDOWN:
				if(event.key.keysym.sym == SDLK_ESCAPE)
					quit = 1;			
				break;
			default:
				break;
			}
		}
		SDL_Delay(25);
	}
	
	thread_exit_sig = 1;
	pthread_join(thread_stream, NULL);

	if (v4l2_stream_off(v_fd) == -1) {
		perror("v4l2_stream_off");
		goto exit_;
	}

	if (v4l2_munmap() == -1) {
		perror("v4l2_munmap");
		goto exit_;
	}

exit_:
	if(v4l2_close(v_fd) == -1) {
		perror("v4l2_close");
	}
	
	SDL_Quit();
}
