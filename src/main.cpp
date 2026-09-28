#include "Camera.hpp"
#include <iostream>

// 前提：SDK 已经初始化成功。
int runCameraDemo()
{
    Camera camera;
    MV_CC_DEVICE_INFO_LIST devices{};

    const int ret = MV_CC_EnumDevices(
        MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);

    if (ret != MV_OK) {
        std::cerr << "EnumDevices failed: 0x"
                  << std::hex << ret << '\n';
        return 1;
    }

    std::cout << "Detected cameras: "
              << devices.nDeviceNum << '\n';

    if (devices.nDeviceNum == 0) {
        // 没有设备时，只检查空对象清理。
        if (!camera.stop() || !camera.close()) {
            return 1;
        }

        std::cout
            << "No camera connected; empty-object cleanup passed.\n";
        return 0;
    }

    const MV_CC_DEVICE_INFO* info = devices.pDeviceInfo[0];

    if (info == nullptr) {
        std::cerr << "Device information is null.\n";
        return 1;
    }

    // open 成功后，才会继续调用 start。
    if (!camera.open(*info) || !camera.start()) {
        return 1;
    }

    Frame frame;
    unsigned int received = 0;

    // 最多尝试 10 次。
    // 每次等待一帧的时间为 1000 毫秒。
    for (int attempt = 0; attempt < 10; ++attempt) {
        const ReadStatus status = camera.read(frame, 1000);

        if (status == ReadStatus::NoData) {
            std::cerr << "No frame on attempt "
                      << attempt + 1 << "/10.\n";
            continue;
        }

        if (status == ReadStatus::Error) {
            // 返回时 camera 析构，自动尝试停止和关闭。
            return 1;
        }

        // 只有 Ok 时，才读取 frame 的内容。
        ++received;

        std::cout << "Frame " << frame.frame_number
                  << ": " << frame.width << "x" << frame.height
                  << ", bytes=" << frame.data.size()
                  << ", pixel_type=0x"
                  << std::hex << frame.pixel_type
                  << std::dec << '\n';
    }

    // 两个清理操作分别执行。
    bool cleanup_ok = camera.stop();

    if (!camera.close()) {
        cleanup_ok = false;
    }

    std::cout << "Received frames: " << received << '\n';

    // 有设备却一帧也没拿到，本次采集验证不算成功。
    return (cleanup_ok && received > 0) ? 0 : 1;
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
