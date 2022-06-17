#include "v4l2_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <../config.h>

int v4l2_open(const char *device)
{
	struct stat st;
	CLEAR(st);
	if (stat(device, &st) == -1) {
		perror("stat");
		return -1;
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is not character device.\n", device);
		return -1;
	} else
		printf("%s is a character device.\n", device);
	return open(device, O_RDWR | O_NONBLOCK, 0);
}

int v4l2_close(int fd) 
{
	return close(fd);
}

int v4l2_query_cap(int fd, const char *device) 
{
	struct v4l2_capability cap;
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
		printf("Error opening device %s:unable to query device.", device);
		return -1;
	} else {
		printf("driver:\t%s\n", cap.driver);
		printf("card:\t%s\n",cap.card);
		printf("bus_info:\t%s\n", cap.bus_info);
		printf("version:\t%d\n", cap.version);
		printf("capabilities:\t%x\n", cap.capabilities);
	}

	if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == V4L2_CAP_VIDEO_CAPTURE) {
		printf("Device %s: support capture.\n", device);
	}

	if ((cap.capabilities & V4L2_CAP_STREAMING) == V4L2_CAP_STREAMING) {
		printf("Device %s: support streaming.\n", device);
	}
	
	/*get camera support format*/
	struct v4l2_fmtdesc fmtdesc;
	fmtdesc.index = 0;
	fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	printf("\033[33mSupport format:\n\033[0m");
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
		printf("\033[33m\t%d.%s\n\033[0m", fmtdesc.index + 1, fmtdesc.description);
		
		struct v4l2_frmsizeenum frmsize;
		frmsize.pixel_format = fmtdesc.pixelformat;
		for (int i = 0; ; ++i) {
			frmsize.index = i;
			if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == -1) {
				break;
			}
			printf("\033[31m\twidth:%d \theight:%d\n\033[0m",
					frmsize.discrete.width, frmsize.discrete.height);
		}
		fmtdesc.index++;
	}
	return 0;
}

int v4l2_set_format(int fd, uint32_t pfmt)
{
	struct v4l2_format format;
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat = pfmt;
	format.fmt.pix.width = FORMAT_WIDTH;
	format.fmt.pix.height = FORMAT_HEIGHT;
	format.fmt.pix.field = V4L2_FIELD_INTERLACED;
	
	if (ioctl(fd, VIDIOC_S_FMT, &format) == -1) {
		fprintf(stderr, "Unable to set format.\n");
		return -1;
	}

	return 0;
}

int v4l2_get_format(int fd)
{
	struct v4l2_format format;
	if (ioctl(fd, VIDIOC_G_FMT, &format) == 1) {
		fprintf(stderr, "Unable to get format.\n");
		return -1;
	}
	printf("\033[33mpixelformat:\t%c%c%c%c\n\033[0m",
			format.fmt.pix.pixelformat & 0xFF,
			(format.fmt.pix.pixelformat >> 8) & 0xFF,
			(format.fmt.pix.pixelformat >> 16) & 0xFF,
			(format.fmt.pix.pixelformat >> 24) & 0xFF);
	printf("width:\t%d\n", format.fmt.pix.width);
	printf("height:\t%d\n", format.fmt.pix.height);
	printf("field:\t%d\n", format.fmt.pix.field);

	return 0;
}

int v4l2_set_fps(int fd, int fps)
{
	struct v4l2_streamparm fpsparm;
	CLEAR(fpsparm);
	fpsparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fpsparm.parm.capture.timeperframe.numerator = 1;
	fpsparm.parm.capture.timeperframe.denominator = fps;
	if (ioctl(fd, VIDIOC_S_PARM, &fpsparm) == -1) {
		fprintf(stderr, "Unable to set framerate.\n");
				return -1;
	}
	return 0;
}

int v4l2_mmap(int fd)
{
	struct v4l2_requestbuffers reqbuffer;
	reqbuffer.count = BUF_NUM;
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.memory = V4L2_MEMORY_MMAP;
	if(ioctl(fd, VIDIOC_REQBUFS, &reqbuffer) == -1) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}
	
	/*mmap for buffers*/
	v4l2_mapbufs = malloc(reqbuffer.count * sizeof(struct v4l2_mapbuf)); 
	if (v4l2_mapbufs == NULL) {
		fprintf(stderr, "Out of memory");
		return -1;
	}

	struct v4l2_buffer buf;
	unsigned int n_bufs;
	for (n_bufs = 0; n_bufs < reqbuffer.count; ++n_bufs) {
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_bufs;
		
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
			fprintf(stderr, "query kernel space queue failed.\n");
			return -1;
		}

		v4l2_mapbufs[n_bufs].start = mmap(NULL, buf.length, 
				PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		v4l2_mapbufs[n_bufs].length = buf.length;

#if DEBUG
		printf("buffer offset:%d\tlength:%d\n", buf.m.offset, buf.length);
#endif

		if (v4l2_mapbufs[n_bufs].start == MAP_FAILED) {
			fprintf(stderr, "buffer map error %u\n", n_bufs);
			return -1;
		}
	}
	return 0;
}

int v4l2_munmap()
{
	int i;
	for (i = 0; i < BUF_NUM; ++i) {
		if (munmap(v4l2_mapbufs[i].start, v4l2_mapbufs[i].length) == -1) {
			fprintf(stderr, "munmap failure %d\n", i);
			return -1;
		}
	}
	return 0;
}

int v4l2_stream_on(int fd)
{
	struct v4l2_buffer buf;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	unsigned int n_bufs;
	for (n_bufs = 0; n_bufs < BUF_NUM; ++n_bufs) {
		buf.index = n_bufs;
		if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			fprintf(stderr, "queue buffer failed.\n");
			return -1;
		}
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		fprintf(stderr, "stream on failed\n");
		return -1;
	}
	return 0;
}

int v4l2_stream_off(int fd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
		fprintf(stderr, "stream off failed\n");
		return -1;
	}
	printf("stream is off\n");
	return 0;
}
