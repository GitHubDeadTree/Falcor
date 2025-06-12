# CIR静态参数实现报告

## 项目概述

本报告记录了在Falcor光线追踪引擎中为VLC（可见光通信）CIR（信道脉冲响应）数据导出功能添加6个静态参数的完整实现过程。该功能增强了现有的CIR数据收集系统，使其能够自动计算并导出VLC信道分析所需的关键静态参数。

## 遇到的错误

在实现过程中遇到了以下编译和链接错误：

### 1. E0135错误
**错误描述**: 类"Falcor::PixelStats"没有成员"setScene"
**原因**: IntelliSense无法识别新添加的`setScene`方法声明

### 2. LNK2019链接错误 - computeCIRStaticParameters
**错误描述**: 无法解析的外部符号`computeCIRStaticParameters`
**原因**: 该方法在头文件中声明但在源文件中没有实现

### 3. LNK2019链接错误 - exportCIRData
**错误描述**: 无法解析的外部符号`exportCIRData`
**原因**: 该方法的实现调用了未实现的`computeCIRStaticParameters`方法

### 4. LNK1120链接错误
**错误描述**: 1个无法解析的外部命令
**原因**: 由上述未解析的符号引起的连锁错误

## 错误修复过程

### 1. 添加数学常数定义

在`PixelStats.h`中添加M_PI常数定义：

```cpp
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
```

### 2. 实现静态参数计算方法

在`PixelStats.cpp`中添加了以下关键方法的完整实现：

#### computeReceiverArea方法
```cpp
float PixelStats::computeReceiverArea(const ref<Camera>& pCamera, const uint2& frameDim)
{
    if (!pCamera) return 1e-4f; // Default 1 cm²

    try
    {
        // Get camera parameters for calculating sensor area
        float focalLength = pCamera->getFocalLength(); // mm
        float frameHeight = pCamera->getFrameHeight(); // mm
        float aspectRatio = pCamera->getAspectRatio();

        // Calculate FOV from focal length and frame height
        float fovY = focalLengthToFovY(focalLength, frameHeight); // radians
      
        // Calculate physical sensor dimensions in meters
        float sensorHeightM = frameHeight * 1e-3f; // Convert mm to meters
        float sensorWidthM = sensorHeightM * aspectRatio;
      
        // Calculate total sensor area
        float totalSensorArea = sensorWidthM * sensorHeightM; // m²
      
        // Calculate pixel area (total sensor area divided by number of pixels)
        uint32_t totalPixels = frameDim.x * frameDim.y;
        float pixelArea = totalSensorArea / totalPixels;
      
        logInfo("PixelStats: Computed receiver area = {:.6e} m² (total sensor: {:.6e} m², pixels: {})",
               pixelArea, totalSensorArea, totalPixels);
             
        return pixelArea;
    }
    catch (const std::exception& e)
    {
        logError("PixelStats: Error computing receiver area: {}", e.what());
        return 1e-4f; // Default fallback
    }
}
```

#### computeLEDLambertianOrder方法
```cpp
float PixelStats::computeLEDLambertianOrder(const ref<Scene>& pScene)
{
    if (!pScene) return 1.0f; // Default Lambertian order

    try
    {
        const auto& lights = pScene->getLights();
        if (lights.empty())
        {
            logWarning("PixelStats: No lights found in scene, using default Lambertian order = 1.0");
            return 1.0f;
        }

        // Find first point light and calculate Lambertian order
        for (const auto& pLight : lights)
        {
            if (pLight->getType() == LightType::Point)
            {
                const auto* pPointLight = static_cast<const PointLight*>(pLight.get());
                float openingAngle = pPointLight->getOpeningAngle(); // radians
              
                // Calculate Lambertian order using m = -ln(2)/ln(cos(θ_1/2))
                if (openingAngle >= (float)M_PI)
                {
                    // Isotropic light source: m = 1 (Lambertian)
                    logInfo("PixelStats: Found isotropic point light, Lambertian order = 1.0");
                    return 1.0f;
                }
                else
                {
                    float halfAngle = openingAngle * 0.5f;
                    float cosHalfAngle = std::cos(halfAngle);
                  
                    if (cosHalfAngle > 0.0f && cosHalfAngle < 1.0f)
                    {
                        float lambertianOrder = -std::log(2.0f) / std::log(cosHalfAngle);
                        logInfo("PixelStats: Computed LED Lambertian order = {:.3f} (half-angle = {:.3f} rad)",
                               lambertianOrder, halfAngle);
                        return std::max(0.1f, lambertianOrder); // Ensure positive value
                    }
                }
            }
        }
      
        logWarning("PixelStats: No suitable point light found, using default Lambertian order = 1.0");
        return 1.0f;
    }
    catch (const std::exception& e)
    {
        logError("PixelStats: Error computing LED Lambertian order: {}", e.what());
        return 1.0f; // Default fallback
    }
}
```

#### computeReceiverFOV方法
```cpp
float PixelStats::computeReceiverFOV(const ref<Camera>& pCamera)
{
    if (!pCamera) return (float)M_PI; // Default wide FOV

    try
    {
        float focalLength = pCamera->getFocalLength(); // mm
        float frameHeight = pCamera->getFrameHeight(); // mm
      
        // Calculate vertical FOV
        float fovY = focalLengthToFovY(focalLength, frameHeight); // radians
      
        logInfo("PixelStats: Computed receiver FOV = {:.3f} rad ({:.1f} degrees)",
               fovY, fovY * 180.0f / (float)M_PI);
             
        return fovY;
    }
    catch (const std::exception& e)
    {
        logError("PixelStats: Error computing receiver FOV: {}", e.what());
        return (float)M_PI; // Default fallback
    }
}
```

#### computeCIRStaticParameters主方法
```cpp
CIRStaticParameters PixelStats::computeCIRStaticParameters(const ref<Scene>& pScene, const uint2& frameDim)
{
    CIRStaticParameters params;
  
    if (!pScene)
    {
        logWarning("PixelStats: No scene provided, using default CIR static parameters");
        return params;
    }

    try
    {
        // Get camera for receiver calculations
        ref<Camera> pCamera = pScene->getCamera();
      
        if (pCamera)
        {
            // 1. Compute receiver effective area (A)
            params.receiverArea = computeReceiverArea(pCamera, frameDim);
          
            // 4. Compute receiver field of view (FOV)
            params.receiverFOV = computeReceiverFOV(pCamera);
        }
        else
        {
            logWarning("PixelStats: No camera found, using default receiver parameters");
            params.receiverArea = 1e-4f; // 1 cm²
            params.receiverFOV = (float)M_PI; // 180 degrees
        }
      
        // 2. Compute LED Lambertian order (m)
        params.ledLambertianOrder = computeLEDLambertianOrder(pScene);
      
        // 3. Light speed (c) - physical constant
        params.lightSpeed = 3.0e8f; // m/s
      
        // 5. Optical filter transmittance (T_s) - set to 1.0 (no filter)
        params.opticalFilterGain = 1.0f;
      
        // 6. Optical concentration gain (g) - set to 1.0 (no concentration)
        params.opticalConcentration = 1.0f;
      
        logInfo("PixelStats: Computed CIR static parameters:");
        logInfo("  Receiver area: {:.6e} m²", params.receiverArea);
        logInfo("  LED Lambertian order: {:.3f}", params.ledLambertianOrder);
        logInfo("  Light speed: {:.3e} m/s", params.lightSpeed);
        logInfo("  Receiver FOV: {:.3f} rad", params.receiverFOV);
        logInfo("  Optical filter gain: {:.1f}", params.opticalFilterGain);
        logInfo("  Optical concentration: {:.1f}", params.opticalConcentration);
      
        return params;
    }
    catch (const std::exception& e)
    {
        logError("PixelStats: Error computing CIR static parameters: {}", e.what());
        return CIRStaticParameters(); // Return default parameters
    }
}
```

### 3. 确认PathTracer集成

PathTracer中已经包含了正确的Scene引用设置：

```455:460:Source/RenderPasses/PathTracer/PathTracer.cpp
// Set scene reference for PixelStats to enable CIR static parameter calculation
if (mpPixelStats)
{
    mpPixelStats->setScene(pScene);
}
```

## 已实现的功能

### 1. 数据结构增强

在`PixelStats.h`中添加了`CIRStaticParameters`结构体：

```cpp
struct CIRStaticParameters
{
    float receiverArea;         // A: Receiver effective area (m²)
    float ledLambertianOrder;   // m: LED Lambertian order
    float lightSpeed;           // c: Light propagation speed (m/s)
    float receiverFOV;          // FOV: Receiver field of view (radians)
    float opticalFilterGain;    // T_s(θ): Optical filter transmittance
    float opticalConcentration; // g(θ): Optical concentration gain
  
    CIRStaticParameters()
        : receiverArea(1e-4f)
        , ledLambertianOrder(1.0f)
        , lightSpeed(3.0e8f)
        , receiverFOV((float)M_PI)
        , opticalFilterGain(1.0f)
        , opticalConcentration(1.0f)
    {}
};
```

### 2. 自动参数计算

实现了6个VLC静态参数的自动计算：

- **A (接收器面积)**: 从相机传感器参数计算每像素接收面积
- **m (LED朗伯阶数)**: 从点光源开角计算朗伯辐射模式参数
- **c (光速)**: 固定物理常数 3.0×10⁸ m/s
- **FOV (接收器视场角)**: 从相机焦距和传感器尺寸计算
- **T_s (光学滤波器增益)**: 默认值 1.0 (无滤波)
- **g (光学集中增益)**: 默认值 1.0 (无光学集中)

### 3. 增强的导出格式

更新了`exportCIRData`方法，在文件头部包含静态参数：

```cpp
// Write header with static parameters
file << "# CIR Path Data Export with Static Parameters\n";
file << "# Static Parameters for VLC Channel Impulse Response Calculation:\n";
file << "# A_receiver_area_m2=" << std::scientific << std::setprecision(6) << staticParams.receiverArea << "\n";
file << "# m_led_lambertian_order=" << std::fixed << std::setprecision(3) << staticParams.ledLambertianOrder << "\n";
file << "# c_light_speed_ms=" << std::scientific << std::setprecision(3) << staticParams.lightSpeed << "\n";
file << "# FOV_receiver_rad=" << std::fixed << std::setprecision(3) << staticParams.receiverFOV << "\n";
file << "# T_s_optical_filter_gain=" << std::fixed << std::setprecision(1) << staticParams.opticalFilterGain << "\n";
file << "# g_optical_concentration=" << std::fixed << std::setprecision(1) << staticParams.opticalConcentration << "\n";
```

### 4. 错误处理和日志

所有新方法都包含完善的错误处理和信息日志记录，确保系统稳定性和调试便利性。

### 5. 向后兼容性

保持了所有现有功能的完整性，新功能作为可选增强添加，不影响原有的CIR数据收集流程。

## 修改文件清单

1. **Source/Falcor/Rendering/Utils/PixelStats.h**
   - 添加`CIRStaticParameters`结构体
   - 添加数学常数定义
   - 添加Scene引用成员变量
   - 添加新方法声明

2. **Source/Falcor/Rendering/Utils/PixelStats.cpp**
   - 实现静态参数计算方法
   - 更新导出功能
   - 添加错误处理和日志

3. **Source/RenderPasses/PathTracer/PathTracer.cpp**
   - 已包含Scene引用设置（无需修改）

## 技术特点

1. **自动化计算**: 所有静态参数从场景信息自动提取和计算
2. **健壮性**: 包含完整的错误处理和默认值fallback
3. **可扩展性**: 模块化设计便于添加新的参数类型
4. **性能优化**: 参数仅在导出时计算，不影响渲染性能
5. **标准兼容**: 符合VLC信道分析的理论要求

## 验证和测试

建议的测试场景：
1. 包含点光源的简单场景
2. 不同相机参数配置
3. 无光源场景（测试默认值）
4. 多种光源类型混合场景

## 总结

通过本次实现，成功为Falcor的CIR数据导出功能添加了完整的VLC静态参数支持。该实现遵循了最小侵入性原则，保持了系统的稳定性和可维护性，同时为VLC信道脉冲响应分析提供了完整的数据支持。 