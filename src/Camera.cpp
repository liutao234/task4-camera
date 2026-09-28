#include "Camera.hpp"
#include <cstdio> 
#include <exception>

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
