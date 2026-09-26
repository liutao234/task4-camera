#include <iostream>
#include "MvCameraControl.h"//SDK附带的头文件

int main()
{
    //程序启动，初始化SDK
    int ret = MV_CC_Initialize();
    //此处没有按照文档，借助ai在此处判断相机初始化是否成功

    if (ret != MV_OK)//若初始化失败，输出错误信息 
    {
        std::cerr << "Initialize failed: 0x"
                  << std::hex << ret << '\n';//改输出成十六进制
        return 1;
    }

    std::cout << "SDK initialized.\n";//显示SDK已初始化

    // 初始化变量来接收设备列表
    MV_CC_DEVICE_INFO_LIST devices{};

    //获取设备列表
    ret = MV_CC_EnumDevices(
        MV_GIGE_DEVICE | MV_USB_DEVICE,//GigE和USB相机
        &devices//存放设备列表
    );

    int exit_code = 0;

    if (ret != MV_OK) {//检测枚举情况
        std::cerr << "EnumDevices failed: 0x"
                  << std::hex << ret << '\n';
        exit_code = 1;
    } else {
        std::cout << "Detected cameras: "
                  << devices.nDeviceNum << '\n';
    }

    // 反初始化SDK，释放SDK资源
    const int cleanup_ret = MV_CC_Finalize();

    if (cleanup_ret != MV_OK) {
        std::cerr << "Finalize failed: 0x"
                  << std::hex << cleanup_ret << '\n';
        exit_code = 1;
    } else {
        std::cout << "SDK finalized.\n";
    }

    return exit_code;
}
