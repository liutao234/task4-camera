#include "Camera.hpp"
#include <cstdio> 
#include <exception>
#include <opencv2/imgproc.hpp> //图像处理
#include <cstdint> //指定位数的整数类型
#include <stdexcept> //提供更具体的异常类型
#include <cstring> //内存复制
#include <limits> //查询数值范围
#include <sstream> //将文字、数字等转换为字符串

Camera::~Camera() noexcept
{
    // 析构函数不能返回清理结果；通过显式调用close（）来检查返回值
    close();
}

bool Camera::open(const MV_CC_DEVICE_INFO& device_info)
{ 
    // 防止覆盖旧句柄。
    if (handle_ != nullptr) {
        std::fprintf(
            stderr,
            "Camera still owns a handle; close it first.\n"
        );
        return false;
    }

    // 创建句柄
    int ret = MV_CC_CreateHandle(&handle_, &device_info);

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "CreateHandle failed: 0x%08X\n",//按十六进制输出错误码，便于对照
            static_cast<unsigned int>(ret)//保持参数类型一致
        );
        return false;
    }

    // 使用句柄打开设备。
    ret = MV_CC_OpenDevice(handle_, MV_ACCESS_Exclusive, 0);
    //最后的0为nSwitchoverKey，用于切换相机访问权限时传入一个密钥数值。
    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "OpenDevice failed: 0x%08X\n",//报错输出错误码
            static_cast<unsigned int>(ret)
        );
        close();
        return false;
    }

    // 只有 SDK 确认打开成功，才更新状态。
    opened_ = true;

    // USB 相机在打开后、开始采集前配置传输通道。
    // 限制并发传输，验证当前虚拟 USB 控制器的兼容性。
    if (device_info.nTLayerType == MV_USB_DEVICE) {
        unsigned int original_ways = 0;
        int usb_ret = MV_USB_GetTransferWays(handle_, &original_ways);
        if (usb_ret != MV_OK) {
            std::fprintf(stderr, "读取 USB 传输通道数失败，错误码：0x%08X\n",
                         static_cast<unsigned int>(usb_ret));
            close();
            return false;
        }

        // SDK 允许 1～10 个通道；这里仅使用 1 个通道。
        // 保持原传输块大小、分辨率和像素格式。
        constexpr unsigned int transfer_ways = 1;
        usb_ret = MV_USB_SetTransferWays(handle_, transfer_ways);
        if (usb_ret != MV_OK) {
            std::fprintf(stderr, "设置 USB 传输通道数失败，错误码：0x%08X\n",
                         static_cast<unsigned int>(usb_ret));
            close();
            return false;
        }

        // 回读确认配置生效，不在配置失败时继续取图。
        unsigned int actual_ways = 0;
        usb_ret = MV_USB_GetTransferWays(handle_, &actual_ways);
        if (usb_ret != MV_OK || actual_ways != transfer_ways) {
            std::fprintf(stderr,
                         "验证 USB 传输通道数失败，错误码：0x%08X，实际值：%u\n",
                         static_cast<unsigned int>(usb_ret), actual_ways);
            close();
            return false;
        }

        std::fprintf(stderr, "USB 传输通道数：%u → %u。\n",
                     original_ways, actual_ways);
    }

    // 为GigE相机设置网络包大小，提升传输效率。
    if (device_info.nTLayerType == MV_GIGE_DEVICE) {
        int packet_size = MV_CC_GetOptimalPacketSize(handle_);

        if (packet_size <= 0) {
        std::fprintf(
            stderr,
            "Packet-size query failed: 0x%08X; trying 1500.\n",
            static_cast<unsigned int>(packet_size));

        packet_size = 1500;
    }

    const int packet_ret = MV_CC_SetIntValue(
        handle_,
        "GevSCPSPacketSize",
        static_cast<unsigned int>(packet_size));

    if (packet_ret != MV_OK) {
        std::fprintf(
            stderr,
            "Warning: Set packet size failed: 0x%08X\n",
            static_cast<unsigned int>(packet_ret));
    }
}

return true;
}

bool Camera::close() noexcept
{
    if (handle_ == nullptr) {
        return true;
    }
    // 停止采集 -> 关闭设备 -> 销毁句柄。
    bool all_ok = stop();

    if (opened_) {
        const int ret = MV_CC_CloseDevice(handle_);//关闭设备

        if (ret != MV_OK) {
            std::fprintf(
                stderr,
                "CloseDevice failed: 0x%08X\n",
                static_cast<unsigned int>(ret)
            );
            all_ok = false;

        } else {
            opened_ = false;
            grabbing_ = false;//设备已关闭，不再记录
        }
    }

    const int ret = MV_CC_DestroyHandle(handle_);//销毁句柄

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "DestroyHandle failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );

        // 保留句柄，便于后续再次尝试清理。
        return false;
    }

    handle_ = nullptr;
    opened_ = false;
    grabbing_ = false;

    return all_ok;
}

bool Camera::start()
{
    // 检测是否打开相机，防止在未打开相机时采集
    if (!opened_) {
        std::fprintf(stderr, "start(): camera is not open.\n");
        return false;
    }

    // 已经开始采集的情况下再次调用 start()，函数会直接返回true，避免重复采集。
    if (grabbing_) {
        return true;
    }

    // 切换连续采集模式。
    int ret = MV_CC_SetEnumValueByString(
        handle_, "AcquisitionMode", "Continuous");

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "Set AcquisitionMode failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // 关闭触发：本例无需外部信号或软件触发命令即可取图。
    ret = MV_CC_SetEnumValueByString(
        handle_, "TriggerMode", "Off");

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "Set TriggerMode failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // 配置成功后，正式开始取流。
    ret = MV_CC_StartGrabbing(handle_);

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "StartGrabbing failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // SDK 确认成功后，才修改本对象的状态。
    grabbing_ = true;
    return true;
}

bool Camera::stop() noexcept
{
    if (!grabbing_) {
        return true;
    }

    const int ret = MV_CC_StopGrabbing(handle_);

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "StopGrabbing failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );

        // 确认是否停止成功。若停止失败，保持grabbing_原来的值
        return false;
    }

    grabbing_ = false;
    return true;
}

ReadStatus Camera::read(Frame& out, unsigned int timeout_ms)
{
    // 每次读取前，先清空上一次结果。防止本次取图失败后，误把上一帧当作当前帧。
    out.clear();

    if (!grabbing_) {
        std::fprintf(stderr, "read(): acquisition has not started.\n");
        return ReadStatus::Error;
    }

    // 用于接收 SDK 返回的缓存地址及帧信息。
    MV_FRAME_OUT sdk_frame{};

    const int ret = MV_CC_GetImageBuffer(
        handle_, &sdk_frame, timeout_ms);

    // 本次没有取得帧，没有缓存需要归还。
    if (static_cast<unsigned int>(ret) == MV_E_NODATA) {
        return ReadStatus::NoData;
    }

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "GetImageBuffer failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return ReadStatus::Error;
    }

    // 从这里起，已经成功取得 SDK 缓存，不能提前 return。
    // 无论后续是否成功，都要执行下面的 FreeImageBuffer。
    ReadStatus result = ReadStatus::Error;

    const auto& info = sdk_frame.stFrameInfo;

    if (sdk_frame.pBufAddr == nullptr ||
        info.nFrameLen == 0 ||
        info.nWidth == 0 ||
        info.nHeight == 0 ||
        info.nLostPacket != 0) {

        std::fprintf(stderr, "Invalid or incomplete frame.\n");

    } else {
        try {
            // 不能只保存 pBufAddr，因为这个指针指向的图像缓存是属于SDK的
            out.data.assign(
                sdk_frame.pBufAddr,//第一个字节
                sdk_frame.pBufAddr + info.nFrameLen//最后一个字节
            );
	    //用assign()提前把内容复制到out.data中

            // 元数据也必须在归还 SDK 缓存之前读取。
            out.width = info.nWidth;
            out.height = info.nHeight;
            out.pixel_type =
                static_cast<unsigned int>(info.enPixelType);
            out.frame_number = info.nFrameNum;

            result = ReadStatus::Ok;

        } catch (const std::exception& e) {
            // vector 分配内存等操作可能抛出异常。
            // 捕获后继续向下执行，尝试归还 SDK 缓存。
            std::fprintf(
                stderr, "Copy frame failed: %s\n", e.what());
        }
    }

    // 与前面成功的 GetImageBuffer 配对。
    const int free_ret =
        MV_CC_FreeImageBuffer(handle_, &sdk_frame);

    if (free_ret != MV_OK) {
        std::fprintf(
            stderr,
            "FreeImageBuffer failed: 0x%08X\n",
            static_cast<unsigned int>(free_ret)
        );
        result = ReadStatus::Error;
    }

    if (result != ReadStatus::Ok) {
        out.clear();
    }

    return result;//清空输出帧内容
}

cv::Mat Camera::toBgr(Frame& frame)
{
    // SDK 的转换接口需要相机句柄，所以转换应在 close() 之前完成。
    if (!opened_) {
        throw std::runtime_error("toBgr(): camera is not open");
    }

    const auto max_int =//获取int类型的最大值
        static_cast<unsigned int>(std::numeric_limits<int>::max());

    const auto max_uint =//将刚刚int转换成unsigned int以获取unsigned int类型的最大值
        std::numeric_limits<unsigned int>::max();

    // OpenCV 的行数、列数使用 int。
    // SDK 转换接口的数据长度使用 unsigned int。
    if (frame.width == 0 || frame.height == 0 || frame.data.empty() ||
        frame.width > max_int || frame.height > max_int ||
        frame.data.size() > max_uint) {
        throw std::runtime_error(
            "Invalid frame dimensions or data length");
    }

    // Bayer 是彩色格式，不能当作普通灰度图处理。
    bool mono = false;//标记是否是灰度图像

    switch (frame.pixel_type) {
    case PixelType_Gvsp_Mono8:
    case PixelType_Gvsp_Mono10:
    case PixelType_Gvsp_Mono10_Packed:
    case PixelType_Gvsp_Mono12:
    case PixelType_Gvsp_Mono12_Packed:
    case PixelType_Gvsp_Mono16:
        mono = true;//匹配到上述灰度图像格式，设置mono为true
        break;

    default:
        break;
    }

    // 先使用 64 位整数计算像素数，再检查目标缓存的大小。
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(frame.width) * frame.height;

    if (pixels > max_uint / 3) {
        throw std::runtime_error("Frame is too large");
    }

    // 黑白图先统一成 Mono8，彩色图统一成 BGR8。
    const auto target =
        mono ? PixelType_Gvsp_Mono8 : PixelType_Gvsp_BGR8_Packed;

    const unsigned int bytes =
        static_cast<unsigned int>(pixels * (mono ? 1 : 3));

    // CV_8UC1：每个通道是 8 位无符号整数，1 个通道。
    // CV_8UC3：每个通道是 8 位无符号整数，3 个通道。
    // 分配图像内存
    cv::Mat converted(
        static_cast<int>(frame.height),
        static_cast<int>(frame.width),
        mono ? CV_8UC1 : CV_8UC3); //根据mono选择像素类型

    if (frame.pixel_type == static_cast<unsigned int>(target)) {
        // 判断输入帧的像素格式是否已经与目标格式相同
        if (frame.data.size() != bytes) {
            throw std::runtime_error("Unsupported frame layout");
        }
        std::memcpy(converted.data, frame.data.data(), bytes);// 复制到 Mat 自己的内存，不借用 frame.data 的指针。
    } else {
        // 初始化结构体中的各个字段，传给 SDK 的转换接口。
        MV_CC_PIXEL_CONVERT_PARAM_EX parameter{};

        parameter.nWidth = frame.width;
        parameter.nHeight = frame.height;

        // 输入信息：原始格式、原始数据地址、原始数据长度。
        parameter.enSrcPixelType =
            static_cast<MvGvspPixelType>(frame.pixel_type);

        parameter.pSrcData = frame.data.data();
        parameter.nSrcDataLen =
            static_cast<unsigned int>(frame.data.size());

        // 输出信息：目标格式、目标缓存地址、缓存容量。
        parameter.enDstPixelType = target;
        parameter.pDstBuffer = converted.data;
        parameter.nDstBufferSize = bytes;

        const int ret =
            MV_CC_ConvertPixelTypeEx(handle_, &parameter);

        if (ret != MV_OK) {
            std::ostringstream message;

            message << "ConvertPixelTypeEx failed: 0x"
                    << std::hex << static_cast<unsigned int>(ret)
                    << ", source pixel type: 0x"
                    << frame.pixel_type;

            throw std::runtime_error(message.str());
        }

        // SDK 成功返回后，还要核对实际写入的数据长度。
        if (parameter.nDstLen != bytes) {
            throw std::runtime_error(
                "Unexpected converted image length");
        }
    }

    if (mono) {
        // 灰度图扩展为三通道，让函数统一返回 BGR 图像。
        cv::Mat bgr;
        cv::cvtColor(converted, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }

    return converted;
}