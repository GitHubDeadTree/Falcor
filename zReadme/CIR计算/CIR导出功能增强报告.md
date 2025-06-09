# CIR导出功能增强报告

## 项目概述

本报告记录了对Falcor中CIR（信道脉冲响应）数据导出功能的重大增强，包括自动时间戳、多种导出格式支持以及专用存储目录的实现。

## 功能需求

用户提出了以下具体需求：
1. **修改保存位置**：将CIR数据保存到专用的`CIRData`目录
2. **添加时间戳后缀**：为导出文件自动添加时间戳，避免文件覆盖
3. **支持多种导出格式**：实现Excel兼容的CSV格式和JSONL格式

## 实现的功能

### 1. 导出格式枚举

在`PixelStats.h`中添加了导出格式枚举：

```cpp
// CIR export format enumeration
enum class CIRExportFormat
{
    CSV,        // Comma-separated values (default, compatible with Excel)
    JSONL,      // JSON Lines format
    TXT         // Original text format
};
```

### 2. 新增的导出方法

#### 自动时间戳导出方法
```cpp
bool exportCIRDataWithTimestamp(CIRExportFormat format = CIRExportFormat::CSV, const ref<Scene>& pScene = nullptr);
```
- 自动生成时间戳文件名
- 数据保存到`./CIRData/`目录
- 支持格式选择

#### 指定格式导出方法
```cpp
bool exportCIRDataWithFormat(const std::string& filename, CIRExportFormat format, const ref<Scene>& pScene = nullptr);
```
- 支持自定义文件名
- 支持多种导出格式
- 保持静态参数计算功能

### 3. UI界面增强

在PixelStats的UI中添加了：

```cpp
// CIR export format selection
const Gui::DropdownList kExportFormatList = {
    {(uint32_t)CIRExportFormat::CSV, "CSV (Excel compatible)"},
    {(uint32_t)CIRExportFormat::JSONL, "JSONL (JSON Lines)"},
    {(uint32_t)CIRExportFormat::TXT, "TXT (Original format)"}
};

uint32_t format = (uint32_t)mCIRExportFormat;
if (widget.dropdown("Export format", kExportFormatList, format))
{
    mCIRExportFormat = (CIRExportFormat)format;
}
```

提供了两个导出按钮：
- **"Export CIR Data (Auto-timestamped)"**：使用新的增强功能
- **"Export CIR Data (Original)"**：保持向后兼容性

### 4. 支持的导出格式

#### CSV格式（Excel兼容）
```csv
# CIR Path Data Export (CSV Format)
# Static Parameters for VLC Channel Impulse Response Calculation:
# A_receiver_area_m2,1.000000e-04
# m_led_lambertian_order,1.000
# c_light_speed_ms,3.000e+08
# FOV_receiver_rad,3.142
# T_s_optical_filter_gain,1.0
# g_optical_concentration,1.0
#
PathIndex,PixelX,PixelY,PathLength_m,EmissionAngle_rad,ReceptionAngle_rad,ReflectanceProduct,ReflectionCount,EmittedPower_W
0,100,200,1.234567,0.523599,0.785398,0.800000,2,0.001000
```

#### JSONL格式（JSON Lines）
```jsonl
{"type":"static_parameters","data":{"receiver_area_m2":1.000000e-04,"led_lambertian_order":1.000,"light_speed_ms":3.000e+08,"receiver_fov_rad":3.142,"optical_filter_gain":1.0,"optical_concentration":1.0}}
{"type":"path_data","data":{"path_index":0,"pixel_x":100,"pixel_y":200,"path_length_m":1.234567,"emission_angle_rad":0.523599,"reception_angle_rad":0.785398,"reflectance_product":0.800000,"reflection_count":2,"emitted_power_w":0.001000}}
```

#### TXT格式（原始格式）
```txt
# CIR Path Data Export with Static Parameters
# Static Parameters for VLC Channel Impulse Response Calculation:
# A_receiver_area_m2=1.000000e-04
# m_led_lambertian_order=1.000
# c_light_speed_ms=3.000e+08
# FOV_receiver_rad=3.142
# T_s_optical_filter_gain=1.0
# g_optical_concentration=1.0
#
# Path Data Format: PathIndex,PixelX,PixelY,PathLength(m),EmissionAngle(rad),ReceptionAngle(rad),ReflectanceProduct,ReflectionCount,EmittedPower(W)
0,100,200,1.234567,0.523599,0.785398,0.800000,2,0.001000
```

### 5. 自动目录创建和时间戳生成

#### 目录管理
```cpp
bool PixelStats::ensureCIRDataDirectory()
{
    try
    {
        const std::string dirPath = "CIRData";
        if (!std::filesystem::exists(dirPath))
        {
            if (std::filesystem::create_directories(dirPath))
            {
                logInfo("PixelStats: Created CIRData directory");
            }
            else
            {
                logError("PixelStats: Failed to create CIRData directory");
                return false;
            }
        }
        return true;
    }
    catch (const std::exception& e)
    {
        logError("PixelStats: Error creating CIRData directory: {}", e.what());
        return false;
    }
}
```

#### 时间戳生成
```cpp
std::string PixelStats::generateTimestampedFilename(CIRExportFormat format)
{
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = *std::localtime(&time_t_now);
    
    // Generate timestamp string
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
    std::string timestamp = oss.str();
    
    // Get file extension based on format
    std::string extension;
    switch (format)
    {
        case CIRExportFormat::CSV:
            extension = ".csv";
            break;
        case CIRExportFormat::JSONL:
            extension = ".jsonl";
            break;
        case CIRExportFormat::TXT:
        default:
            extension = ".txt";
            break;
    }
    
    return "CIRData_" + timestamp + extension;
}
```

生成的文件名格式：`CIRData_YYYYMMDD_HHMMSS.ext`
例如：`CIRData_20231201_143052.csv`

## 代码修改详情

### 修改的文件

1. **Source/Falcor/Rendering/Utils/PixelStats.h**
   - 添加`CIRExportFormat`枚举
   - 添加新的导出方法声明
   - 添加导出格式配置成员变量
   - 添加helper方法声明

2. **Source/Falcor/Rendering/Utils/PixelStats.cpp**
   - 添加必要的头文件（`<chrono>`, `<filesystem>`, `<iomanip>`, `<sstream>`）
   - 修改UI，添加格式选择和新的导出按钮
   - 实现新的导出方法和helper方法

### 新增的方法

1. **主要导出方法**：
   - `exportCIRDataWithTimestamp()` - 自动时间戳导出
   - `exportCIRDataWithFormat()` - 指定格式导出

2. **Helper方法**：
   - `generateTimestampedFilename()` - 生成时间戳文件名
   - `ensureCIRDataDirectory()` - 确保目录存在
   - `exportCIRDataCSV()` - CSV格式导出
   - `exportCIRDataJSONL()` - JSONL格式导出
   - `exportCIRDataTXT()` - TXT格式导出

## 优势和特点

### 1. Excel兼容性
- CSV格式可以直接在Excel中打开
- 列标题清晰，便于数据分析
- 保持了静态参数信息

### 2. 数据处理友好
- JSONL格式便于编程处理
- 结构化数据，易于解析
- 支持流式处理大文件

### 3. 向后兼容
- 保留了原始TXT格式
- 原有的导出功能仍然可用
- 新功能作为增强选项

### 4. 用户体验
- 自动时间戳避免文件覆盖
- 专用目录组织更整洁
- UI界面直观易用

### 5. 技术特点
- 使用现代C++特性（`std::filesystem`, `std::chrono`）
- 完善的错误处理和日志记录
- 模块化设计，易于维护和扩展

## 使用方法

1. **启用CIR数据收集**：在UI中选择"Raw Data"或"Both"模式
2. **选择导出格式**：从下拉菜单中选择CSV、JSONL或TXT
3. **导出数据**：
   - 点击"Export CIR Data (Auto-timestamped)"获得自动命名的文件
   - 点击"Export CIR Data (Original)"使用传统方式

## 输出文件位置

所有CIR数据文件保存在：`./CIRData/` 目录下

示例文件名：
- `CIRData_20231201_143052.csv`
- `CIRData_20231201_143052.jsonl`
- `CIRData_20231201_143052.txt`

## 总结

这次功能增强成功实现了用户的所有需求，提供了更加专业和便于使用的CIR数据导出功能。新的格式支持和自动化特性大大提升了数据分析的效率，同时保持了与现有系统的完全兼容性。 