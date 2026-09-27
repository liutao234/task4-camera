#include "Camera.hpp"
#include <cstdio> 

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
    }//检查句柄是否已清理

    // 用all_ok来记录本次清理过程中是否出现过错误。
    bool all_ok = true;

    // 如果设备曾成功打开，就尝试关闭设备。
    if (opened_) {
        const int ret = MV_CC_CloseDevice(handle_);

        if (ret != MV_OK) {
            std::fprintf(
                stderr,
                "CloseDevice failed: 0x%08X\n",
                static_cast<unsigned int>(ret)
            );
            all_ok = false;
        } else {
            // 即使后续销毁句柄失败，也要记住设备已经关闭。
            opened_ = false;
        }
    }

    // 尝试销毁句柄。
    const int ret = MV_CC_DestroyHandle(handle_);

    if (ret != MV_OK) {
        std::fprintf(
            stderr,
            "DestroyHandle failed: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );

        // 保留句柄，以进行后续尝试
        return false;
    }

    // 只有 SDK 确认销毁成功，才清空保存的句柄。
    handle_ = nullptr;
    opened_ = false;

    return all_ok;
}
