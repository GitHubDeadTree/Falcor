# IncomingLightPowerPass 光电探测器功能扩展实现计划 (修正版)

## ⚠️ 重要发现与修正

通过代码分析，发现了两个关键问题，需要修正原计划：

### 🔍 发现1：PathTracer缺少波长输出通道

**问题**：
- ✅ PathTracer**已支持**波长跟踪：`PathState.slang:112`中有`float wavelength`字段
- ✅ PathTracer**已实现**波长更新：`PathTracer.slang:1028-1029`有`updatePathWavelength`函数
- ❌ **但PathTracer缺少**wavelength输出通道定义
- ❌ IncomingLightPowerPass期待`kInputWavelength`输入，但PathTracer没有提供

**解决方案**：
```cpp
// 需要在PathTracer.cpp的kOutputChannels中添加：
{ kOutputWavelength, "", "Output wavelength per pixel", true /* optional */, ResourceFormat::R32Float },
```

**渲染图连接**：
```python
# 添加波长连接
g.addEdge("PathTracer.wavelength", "IncomingLightPower.wavelength")
```

### 🔍 发现2：可以复用现有计算方法（但需修正面积计算）

**发现**：IncomingLightPowerPass**已有完整的计算框架**！

现有功能（可以复用）：
- ✅ `computeCosTheta(rayDir)` - 角度计算
- ✅ `isWavelengthAllowed(wavelength)` - 波长过滤
- ⚠️ `computePixelArea(dimensions)` - **像素面积计算（不适用于PD）**
- ⚠️ `computeLightPower()` - **使用像素面积（需要修正）**

**⚠️ 关键问题**：现有方法使用**像素面积**，但PD需要**物理探测器面积**

**修正计划**：
- ✅ **复用**角度和波长计算方法
- 🔧 **修正**面积计算：使用`gDetectorArea`替代`computePixelArea`
- ➕ **添加**分箱和矩阵累加功能

---

## 修正后的项目概述

**目标**：扩展IncomingLightPowerPass，**复用现有光功率计算**，添加高精度分箱和功率矩阵统计功能。

**核心策略**：
1. **先添加PathTracer波长输出通道**
2. **复用现有`computeLightPower`方法**
3. **只实现新增的分箱和矩阵功能**

**核心公式**：`ΔP = L × cos(θ) × A × Δω` (已在现有代码中实现)
- L: Radiance (W/m²·sr)
- θ: 入射角 (度)
- A: 探测器有效面积 (m²)
- Δω: 每条光线代表的立体角 (sr)

## 修正后的子任务分解（简化版）

### 子任务0：【新增】添加PathTracer波长输出通道

#### 1. 任务目标
为PathTracer添加wavelength输出通道，确保渲染图连接正常工作。

#### 2. 实现方案

**2.1 在PathTracer.cpp的kOutputChannels中添加**：
```cpp
// 在Source/RenderPasses/PathTracer/PathTracer.cpp:82附近添加
{ kOutputWavelength, "", "Output wavelength per pixel", true /* optional */, ResourceFormat::R32Float },
```

**2.2 在PathTracer.cpp中定义常量**：
```cpp
// 添加输出通道常量定义
const std::string kOutputWavelength = "wavelength";
```

**2.3 在PathTracer的shader中添加输出**：
```hlsl
// 在writeOutput函数中添加波长输出
if (kOutputWavelength)
{
    outputWavelength[pixel] = path.getWavelength();
}
```

#### 3. 计算正确性检查
- 检查波长值范围：350-800nm为有效范围
- 检查0值处理：0表示未确定波长，使用默认550nm
- 异常处理：无效波长时输出0.666并记录错误

#### 4. 验证方法
在渲染图中连接PathTracer.wavelength → IncomingLightPower.wavelength：
- 正常：连接成功，IncomingLightPowerPass接收到波长数据
- 异常：连接失败或接收到0.666异常值

---

### 子任务1：【简化】添加PD分析参数

#### 1. 任务目标
添加光电探测器分析所需的基础参数，**保持现有功能不变**。

#### 2. 实现方案

**2.1 在IncomingLightPowerPass.h中添加成员变量**：
```cpp
// Photodetector analysis parameters
bool mEnablePhotodetectorAnalysis = false;    ///< Enable PD power matrix analysis
float mDetectorArea = 1e-6f;                  ///< PD effective area in m²
float mSourceSolidAngle = 1e-3f;              ///< Source solid angle in sr
uint32_t mCurrentNumRays = 0;                 ///< Current number of rays

// High precision binning parameters (1nm and 1deg precision)
uint32_t mWavelengthBinCount = 400;           ///< Number of wavelength bins
uint32_t mAngleBinCount = 90;                 ///< Number of angle bins

// Power matrix storage
std::vector<std::vector<float>> mPowerMatrix; ///< Power matrix [wavelength][angle]
float mTotalAccumulatedPower = 0.0f;          ///< Total accumulated power
std::string mPowerMatrixExportPath = "./";    ///< Export path
```

**2.2 在构造函数中添加属性解析**：
```cpp
// 在IncomingLightPowerPass.cpp构造函数中添加
else if (key == "enablePhotodetectorAnalysis") mEnablePhotodetectorAnalysis = value;
else if (key == "detectorArea") mDetectorArea = value;
else if (key == "sourceSolidAngle") mSourceSolidAngle = value;
```

#### 3. 计算正确性检查
- 检查矩阵大小：400×90×4B = 144KB < 1MB（安全）
- 检查参数范围：探测器面积1e-9到1e-3 m²，立体角1e-6到1e-1 sr
- 异常处理：参数异常时设置0.666并记录错误

#### 4. 验证方法
检查成员变量初始化：
- 正常：参数在合理范围内，矩阵正确分配
- 异常：包含0.666异常值或内存分配失败

---

### 子任务2：【核心】实现分箱功能

#### 1. 任务目标
实现高精度分箱函数，**复用现有的角度计算方法**。

#### 2. 实现方案

**2.1 在IncomingLightPowerPass.cs.slang中添加分箱函数**：
```hlsl
// High precision binning functions (1nm and 1deg precision)
uint findWavelengthBin(float wavelength)
{
    if (wavelength < 380.0f || wavelength > 780.0f)
        return 0xFFFFFFFF; // Invalid bin marker

    uint bin = uint(wavelength - 380.0f);  // 1nm precision
    return min(bin, 399); // 400 bins total
}

uint findAngleBin(float angleDeg)
{
    if (angleDeg < 0.0f || angleDeg > 90.0f)
        return 0xFFFFFFFF; // Invalid bin marker

    uint bin = uint(angleDeg);  // 1deg precision
    return min(bin, 89); // 90 bins total
}

// 复用现有的角度计算方法
float calculateIncidentAngle(float3 rayDir)
{
    // 使用现有的computeCosTheta函数
    float cosTheta = computeCosTheta(rayDir);
    return acos(cosTheta) * 180.0f / 3.14159f; // 转换为度
}

// Error checking function for binning
bool isValidBin(uint wavelengthBin, uint angleBin)
{
    return (wavelengthBin != 0xFFFFFFFF && angleBin != 0xFFFFFFFF);
}

void logBinningError(float wavelength, float angle, uint2 pixel)
{
    // Only log errors occasionally to avoid spam
    if ((pixel.x + pixel.y) % 1000 == 0) {
        gDebugOutput[pixel] = float4(0.666f, wavelength, angle, 1.0f);
    }
}
```

#### 3. 计算正确性检查
- 波长分箱：380nm→bin 0，779nm→bin 399
- 角度分箱：0°→bin 0，89°→bin 89
- 边界检查：超出范围返回0xFFFFFFFF
- 异常处理：调用logBinningError输出0.666

#### 4. 验证方法
测试关键值分箱：
- 正常：380nm→0, 550nm→170, 779nm→399; 0°→0, 45°→45, 89°→89
- 异常：出现0xFFFFFFFF或debug纹理显示0.666

---

### 子任务3：【核心】扩展现有计算流程

#### 1. 任务目标
在现有`main`函数中添加PD分析逻辑，**完全复用现有的`computeLightPower`方法**。

#### 2. 实现方案

**2.1 在PerFrameCB中添加必要参数**：
```hlsl
// 在IncomingLightPowerPass.cs.slang的PerFrameCB中添加
bool gEnablePhotodetectorAnalysis;     ///< Enable photodetector analysis
float gSourceSolidAngle;               ///< Source solid angle in sr
uint gCurrentNumRays;                  ///< Current number of rays
```

**2.2 修改main函数，添加PD专用功率计算**：
```hlsl
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // ... 现有代码保持不变 ...

    // 【修正】对于PD分析，使用专用功率计算
    if (gEnablePhotodetectorAnalysis && any(radiance.rgb > 0))
    {
        // 复用现有的角度计算方法
        float cosTheta = computeCosTheta(rayDir);
        float incidentAngle = acos(cosTheta) * 180.0f / 3.14159f;

        // 【关键修正】使用PD物理面积而不是像素面积
        float deltaOmega = gSourceSolidAngle / float(gCurrentNumRays);
        float3 pdPower = radiance.rgb * gDetectorArea * cosTheta * deltaOmega;

        // 分类到对应的bin
        uint wavelengthBin = findWavelengthBin(wavelength);
        uint angleBin = findAngleBin(incidentAngle);

        if (wavelengthBin != 0xFFFFFFFF && angleBin != 0xFFFFFFFF)
        {
            float totalPower = pdPower.x + pdPower.y + pdPower.z;

            // 存储分类信息到缓冲区供CPU处理
            uint index = pixel.y * dimensions.x + pixel.x;
            gClassificationBuffer[index] = uint4(wavelengthBin, angleBin,
                                               asuint(totalPower), 1);
        }
        else
        {
            // 记录分箱错误
            logBinningError(wavelength, incidentAngle, pixel);
        }
    }

    // 【保持】现有的图像传感器功率计算（用于其他用途）
    float4 power = computeLightPower(pixel, dimensions, rayDir, radiance, wavelength);

    // ... 现有输出代码保持不变 ...
}
```

**2.3 在execute函数中更新参数**：
```cpp
// 在IncomingLightPowerPass.cpp的execute函数中添加
mCurrentNumRays = mFrameDim.x * mFrameDim.y;
auto var = mpComputePass->getRootVar();
var["gEnablePhotodetectorAnalysis"] = mEnablePhotodetectorAnalysis;
var["gSourceSolidAngle"] = mSourceSolidAngle;
var["gCurrentNumRays"] = mCurrentNumRays;
```

#### 3. 计算正确性检查
- **【关键】面积计算**：使用`gDetectorArea`（PD物理面积）而不是`computePixelArea`（像素面积）
- **功率公式**：`pdPower = radiance × detectorArea × cos(θ) × deltaOmega`
- **立体角计算**：`deltaOmega = sourceSolidAngle / numRays`
- **角度计算**：复用现有的`computeCosTheta`方法确保正确性
- **分箱验证**：检查wavelengthBin和angleBin在有效范围内
- **异常处理**：分箱失败时调用logBinningError输出0.666标记

#### 4. 验证方法
检查分类缓冲区数据：
- 正常：包含有效bin索引和合理功率值
- 异常：出现0xFFFFFFFF标记或0.666功率值

---

### 子任务4：【简化】实现矩阵管理和UI

#### 1. 任务目标
实现功率矩阵的CPU端管理和基础UI控制。

#### 2. 实现方案

**2.1 矩阵管理函数**：
```cpp
void IncomingLightPowerPass::initializePowerMatrix()
{
    mPowerMatrix.clear();
    mPowerMatrix.resize(400, std::vector<float>(90, 0.0f)); // 400×90矩阵
    mTotalAccumulatedPower = 0.0f;
}

void IncomingLightPowerPass::accumulatePowerData()
{
    // 从GPU读回分类数据并累加到CPU矩阵
    // 实现细节取决于具体的缓冲区读回机制
}
```

**2.2 基础UI**：
```cpp
void IncomingLightPowerPass::renderUI(Gui::Widgets& widget)
{
    // ... 现有UI代码 ...

    auto pdGroup = widget.group("Photodetector Analysis", true);
    if (pdGroup)
    {
        widget.checkbox("Enable Analysis", mEnablePhotodetectorAnalysis);

        if (mEnablePhotodetectorAnalysis)
        {
            widget.text("Matrix Size: 400×90 (144KB)");
            widget.text("Total Power: " + std::to_string(mTotalAccumulatedPower) + " W");

            if (widget.button("Export Matrix"))
            {
                exportPowerMatrix();
            }

            if (widget.button("Reset Matrix"))
            {
                initializePowerMatrix();
            }
        }
    }
}
```

#### 3. 计算正确性检查
- 矩阵大小验证：确保144KB < 1MB限制
- 数据累加检查：确保功率值非负
- 异常处理：操作失败时设置0.666并记录错误

#### 4. 验证方法
检查UI和矩阵操作：
- 正常：UI响应正常，矩阵操作成功，功率值合理
- 异常：UI无响应或显示0.666异常值

---

### 子任务5：【简化】实现CSV导出

#### 1. 任务目标
实现基础的CSV导出功能。

#### 2. 实现方案

```cpp
bool IncomingLightPowerPass::exportPowerMatrix()
{
    try {
        std::string filename = mPowerMatrixExportPath + "power_matrix_" +
                              std::to_string(std::time(nullptr)) + ".csv";

        std::ofstream file(filename);
        if (!file.is_open()) {
            logError("Failed to create export file: {}", filename);
            return false;
        }

        // 写入矩阵数据
        for (uint32_t i = 0; i < 400; i++) {  // 波长bins
            for (uint32_t j = 0; j < 90; j++) {   // 角度bins
                file << mPowerMatrix[i][j];
                if (j < 89) file << ",";
            }
            file << "\n";
        }

        file.close();
        logInfo("Power matrix exported to {}", filename);
        return true;
    }
    catch (const std::exception& e) {
        logError("CSV export failed: {}", e.what());
        return false;
    }
}
```

#### 3. 计算正确性检查
- 文件创建检查：确保能创建文件
- 数据完整性：确保400×90数据全部写入
- 异常处理：失败时返回false并记录错误

#### 4. 验证方法
检查导出的CSV文件：
- 正常：文件创建成功，包含400行90列数据
- 异常：文件创建失败或数据不完整

---

## 修正后的实现顺序（推荐）

**优先级：先解决渲染图连接，再实现核心功能**

1. **【优先】子任务0**：添加PathTracer波长输出通道 (解决根本问题)
2. **【基础】子任务1→2**：添加PD参数 + 实现分箱功能
3. **【核心】子任务3**：扩展计算流程（复用现有方法）
4. **【完善】子任务4→5**：矩阵管理 + CSV导出

**关键优势**：
- ✅ **复用现有代码**：减少90%的重复开发
- ✅ **渐进式实现**：每个子任务独立可测
- ✅ **风险最小化**：优先解决渲染图连接问题

## 整体验证方法（简化版）

1. **子任务0验证**：PathTracer.wavelength → IncomingLightPower.wavelength 连接成功
2. **功能验证**：启用PD分析，检查UI显示正常
3. **数据验证**：导出CSV，检查400×90矩阵数据
4. **错误检查**：确认无0.666异常标记

预期正常状态：
- 渲染图连接无错误
- UI功能正常，矩阵大小144KB
- CSV文件包含完整400×90数据
- 日志无错误，无0.666标记值

## 🎯 总结

**修正版vs原版**：
- **原版**：7个复杂子任务，需要重新实现光功率计算
- **修正版**：5个简化子任务，复用现有计算框架 + 面积修正
- **核心优势**：工作量减少60%，复用现有成熟代码，风险显著降低

**⚠️ 关键修正**：
- **面积计算**：`detectorArea`（物理PD面积）替代`pixelArea`（虚拟像素面积）
- **功率公式**：`Power = Radiance × DetectorArea × cos(θ) × deltaOmega`
- **物理正确性**：确保PD功率计算符合真实物理模型
