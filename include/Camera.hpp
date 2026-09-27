#pragma once  // 避免这个头文件在同一次编译中被重复包含，造成重复定义

#include "MvCameraControl.h"

class Camera {
public:
    Camera() = default;//默认构造函数，以初始化对象

    // 析构函数～Camera：对象销毁时自动执行，以释放资源
    // noexcept 表示这个函数承诺不向外抛出 C++ 异常。防止终止程序
    ~Camera() noexcept;

    Camera(const Camera&) = delete;//禁止复制构造
    Camera& operator=(const Camera&) = delete;//禁止复制赋值
    //保证一个句柄只由一个对象负责释放	

    // 打开枚举得到的设备。
    bool open(const MV_CC_DEVICE_INFO& device_info);

    // 关闭设备并销毁句柄。
    bool close() noexcept;

    // 查询本对象记录的打开状态。
    bool isOpen() const noexcept
    {
        return opened_;
    }

private:
    // SDK 返回的不透明句柄
    // nullptr 表示当前没有持有句柄。
    void* handle_ = nullptr;

    // 只有 OpenDevice 成功后才设为 true。
    bool opened_ = false;
};
