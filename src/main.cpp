#include <Camera.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

// Ctrl+C 只设置退出标记，资源关闭仍由正常的程序流程完成。
volatile std::sig_atomic_t stop_requested = 0;

void on_signal(int)
{
    stop_requested = 1;
}

void save_image(const cv::Mat& image, unsigned int frame_number)
{
    // 保存失败时报告错误，然后继续预览。
    try
    {
        std::filesystem::create_directories("output");

        static unsigned int counter = 0;
        std::string path{};

        // 跳过已有文件，避免覆盖以前保存的图片。
        do
        {
            path = "output/frame_" + std::to_string(counter++) + ".png";
        }
        while (std::filesystem::exists(path));

        if (!cv::imwrite(path, image))
        {
            throw std::runtime_error("图像写入失败");
        }

        std::cout
            << "保存成功：第 " << frame_number << " 帧，文件位置："
            << path << '\n'
            << "下一步：在预览窗口按小写 s 保存下一张；"
               "按小写 q 或 Esc 退出，无需回车。\n";
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "保存失败：" << e.what() << '\n'
            << "预览仍在继续。下一步：检查保存位置后，"
               "在预览窗口按小写 s 重试；"
               "按小写 q 或 Esc 退出。\n";
    }
}

int run_preview()
{
    Camera camera{};
    MV_CC_DEVICE_INFO_LIST devices{};

    const int ret = MV_CC_EnumDevices(
        MV_GIGE_DEVICE | MV_USB_DEVICE,
        &devices
    );

    if (ret != MV_OK)
    {
        std::cerr << "枚举相机失败，错误码：0x" << std::hex
                  << static_cast<unsigned int>(ret)
                  << std::dec << '\n';
        return 1;
    }

    if (devices.nDeviceNum == 0 || devices.pDeviceInfo[0] == nullptr)
    {
        std::cerr
            << "未找到可用相机，请检查相机连接。\n";
        return 1;
    }

    // 默认打开枚举到的第 0 台相机。
    std::cout
        << "发现 " << devices.nDeviceNum
        << " 台相机，正在打开编号为 0 的相机\n";

    if (!camera.open(*devices.pDeviceInfo[0]) || !camera.start())
    {
        return 1; // 局部 Camera 对象析构时仍会执行清理。
    }

    using Clock = std::chrono::steady_clock;
    auto last_frame_time = Clock::now();
    cv::namedWindow("Camera", cv::WINDOW_NORMAL);
    cv::setWindowTitle(
        "Camera",
        "相机预览 | 等待首帧 | q 或 Esc：退出"
    );

    // 首帧到达前显示灰色背景，状态说明放在窗口标题中。
    // 这个等待画面不会被当作相机图像保存。
    cv::Mat waiting(
        360,
        640,
        CV_8UC3,
        cv::Scalar(30, 30, 30)
    );
    cv::imshow("Camera", waiting);

    std::cout
        << "\n相机已开始采集，正在等待图像。\n"
        << "下一步：点击相机预览窗口，并切换到英文输入法。\n"
        << "  收到图像后，按 s 保存当前显示的图像。\n"
        << "  按 q 或 Esc 退出。\n"
        << "  图片将会保存在程序运行目录下的 output 文件夹。\n\n";

    Frame frame{};
    cv::Mat last_image{};
    unsigned int last_frame_number = 0;

    while (!stop_requested)
    {
        // 每次最多等待 100 毫秒，以便及时处理窗口操作。
        const ReadStatus status = camera.read(frame, 100);

        if (stop_requested)
        {
            break;
        }

        if (status == ReadStatus::Error)
        {
            return 1;
        }

        if (status == ReadStatus::Ok)
        {
            last_image = camera.to_bgr(frame);
            last_frame_number = frame.frame_number();
            last_frame_time = Clock::now();

            cv::imshow("Camera", last_image);
        }

        // 处理窗口事件，并读取键盘操作。
        const int key = cv::waitKey(1);

        // 小写 q 或 Esc 表示用户主动退出。
        if (key == 'q' || key == 27)
        {
            std::cout << "已收到退出按键，正在停止采集。\n";
            break;
        }

        // 某些窗口后端不支持这个查询，会返回负数。
        // 不能把负数当成“窗口已关闭”，否则程序会立即退出。
        const double visible =
            cv::getWindowProperty("Camera", cv::WND_PROP_VISIBLE);

        if (visible == 0.0)
        {
            std::cout << "预览窗口已关闭，正在停止采集。\n";
            break;
        }

        const auto elapsed = Clock::now() - last_frame_time;

        cv::setWindowTitle(
            "Camera",
            last_image.empty() ||
                    elapsed > std::chrono::milliseconds(500)
                ? "相机预览 | 等待新图像 | q 或 Esc：退出"
                : "相机预览 | 实时采集 | s：保存 | q 或 Esc：退出"
        );

        // 根据实际经过的时间判断是否持续没有有效图像。
        if (status != ReadStatus::Ok &&
            elapsed >= std::chrono::seconds(10))
        {
            throw std::runtime_error(
                "连续 10 秒未收到有效图像，请检查相机连接和采集设置"
            );
        }

        // 至少收到一帧图像后，才允许按 s 保存。
        if (key == 's' && !last_image.empty())
        {
            const auto save_start = Clock::now();

            save_image(last_image, last_frame_number);

            // 保存图片花费的时间不计入相机无帧等待时间。
            last_frame_time += Clock::now() - save_start;
        }
    }

    // 分别执行两个清理动作，避免短路求值跳过 close()。
    const bool stopped = camera.stop();
    const bool closed = camera.close();

    return (stopped && closed) ? 0 : 1;
}

int main()
{
    if (std::signal(SIGINT, on_signal) == SIG_ERR ||
        std::signal(SIGTERM, on_signal) == SIG_ERR)
    {
        std::cerr
            << "注册退出信号处理函数失败。\n"
            << "下一步：检查错误后，在终端输入 ./build/camera，"
               "再按回车重新运行。\n";
        return 1;
    }

    const int init_ret = MV_CC_Initialize();

    if (init_ret != MV_OK)
    {
        std::cerr << "初始化相机 SDK 失败，错误码：0x" << std::hex
                  << static_cast<unsigned int>(init_ret)
                  << std::dec << '\n'
                  << "下一步：检查 SDK 环境后，在终端输入 "
                     "./build/camera，再按回车重新运行。\n";
        return 1;
    }

    int exit_code = 0;

    try
    {
        exit_code = run_preview();
    }
    catch (const std::exception& e)
    {
        // 到这里时，run_preview 内的 Camera 对象已经析构。
        std::cerr << "程序运行错误：" << e.what() << '\n';
        exit_code = 1;
    }

    // 窗口清理失败时仍继续执行 SDK 清理。
    try
    {
        cv::destroyAllWindows();
    }
    catch (const std::exception& e)
    {
        std::cerr << "关闭预览窗口失败：" << e.what() << '\n';
        exit_code = 1;
    }

    const int final_ret = MV_CC_Finalize();

    if (final_ret != MV_OK)
    {
        std::cerr << "释放相机 SDK 资源失败，错误码：0x" << std::hex
                  << static_cast<unsigned int>(final_ret)
                  << std::dec << '\n';
        exit_code = 1;
    }

    if (exit_code == 0)
    {
        std::cout << "\n程序已正常结束。\n";
    }
    else
    {
        std::cerr
            << "\n程序因错误结束，请查看上方提示和错误码。\n";
    }

    std::cout
        << "下一步：如需再次运行，在终端输入 ./build/camera，"
           "然后按回车。\n";

    return exit_code;
}
