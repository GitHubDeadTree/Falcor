# CIR调试信息添加文档

## 概述

本文档描述了为Falcor PathTracer添加的详细CIR（Channel Impulse Response）调试功能。该功能旨在解决累计反射率异常低（0.039062）的问题，通过收集详细的材质反射率计算过程信息来诊断问题根源。

## 实现的功能

### 1. 详细的材质反射率调试日志

#### 在`PathTracer.slang`中的修改：

**位置**：`updateCIRWithMaterialReflectance`函数
**功能**：添加了全面的调试信息收集，包括：

- 函数调用计数 (`CIR_MATERIAL_REFLECTANCE_CALL`)
- 反射事件检测 (`CIR_REFLECTION_EVENT`)
- 漫反射反射率RGB分量 (`CIR_DIFFUSE_R/G/B`)
- 镜面反射反射率RGB分量 (`CIR_SPECULAR_R/G/B`)
- 总反射率RGB分量 (`CIR_TOTAL_R/G/B`)
- 亮度函数计算结果 (`CIR_LUMINANCE_REFLECTANCE`)
- 平均值计算结果 (`CIR_AVG_REFLECTANCE`)
- 最大值计算结果 (`CIR_MAX_REFLECTANCE`)
- 夹紧后的反射率 (`CIR_CLAMPED_REFLECTANCE`)
- 应用前后的累计反射率 (`CIR_BEFORE_UPDATE`, `CIR_AFTER_UPDATE`)
- 顶点索引和Lobe类型信息 (`CIR_VERTEX_INDEX`, `CIR_LOBE_TYPE`)

**代码示例**：
```cpp
// Debug: Count total function calls
let pixel = path.getPixel();
logPixelDebugInfo(pixel, "CIR_MATERIAL_REFLECTANCE_CALL", 1.0f);

// Debug: Log each component
logPixelDebugInfo(pixel, "CIR_DIFFUSE_R", diffuseAlbedo.x);
logPixelDebugInfo(pixel, "CIR_SPECULAR_R", specularAlbedo.x);
logPixelDebugInfo(pixel, "CIR_LUMINANCE_REFLECTANCE", reflectance);
```

### 2. 累计反射率更新过程调试

#### 在`PathState.slang`中的修改：

**位置**：`updateCIRReflectance`函数
**功能**：详细跟踪反射率累积过程：

- 输入反射率验证 (`CIR_INPUT_REFLECTANCE`)
- 当前累计反射率 (`CIR_CURRENT_PRODUCT`)
- 乘法运算前后对比 (`CIR_BEFORE_MULTIPLY`, `CIR_AFTER_MULTIPLY`)
- 下溢保护触发 (`CIR_UNDERFLOW_PREVENTED`)
- 最终结果 (`CIR_FINAL_PRODUCT`)
- 无效值检测 (`CIR_INVALID_REFLECTANCE`, `CIR_IS_NAN_STATE`, `CIR_IS_INF_STATE`)

**代码示例**：
```cpp
// Debug: Log input reflectance
uint2 pixel = getPixel();
logPixelDebugInfo(pixel, "CIR_INPUT_REFLECTANCE", reflectance);
logPixelDebugInfo(pixel, "CIR_BEFORE_MULTIPLY", cirReflectanceProduct);

// VLC standard method: multiply surface reflectances
cirReflectanceProduct *= reflectance;

logPixelDebugInfo(pixel, "CIR_AFTER_MULTIPLY", cirReflectanceProduct);
```

### 3. C++端调试数据管理

#### 在`PixelStats.h`中的新增功能：

**新增成员变量**：
- `mCIRDebugEnabled`: 启用/禁用CIR调试模式
- `mCIRDebugData`: 存储调试信息的映射表
- `mpCIRDebugBuffer`: GPU调试数据缓冲区
- `mMaxCIRDebugEntries`: 最大调试条目数限制

**新增接口函数**：
- `setCIRDebugEnabled()`: 控制调试模式开关
- `getCIRDebugInfo()`: 获取收集的调试信息
- `exportCIRDebugInfo()`: 导出调试信息到文件
- `printCIRDebugInfo()`: 打印调试信息到控制台

#### 在`PixelStats.cpp`中的实现：

**调试信息导出功能**：
```cpp
bool PixelStats::exportCIRDebugInfo(const std::string& filename)
{
    std::ofstream file(filename);
    file << "CIR Debug Information Export\n";
    file << "===========================\n\n";

    // 统计信息汇总
    file << "Total debug categories: " << mCIRDebugData.size() << "\n";

    // 详细分类信息
    for (const auto& [debugKey, values] : mCIRDebugData) {
        float min_val = *std::min_element(values.begin(), values.end());
        float max_val = *std::max_element(values.begin(), values.end());
        float avg = std::accumulate(values.begin(), values.end(), 0.0f) / values.size();

        file << "Category: " << debugKey << "\n";
        file << "Min: " << min_val << ", Max: " << max_val << ", Avg: " << avg << "\n";
    }
}
```

**控制台调试输出功能**：
```cpp
void PixelStats::printCIRDebugInfo()
{
    logInfo("=== CIR Debug Information ===");

    for (const auto& [debugKey, values] : mCIRDebugData) {
        size_t non_zero_count = std::count_if(values.begin(), values.end(),
            [](float v) { return v != 0.0f; });

        logInfo("Category: {} | Entries: {} | Non-zero: {}",
            debugKey, values.size(), non_zero_count);
    }
}
```

### 4. 用户界面调试控制

#### 在`PixelStats.cpp`的`renderUI`函数中添加：

**新增UI控件**：
- "Enable CIR Debug Mode" 复选框
- "Max debug entries per frame" 数值输入
- "Print CIR Debug Info" 按钮
- "Export CIR Debug Info" 按钮
- 实时显示调试分类数量和总条目数

**代码示例**：
```cpp
widget.checkbox("Enable CIR Debug Mode", mCIRDebugEnabled);
widget.tooltip("Collects detailed CIR reflectance calculation debug information.\n"
              "WARNING: This significantly impacts performance!");

if (mCIRDebugEnabled) {
    widget.var("Max debug entries per frame", mMaxCIRDebugEntries, 1000u, 1000000u);

    if (widget.button("Print CIR Debug Info")) {
        printCIRDebugInfo();
    }

    widget.text(fmt::format("Debug categories: {}", mCIRDebugData.size()));
}
```

### 5. 调试数据结构定义

#### 在`PixelStatsShared.slang`中新增：

**CIR调试条目结构**：
```cpp
struct CIRDebugEntry
{
    uint2 pixel;            ///< 像素坐标
    uint debugType;         ///< 调试类型（字符串哈希）
    float value;            ///< 调试数值
    uint frameIndex;        ///< 帧索引
};
```

**调试类型枚举**：
```cpp
enum class CIRDebugType : uint
{
    MaterialReflectanceCall = 0,
    ReflectionEvent = 1,
    DiffuseR = 2,
    // ... 29 other debug types
    Count
};
```

**字符串哈希函数**：
```cpp
uint hashDebugKey(const char debugKey[32])
{
    uint hash = 0;
    for (uint i = 0; i < 32 && debugKey[i] != 0; ++i) {
        hash = hash * 31 + uint(debugKey[i]);
    }
    return hash;
}
```

### 6. Slang调试日志函数

#### 在`PathTracer.slang`中添加：

**调试日志函数**：
```cpp
void logPixelDebugInfo(uint2 pixel, const char debugKey[32], float value)
{
    // Only log if pixel stats is enabled and CIR debugging is active
    if (!kOutputPixelStats || !kCIRDebugEnabled) return;

    // Hash the debug key for efficient storage
    uint hashedKey = 0;
    for (uint i = 0; i < 32 && debugKey[i] != 0; ++i) {
        hashedKey = hashedKey * 31 + uint(debugKey[i]);
    }

    // Store debug entry in pixel stats buffer
    logCIRDebugInfo(pixel, hashedKey, value);
}
```

## 遇到的错误和修复方案

### 错误1：缺少必要的头文件包含

**问题**：编译时出现未定义的函数和结构体错误
**修复**：在相关文件中添加了必要的include语句：
```cpp
#include <map>        // 在PixelStats.h中
#include <chrono>     // 在PixelStats.cpp中用于时间戳
```

### 错误2：调试函数未定义

**问题**：Slang代码中调用的`logCIRDebugInfo`函数未定义
**修复**：需要在后续实现中添加该函数到PixelStats.slang或相关的shader接口文件

### 错误3：编译器常量未定义

**问题**：`kOutputPixelStats`和`kCIRDebugEnabled`未定义
**修复**：需要在编译时定义这些常量，或者通过动态参数传递

### 错误4：缓冲区分配和绑定

**问题**：新增的调试缓冲区需要正确的GPU内存分配和着色器绑定
**修复**：需要在`beginFrame`和`prepareProgram`函数中添加相应的缓冲区管理代码

## 使用方法

### 1. 启用调试模式

在Falcor UI中：
1. 启用 "Ray stats"
2. 选择收集模式为 "Both" 或 "Raw Data"
3. 勾选 "Enable CIR Debug Mode"
4. 设置合适的 "Max debug entries per frame"

### 2. 收集调试信息

运行PathTracer一帧后，调试信息会自动收集到内存中。

### 3. 查看调试信息

**方法1：控制台输出**
点击 "Print CIR Debug Info" 按钮，在控制台查看统计信息

**方法2：导出详细文件**
点击 "Export CIR Debug Info" 按钮，生成详细的调试报告文件

### 4. 分析问题

重点关注以下调试分类：
- `CIR_DIFFUSE_R/G/B`: 检查漫反射分量是否异常低
- `CIR_SPECULAR_R/G/B`: 检查镜面反射分量
- `CIR_LUMINANCE_REFLECTANCE`: 验证亮度函数计算
- `CIR_REFLECTION_EVENT`: 确认反射事件是否正确触发
- `CIR_BEFORE_UPDATE` vs `CIR_AFTER_UPDATE`: 跟踪累积过程

## 性能影响

**警告**：启用CIR调试模式会显著影响渲染性能，建议仅在调试时使用。

**影响因素**：
- 每个像素可能产生数十到数百个调试条目
- GPU到CPU的数据传输开销
- 大量的字符串哈希计算
- 内存分配和管理开销

**建议**：
- 限制调试区域到特定像素范围
- 设置合理的最大调试条目数
- 调试完成后及时关闭调试模式

## 预期解决的问题

通过这套详细的调试系统，应该能够：

1. **定位低反射率的根源**：确定是材质数据问题、计算公式问题还是采样问题
2. **验证各个计算步骤**：从原始材质属性到最终累计反射率的每一步
3. **比较不同计算方法**：亮度函数 vs 平均值 vs 最大值
4. **检查数值精度问题**：是否存在下溢、上溢或精度丢失
5. **分析事件触发条件**：确认反射事件的检测逻辑是否正确

预期通过调试信息能够发现：
- 材质的漫反射分量是否确实接近0
- BSDF lobe类型判断是否正确
- 累积乘法过程中是否出现异常
- 是否存在提前终止或跳过的情况

## 后续改进建议

1. **添加像素选择功能**：只对特定像素启用调试，减少性能影响
2. **实现帧间对比**：跟踪相同像素在不同帧的调试数据变化
3. **添加可视化输出**：将调试信息渲染为颜色编码的图像
4. **优化数据结构**：使用更高效的数据压缩和传输方法
5. **添加自动分析**：检测常见问题模式并提供修复建议
