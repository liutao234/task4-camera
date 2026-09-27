#include "Camera.hpp"
#include <iostream>

// 前提：SDK 已经初始化成功。
int runCameraDemo()//便于看清生命周期
{
    // 创建空对象
    Camera camera;

    std::cout << "Initial open state: "
              << camera.isOpen() << '\n';

    // 枚举设备
    MV_CC_DEVICE_INFO_LIST devices{};

    const int ret = MV_CC_EnumDevices(
        MV_GIGE_DEVICE | MV_USB_DEVICE,
        &devices//GigE相机和USB相机
    );

    if (ret != MV_OK) 
    {
        std::cerr << "EnumDevices failed: 0x"
                  << std::hex << ret << '\n';
        return 1;
    }

    std::cout << "Detected cameras: "
              << devices.nDeviceNum << '\n';

    if (devices.nDeviceNum == 0)
    {
    std::cout << "No camera connected.\n";
    return 0;
    }	

    const MV_CC_DEVICE_INFO* info = devices.pDeviceInfo[0];

    if (info == nullptr) {
        std::cerr << "Device information is null.\n";
        return 1;
    }

    // info 是指针，*info 表示它指向的设备信息对象。
    // open() 接收的是该对象的 const 引用。
    if (!camera.open(*info)) {
        return 1;
    }

    std::cout << "Camera opened.\n";

    // 显式关闭，让主程序能够检查清理是否出现错误。
    if (!camera.close()) {
        return 1;
    }

    std::cout << "Camera closed.\n";
    return 0;

    // 从本函数任何位置 return，局部对象 camera 都会析构.
}

int main()
{
    // SDK 是整个程序使用的基础资源，先初始化。
    const int init_ret = MV_CC_Initialize();

    if (init_ret != MV_OK) {
        std::cerr << "Initialize failed: 0x"
                  << std::hex << init_ret << '\n';
        return 1;
    }

    std::cout << "SDK initialized.\n";

    // 函数内部创建 Camera；函数返回时，它已经析构。
    const int exit_code = runCameraDemo();

    // 先完成 Camera 的清理，再结束 SDK。
    std::cout << "Camera scope ended.\n";

    const int final_ret = MV_CC_Finalize();

    if (final_ret != MV_OK) {
        std::cerr << "Finalize failed: 0x"
                  << std::hex << final_ret << '\n';
        return 1;
    }

    std::cout << "SDK finalized.\n";

    // 保留相机操作阶段的成功或失败结果。
    return exit_code;
}
