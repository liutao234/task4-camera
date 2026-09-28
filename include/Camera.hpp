#pragma once

#include "MvCameraControl.h"
#include <vector>

// 创建一个Frame对象，用来接受图像
struct Frame {
    std::vector<unsigned char> data;

    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int pixel_type = 0;    // 相机输出的像素格式
    unsigned int frame_number = 0;  // SDK 返回的帧号

    // 清空上一次取图结果。
    void clear() noexcept
    {
        data.clear();
        width = height = pixel_type = frame_number = 0;
    }
};

// 采用自己定义的返回状态来让调用者清楚了解本次read()的执行情况
enum class ReadStatus {
    Ok,      // 已取得并复制一帧，SDK 缓存也已归还
    NoData,  // 本次等待没有拿到帧
    Error    // 操作顺序、SDK 调用、复制或缓存归还出现错误
};

class Camera {
public:
    Camera() = default;
    ~Camera() noexcept;

    // 同一个句柄只能由一个 Camera 对象负责管理。
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) = delete;
    Camera& operator=(Camera&&) = delete;

    bool open(const MV_CC_DEVICE_INFO& device_info);
    bool close() noexcept;

    // 配置为连续、非触发采集，然后开始取流。
    bool start();

    // 停止采集
    bool stop() noexcept;

    // 获取一帧
    // Error 后，调用者应结束本次采集并关闭相机。
    ReadStatus read(Frame& out, unsigned int timeout_ms = 1000);

    // 本对象记录的状态
    bool isOpen() const noexcept
    {
        return opened_;
    }

    bool isGrabbing() const noexcept
    {
        return grabbing_;
    }

private:
    void* handle_ = nullptr;
    bool opened_ = false;

    // 只有 StartGrabbing 成功后才设置为 true。
    bool grabbing_ = false;
};
