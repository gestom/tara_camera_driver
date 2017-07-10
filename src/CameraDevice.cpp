#include "CameraDevice.h"

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define forn(i,n) for(size_t i=0;i<(n);i++)

using namespace tara;

static int xioctl(int fd, int request, void* arg)
{
  int r;

  do r = ioctl (fd, request, arg);
  while (-1 == r && EINTR == errno);

  return r;
}

StereoCameraDriver::StereoCameraDriver(const std::string& device)
{
  fd_ = open("/dev/video1", O_RDWR);
  if (fd_ < 0)
    throw std::runtime_error("Opening video device");

  printCapabilities();

  init_mmap();
}

StereoCameraDriver::~StereoCameraDriver()
{
  close( fd_ );
}

void StereoCameraDriver::grabNextFrame(cv::Mat& img_left, cv::Mat& img_right)
{
  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;

  if(xioctl(fd_, VIDIOC_QBUF, &buf) == -1)
    throw std::runtime_error("Query Buffer");

  if(xioctl(fd_, VIDIOC_STREAMON, &buf.type) == -1)
    throw std::runtime_error("Start Capture");

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd_, &fds);
  struct timeval tv = {0};
  tv.tv_sec = 2;

  if(select(fd_+1, &fds, NULL, NULL, &tv) == -1)
    throw std::runtime_error("Waiting for Frame");

  if(xioctl(fd_, VIDIOC_DQBUF, &buf) == -1)
    throw std::runtime_error("Retrieving Frame");

  //printf("bytes used: %d\n", buf.bytesused);

  // de-interleave image
  uint8_t* pixel = buffer_;
  forn(i, FRAME_HEIGHT)
    forn(j, FRAME_WIDTH)
  {
    // each pixel is defined in 24 bits (3 bytes)
    // the following was found out by trial and error:
    // - the first 4b are always zero
    // - the second byte are the MSb of the left image.
    // - the third byte are the MSb of the right image.
    // - the last 4b of the first byte may be the LSb of both,
    //   they are not used here.
    img_left.at<uint8_t>(i, j) = pixel[1];
    img_right.at<uint8_t>(i, j) = pixel[2];
    pixel += 3;
  }
}

void StereoCameraDriver::printCapabilities()
{
  struct v4l2_capability caps = {};
  if (xioctl(fd_, VIDIOC_QUERYCAP, &caps) == -1)
    throw std::runtime_error("Querying Capabilities");

  printf( "Driver Caps:\n"
    "  Driver: \"%s\"\n"
    "  Card: \"%s\"\n"
    "  Bus: \"%s\"\n"
    "  Version: %d.%d\n"
    "  Capabilities: %08x\n",
    caps.driver,
    caps.card,
    caps.bus_info,
    (caps.version>>16)&&0xff,
    (caps.version>>24)&&0xff,
    caps.capabilities);

  struct v4l2_cropcap cropcap = {0};
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl (fd_, VIDIOC_CROPCAP, &cropcap) == -1)
    throw std::runtime_error("Querying Cropping Capabilities");

  printf( "Camera Cropping:\n"
    "  Bounds: %dx%d+%d+%d\n"
    "  Default: %dx%d+%d+%d\n"
    "  Aspect: %d/%d\n",
    cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top,
    cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top,
    cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);

  int support_grbg10 = 0;

  struct v4l2_fmtdesc fmtdesc = {0};
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  char fourcc[5] = {0};
  char c, e;
  printf("  FMT : CE Desc\n--------------------\n");
  while (xioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
  {
    strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);

    if (fmtdesc.pixelformat == V4L2_PIX_FMT_BGR24)
      support_grbg10 = 1;

    c = fmtdesc.flags & 1? 'C' : ' ';
    e = fmtdesc.flags & 2? 'E' : ' ';

    printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);

    fmtdesc.index++;
  }

  if (!support_grbg10)
    printf("Doesn't support GRBG10.\n");

  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = FRAME_WIDTH;
  fmt.fmt.pix.height = FRAME_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (xioctl(fd_, VIDIOC_S_FMT, &fmt) == -1)
    throw std::runtime_error("Setting Pixel Format");

  strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
  printf( "Selected Camera Mode:\n"
    "  Width: %d\n"
    "  Height: %d\n"
    "  PixFmt: %s\n"
    "  Field: %d\n",
    fmt.fmt.pix.width,
    fmt.fmt.pix.height,
    fourcc,
    fmt.fmt.pix.field);
}

void StereoCameraDriver::init_mmap()
{
  struct v4l2_requestbuffers req = {0};
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (xioctl(fd_, VIDIOC_REQBUFS, &req) == -1)
    throw std::runtime_error("Requesting Buffer");

  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;

  if(xioctl(fd_, VIDIOC_QUERYBUF, &buf) == -1)
    throw std::runtime_error("Querying Buffer");

  buffer_ = static_cast<uint8_t*>(mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset));

  printf("Length: %d\nAddress: %p\n", buf.length, buffer_);
  printf("Image Length: %d\n", buf.bytesused);
}
