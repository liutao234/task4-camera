#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <MvCameraControl.h>

// Frame 独立持有一帧图像；外部代码不能直接修改图像数据和元信息。
class Frame
{
public:
    Frame() = default;

    // 清空上一次取图结果。
    void clear() noexcept
    {
        m_data.clear();
        m_width = m_height = m_pixel_type = m_frame_number = 0;
    }

    // 只读访问 SDK 返回的帧号，不暴露可写的数据成员。
    unsigned int frame_number() const noexcept
    {
        return m_frame_number;
    }

private:
    // 只有负责取帧和格式转换的 Camera 可以写入这些数据。
    friend class Camera;

    std::vector<unsigned char> m_data{};
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    unsigned int m_pixel_type = 0;    // 相机输出的像素格式。
    unsigned int m_frame_number = 0;  // SDK 返回的帧号。
};

// 让调用者区分成功取帧、暂时没有数据和真正的错误。
enum class ReadStatus : std::uint8_t
{
    Ok,      // 已取得并复制一帧，SDK 缓存也已归还。
    NoData,  // 本次等待没有拿到帧。
    Error    // 操作顺序、SDK 调用、复制或缓存归还出现错误。
};

class Camera
{
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

    // 停止采集。
    bool stop() noexcept;

    // 获取一帧；Error 后，调用者应结束本次采集并关闭相机。
    ReadStatus read(Frame& out, unsigned int timeout_ms = 1000);

    // 相机打开期间，将原始帧转换为独立拥有数据的 BGR 图像。
    // 转换失败会抛出异常，由 main 捕获。
    cv::Mat to_bgr(Frame& frame);

    // 本对象记录的打开和采集状态。
    bool is_open() const noexcept
    {
        return m_opened;
    }

    bool is_grabbing() const noexcept
    {
        return m_grabbing;
    }

private:
    void* m_handle = nullptr;
    bool m_opened = false;

    // 只有 StartGrabbing 成功后才设置为 true。
    bool m_grabbing = false;
};
