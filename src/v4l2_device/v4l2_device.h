#ifndef __V4L2_DEVICE_H
#define __V4L2_DEVICE_H

#include <pthread.h>
#include <inttypes.h>


#define BUF_NUM		4
#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct v4l2_mapbuf {
	void *start;
	unsigned int length;
};

struct v4l2_mapbuf *v4l2_mapbufs;

int v4l2_open(const char *device);
int v4l2_close(int fd);
int v4l2_query_cap(int fd, const char *device);
int v4l2_set_format(int fd, uint32_t pfmt);
int v4l2_get_format(int fd);
int v4l2_mmap(int fd);
int v4l2_munmap();
int v4l2_set_fps(int fd, int fps);
int v4l2_stream_on(int fd);
int v4l2_stream_off(int fd);

#endif
