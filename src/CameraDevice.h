#include <string>
#include <opencv2/core/core.hpp>

#define FRAME_WIDTH 752
#define FRAME_HEIGHT 480

namespace tara
{

class StereoCameraDriver
{
  public:

    StereoCameraDriver(int deviceID);

    ~StereoCameraDriver();

    void grabNextFrame(cv::Mat& img_left, cv::Mat& img_right);

    bool setExposure(int ExposureVal);

    bool setBrightness(int BrightnessVal);

    void printCapabilities();

  private:

    // file descriptor for camera device
    int fd_;

    // image data buffer
    uint8_t* buffer_;

  // helper functions

    void init_mmap();

    // helper function for v4l parameters setting
    int set_control(uint32_t id, int val);
};

};
