#pragma once

#include "Module.h"
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <memory>
#include "Logger.h"
class OpencvWebcamProps : public ModuleProps
{
public:
    OpencvWebcamProps()
    {
        vidCapWidth = 1920;
        vidCapHeight = 960;
    }
    OpencvWebcamProps(uint32_t _width, uint32_t _height)
    {
        vidCapWidth = _width;
        vidCapHeight = _height;
    }

    uint32_t vidCapWidth;
    uint32_t vidCapHeight;
};

class OpencvWebcam : public Module
{
public:
    OpencvWebcam(OpencvWebcamProps props);
    virtual ~OpencvWebcam();
    bool validateOutputPins();
    bool produce();
    bool init();
    bool term();
    // cv::VideoCapture vid_capture;

private:
    class Detail;
    std::string mOutputPinId;
    std::shared_ptr<Detail> mDetail;
    OpencvWebcamProps mProps;
    
};