#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <MvCameraControl.h>

// Frame类用于保存相机采集到的一帧图像，以及这帧图像的编号
class Frame
{
public:
    Frame() = default;

    // 清空上一次取图结果，防止数据残留产生错误。
    void clear() noexcept
    {
        m_data.clear();
        m_width = m_height = m_pixel_type = m_frame_number = 0; // 清空元信息
    }

    // 只读访问 SDK 返回的帧号，不暴露可写的数据成员。
    unsigned int frame_number() const noexcept
    {
        return m_frame_number;
    }

private:
    // 只有负责取帧和格式转换的 Camera 可以写入这些数据。
    friend class Camera;

    std::vector<unsigned char> m_data{}; //初始化 Frame 类中定义一个保存图像原始字节数据的成员
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    unsigned int m_pixel_type = 0;    // 相机输出的像素格式。
    unsigned int m_frame_number = 0;  // SDK 返回的帧号。
};

// 让调用者区分成功取帧、暂时没有数据和真正的错误。
enum class ReadStatus : std::uint8_t //枚举状态
{
    Ok,      // 已取得并复制一帧，SDK 缓存也已归还。
    NoData,  // 本次等待没有拿到帧。
    Error    // 发生错误。
};

class Camera
{
public:
    Camera() = default; //构造函数
    ~Camera() noexcept; //析构函数，关闭相机并释放资源

    // 同一个句柄只能由一个 Camera 对象负责管理，禁止复制和移动，由delete表示禁止使用该函数。
    Camera(const Camera&) = delete; //禁止拷贝构造，const Camera&只读引用
    Camera& operator=(const Camera&) = delete; //禁止拷贝赋值，operator赋值运算符
    Camera(Camera&&) = delete; //禁止移动构造
    Camera& operator=(Camera&&) = delete; //禁止移动赋值

    // SDK 全局操作，不依赖某个 Camera 对象的句柄。
    // 程序开始时初始化一次；所有 Camera 对象销毁后再配对释放。
    static bool initialize() noexcept; //static表示静态成员函数，初始化时不需要句柄，因此可以作为类的静态方法调用
    static bool finalize() noexcept;

    // 初始化 SDK 后枚举 GigE 和 USB 相机，设备数量由 devices 返回。
    // 列表中的设备信息由 SDK 管理，调用方不负责释放。
    static bool enumerate_devices(MV_CC_DEVICE_INFO_LIST& devices) noexcept;

    // 打开相机。
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

    // 查询状态
    bool is_open() const noexcept
    {
        return m_opened;
    }

    bool is_grabbing() const noexcept
    {
        return m_grabbing;
    }

private://类的私有成员，为其内部保存的数据和状态
    void* m_handle = nullptr;
    bool m_opened = false;

    // 只有 StartGrabbing 成功后才设置为 true。
    bool m_grabbing = false;
};
