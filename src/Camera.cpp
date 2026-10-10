#include <Camera.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

bool Camera::initialize() noexcept //封装之前没有封装进来的初始化函数
{
    const int ret = MV_CC_Initialize();

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "初始化相机 SDK 失败，错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    return true;
}

bool Camera::finalize() noexcept //封装之前没有封装进来的释放函数
{
    const int ret = MV_CC_Finalize();

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "释放相机 SDK 资源失败，错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    return true;
}

bool Camera::enumerate_devices(MV_CC_DEVICE_INFO_LIST& devices) noexcept //检查发现枚举相机也没有封装进来，本次封装进来
{
    devices = {};

    const int ret = MV_CC_EnumDevices(
        MV_GIGE_DEVICE | MV_USB_DEVICE,
        &devices
    );

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "枚举相机失败，错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        devices = {};
        return false;
    }

    return true;
}

Camera::~Camera() noexcept
{
    // 析构函数不能返回清理结果；通过显式调用 close() 来检查返回值。
    close();
}

bool Camera::open(const MV_CC_DEVICE_INFO& device_info)
{
    // 检查以防止覆盖旧句柄。
    if (m_handle != nullptr)
    {
        std::fprintf(
            stderr,
            "相机仍然拥有旧句柄，请先关闭。\n"
        );
        return false;
    }

    // 创建句柄。
    int ret = MV_CC_CreateHandle(&m_handle, &device_info);

    if (ret != MV_OK)
    {
        // 按十六进制输出错误码，便于对照 SDK 文档。
        std::fprintf(
            stderr,
            "创建句柄失败，错误码:0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // 使用句柄打开设备。
    // 最后的 0 为 nSwitchoverKey，用于切换相机访问权限时传入密钥。
    ret = MV_CC_OpenDevice(m_handle, MV_ACCESS_Exclusive, 0);
    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "设备打开失败。错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        close();
        return false;
    }

    // 确认 SDK 打开成功后，才更新状态。
    m_opened = true;

    // 相机在开始采集前配置传输通道。
    if (device_info.nTLayerType == MV_USB_DEVICE) //nTLayerType属于设备信息对象 device_info，表示设备传输协议
    {
        unsigned int original_ways = 0;
        int usb_ret = MV_USB_GetTransferWays(m_handle, &original_ways);
        if (usb_ret != MV_OK)
        {
            std::fprintf(
                stderr,
                "读取 USB 传输通道数失败，错误码:0x%08X\n",
                static_cast<unsigned int>(usb_ret) 
                //static_cast用于转换类型，将usb_ret转换为unsigned int类型，以便在fprintf中正确输出十六进制格式
            );
            close();
            return false;
        }

        // 保持原传输块大小、分辨率和像素格式。
        constexpr unsigned int transfer_ways = 1; //constexpr用于声明一个编译期常量，表示USB传输通道数为1，通道数在编译时即可确定
        usb_ret = MV_USB_SetTransferWays(m_handle, transfer_ways);
        if (usb_ret != MV_OK)
        {
            std::fprintf(
                stderr,
                "设置 USB 传输通道数失败，错误码:0x%08X\n",
                static_cast<unsigned int>(usb_ret) 
            );
            close();
            return false;
        }

        // 回读确认配置生效，不在配置失败时继续取图。
        unsigned int actual_ways = 0;
        usb_ret = MV_USB_GetTransferWays(m_handle, &actual_ways);
        if (usb_ret != MV_OK || actual_ways != transfer_ways)
        {
            std::fprintf(
                stderr,
                "验证 USB 传输通道数失败，错误码:0x%08X，实际通道数：%u\n",
                static_cast<unsigned int>(usb_ret),
                actual_ways
            );
            close();
            return false;
        }

        std::fprintf(
            stderr,
            "USB 传输通道数：%u → %u。\n",
            original_ways,
            actual_ways
        );
    }

    // 为 GigE 相机设置网络包大小，提升传输效率。
    if (device_info.nTLayerType == MV_GIGE_DEVICE)
    {
        int packet_size = MV_CC_GetOptimalPacketSize(m_handle);

        if (packet_size <= 0)
        {
            std::fprintf(
                stderr,
                "数据包大小查询失败，错误码: 0x%08X; 将尝试使用 1500 字节大小。\n",
                static_cast<unsigned int>(packet_size)
            );

            packet_size = 1500;
        }

        const int packet_ret = MV_CC_SetIntValue( //设置数据包大小
            m_handle,
            "GevSCPSPacketSize", //流通道数据包大小
            static_cast<unsigned int>(packet_size)
        );

        if (packet_ret != MV_OK)
        {
            std::fprintf(
                stderr,
                "警告：设置数据包大小失败，错误码: 0x%08X\n",
                static_cast<unsigned int>(packet_ret)
            );
        }
    }

    return true;
}

bool Camera::close() noexcept
{
    if (m_handle == nullptr)
    {
        return true;
    }

    // 停止采集 -> 关闭设备 -> 销毁句柄。
    bool all_ok = stop();

    if (m_opened)
    {
        const int ret = MV_CC_CloseDevice(m_handle);

        if (ret != MV_OK)
        {
            std::fprintf(
                stderr,
                "关闭设备失败，错误码: 0x%08X\n",
                static_cast<unsigned int>(ret)
            );
            all_ok = false;
        }
        else
        {
            m_opened = false;
            m_grabbing = false; // 设备已关闭，因此在此处改变状态。
        }
    }

    const int ret = MV_CC_DestroyHandle(m_handle);

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "销毁句柄失败，错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );

        // 保留句柄，便于后续再次尝试清理。
        return false;
    }

    m_handle = nullptr;
    m_opened = false;
    m_grabbing = false;

    return all_ok;
}

bool Camera::start()
{
    // 检测是否打开相机，防止在未打开相机时采集。
    if (!m_opened)
    {
        std::fprintf(stderr, "start():相机未开启。\n");
        return false;
    }

    // 已经开始采集时再次调用 start()，直接返回 true，避免重复启动。
    if (m_grabbing)
    {
        return true;
    }

    int ret = MV_CC_SetEnumValueByString(
        m_handle,
        "AcquisitionMode",//采集模式：连续采集
        "Continuous"
    );

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "设置采集模式失败，错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    ret = MV_CC_SetEnumValueByString(
        m_handle,
        "TriggerMode",//触发模式：关闭
        "Off"
    );

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "设置触发模式失败。错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // 配置成功后，正式开始取流。
    ret = MV_CC_StartGrabbing(m_handle);

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "开始取流失败。错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );
        return false;
    }

    // SDK 确认成功后，才会修改本对象的状态。
    m_grabbing = true;
    return true;
}

bool Camera::stop() noexcept
{
    if (!m_grabbing)
    {
        return true;
    }

    const int ret = MV_CC_StopGrabbing(m_handle);

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "停止取流失败。错误码: 0x%08X\n",
            static_cast<unsigned int>(ret)
        );

        // 若停止失败，保持 m_grabbing 原来的值。
        return false;
    }

    m_grabbing = false;
    return true;
}

ReadStatus Camera::read(Frame& out, unsigned int timeout_ms)
{
    // 每次读取前，先用clear()清空上一次结果，避免取图失败后误用上一帧。
    out.clear();

    if (!m_grabbing)
    {
        std::fprintf(stderr, "read():采集暂未开始。\n");
        return ReadStatus::Error;
    }

    // 用于接收 SDK 返回的缓存地址及帧信息。
    MV_FRAME_OUT sdk_frame{}; //初始化 sdk_frame 结构体，{}表示使用默认构造函数进行初始化

    const int ret = MV_CC_GetImageBuffer( //获取图像缓存
        m_handle,
        &sdk_frame, //接收结果的结构体指针
        timeout_ms
    );

    // 本次没有取得帧，没有缓存需要归还。
    if (static_cast<unsigned int>(ret) == MV_E_NODATA)
    {
        return ReadStatus::NoData;
    }

    if (ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "获取图像缓存失败。错误码: 0x%08X\n",
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
        info.nLostPacket != 0)
    {
        std::fprintf(stderr, "无效或不完整的帧。\n");
    }
    else
    {
        try
        {
            // pBufAddr 指向 SDK 的缓存；先复制到 Frame 自己拥有的内存中。
            // assign() 的范围是 [首地址, 尾后地址)，不包含尾后地址，用于抛出异常。
            out.m_data.assign(
                sdk_frame.pBufAddr,
                sdk_frame.pBufAddr + info.nFrameLen
            );

            // 元数据也必须在归还 SDK 缓存之前读取。
            out.m_width = info.nWidth;
            out.m_height = info.nHeight;
            out.m_pixel_type =
                static_cast<unsigned int>(info.enPixelType);
            out.m_frame_number = info.nFrameNum;

            result = ReadStatus::Ok;
        }
        catch (const std::exception& e)
        {
            // vector 分配内存等操作可能抛出异常。
            // 捕获后继续向下执行，尝试归还 SDK 缓存。
            std::fprintf(
                stderr,
                "复制帧数据失败。错误原因: %s\n",
                e.what()
            );
        }
    }

    // 与前面成功的 GetImageBuffer 配对。
    const int free_ret = MV_CC_FreeImageBuffer(m_handle, &sdk_frame);

    if (free_ret != MV_OK)
    {
        std::fprintf(
            stderr,
            "释放图像缓存失败。错误码: 0x%08X\n",
            static_cast<unsigned int>(free_ret)
        );
        result = ReadStatus::Error;
    }

    if (result != ReadStatus::Ok)
    {
        out.clear();
    }

    // 返回本次取图和缓存归还的结果。
    return result;
}

cv::Mat Camera::to_bgr(Frame& frame)
{
    // SDK 的转换接口需要相机句柄，所以转换应在 close() 之前完成。
    if (!m_opened)
    {
        throw std::runtime_error("to_bgr():camera is not opened");
    }

    // 获取 int 的最大值，并转换为 unsigned int，便于比较帧尺寸。
    const auto max_int =
        static_cast<unsigned int>(std::numeric_limits<int>::max());

    // 直接查询 unsigned int 类型可以表示的最大值。
    const auto max_uint = std::numeric_limits<unsigned int>::max();

    // OpenCV 的行数、列数使用 int。
    // SDK 转换接口的数据长度使用 unsigned int。
    if (frame.m_width == 0 || frame.m_height == 0 || frame.m_data.empty() ||
        frame.m_width > max_int || frame.m_height > max_int ||
        frame.m_data.size() > max_uint)
    {
        throw std::runtime_error(
            "Invalid frame size or data length"
        );
    }

    // Bayer 是彩色格式，不能当作普通灰度图处理。
    bool mono = false; // 标记是否为灰度格式。

    switch (frame.m_pixel_type)
    {
        case PixelType_Gvsp_Mono8:
        case PixelType_Gvsp_Mono10:
        case PixelType_Gvsp_Mono10_Packed:
        case PixelType_Gvsp_Mono12:
        case PixelType_Gvsp_Mono12_Packed:
        case PixelType_Gvsp_Mono16:
            mono = true;
            break;

        default:
            break;
    }

    // 先使用 64 位整数计算像素数，再检查目标缓存的大小。
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(frame.m_width) * frame.m_height;

    if (pixels > max_uint / 3)
    {
        throw std::runtime_error("Frame is too large");
    }

    // 黑白图先统一成 Mono8，彩色图统一成 BGR8。
    const auto target =
        mono ? PixelType_Gvsp_Mono8 : PixelType_Gvsp_BGR8_Packed;

    const unsigned int bytes =
        static_cast<unsigned int>(pixels * (mono ? 1 : 3));

    // CV_8UC1：每个通道是 8 位无符号整数，1 个通道。
    // CV_8UC3：每个通道是 8 位无符号整数，3 个通道。
    // 根据 mono 分配目标图像的内存。
    cv::Mat converted( //创建OpenCV的Mat对象，存储转换后的图像数据
        static_cast<int>(frame.m_height),
        static_cast<int>(frame.m_width),
        mono ? CV_8UC1 : CV_8UC3 //？表示条件成立时的值 : 条件不成立时的值
    );

    if (frame.m_pixel_type == static_cast<unsigned int>(target))
    {
        // 输入帧的像素格式已经与目标格式相同时。
        if (frame.m_data.size() != bytes)
        {
            throw std::runtime_error("Unsupported frame layout");
        }

        //从 Frame 保存的图像数据中，复制 bytes 个字节到 converted 的图像内存中。
        std::memcpy(converted.data, frame.m_data.data(), bytes);//调用data()函数取得内部数组的起始地址
    }
    else
    {
        // 初始化结构体中的各个字段，传给 SDK 的转换接口。
        MV_CC_PIXEL_CONVERT_PARAM_EX parameter{}; //初始化结构体，用于转换像素格式

        parameter.nWidth = frame.m_width;
        parameter.nHeight = frame.m_height;

        // 输入信息：原始格式、原始数据地址、原始数据长度。
        parameter.enSrcPixelType = //将像素格式写入参数结构体，用于指定输入图像的像素格式。
            static_cast<MvGvspPixelType>(frame.m_pixel_type);

        parameter.pSrcData = frame.m_data.data(); //把地址保存到 pSrcData
        parameter.nSrcDataLen =
            static_cast<unsigned int>(frame.m_data.size()); //原始图像总字节数

        parameter.enDstPixelType = target; //目标像素格式
        parameter.pDstBuffer = converted.data; //目标图像的缓存地址
        parameter.nDstBufferSize = bytes; //目标图像缓存的容量，单位为字节

        const int ret = MV_CC_ConvertPixelTypeEx(m_handle, &parameter);

        if (ret != MV_OK)
        {
            std::ostringstream message{}; //输出字符串流，创建名为 message 的字符串流，初始内容为空

            message << "ConvertPixelTypeEx failed: 0x"
                    << std::hex << static_cast<unsigned int>(ret) //hex十六进制
                    << ", source pixel type: 0x"
                    << frame.m_pixel_type;

            throw std::runtime_error(message.str());
        }

        // SDK 成功返回后，还要核对实际写入的数据长度。
        if (parameter.nDstLen != bytes)
        {
            throw std::runtime_error(
                "Unexpected converted image length"
            );
        }
    }

    if (mono)
    {
        // 灰度图扩展为三通道，让函数统一返回 BGR 图像。
        cv::Mat bgr{};
        cv::cvtColor(converted, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }

    return converted;
}
