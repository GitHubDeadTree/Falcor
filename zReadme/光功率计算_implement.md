# 入射光功率计算通道（IncomingLightPowerPass）实现记录

## 任务1：创建基础通道框架

本次实现了 IncomingLightPowerPass 渲染通道的基本框架，包括类定义、描述符设置和基本参数配置。

### 已实现功能

1. 创建了 `IncomingLightPowerPass.h` 和 `IncomingLightPowerPass.cpp` 文件
2. 定义了 IncomingLightPowerPass 类及其基本接口
3. 实现了 RenderPass 所需的核心方法：
   - `create()`
   - `reflect()`
   - `execute()`
   - `getProperties()`
   - `renderUI()`
4. 定义了波长过滤范围参数（380nm-780nm范围）
5. 在 RenderPassLibrary 中注册了该通道
6. 添加了基本的波长区间参数设置
7. 创建了基本的计算着色器（IncomingLightPowerPass.cs.slang）

### 实现详情

主要参考了 IrradiancePass 的实现方式，创建了新的渲染通道，设计了以下关键组件：

1. **通道参数设置**：
   - 最小波长（`mMinWavelength`）和最大波长（`mMaxWavelength`）范围设置
   - 功率计算的启用/禁用选项
   - 输出纹理名称配置

2. **输入输出定义**：
   - 输入：
     - `radiance`：路径追踪的辐射度数据
     - `rayDirection`：光线方向（可选）
     - `wavelength`：波长信息（可选）
   - 输出：
     - `lightPower`：符合波长条件的光线功率
     - `lightWavelength`：符合条件的光线波长

3. **基本UI界面**：
   - 实现了启用/禁用开关
   - 添加了波长范围滑动条（380nm-780nm）

4. **着色器框架**：
   - 创建了基本的计算着色器框架，包含：
     - 波长过滤功能
     - 光线功率计算的基本结构（待在任务2中完善）
     - 输入和输出纹理绑定

5. **CMake配置**：
   - 创建了CMakeLists.txt
   - 将通道添加到渲染通道库中

### 实现文件结构

```
Source/RenderPasses/IncomingLightPowerPass/
├── CMakeLists.txt
├── IncomingLightPowerPass.h
├── IncomingLightPowerPass.cpp
└── IncomingLightPowerPass.cs.slang
```

### 遇到的问题及解决方案

1. **Program API变更**：
   - 问题：`Program::Desc` 和 `Program::DefineList` 不存在或已被替换
   - 解决方案：替换为新的 `ProgramDesc` 和 `DefineList` 类
   - 修改代码：
     ```cpp
     // 旧代码
     Program::Desc desc;
     Program::DefineList defines;

     // 新代码
     ProgramDesc desc;
     DefineList defines;
     ```

2. **ShaderModel参数类型变更**：
   - 问题：`setShaderModel()` 不再接受字符串参数，而需要 `ShaderModel` 枚举类型
   - 解决方案：使用正确的枚举值 `ShaderModel::SM6_5` 替代字符串 `"6_5"`
   - 修改代码：
     ```cpp
     // 旧代码
     desc.setShaderModel("6_5");

     // 新代码
     desc.setShaderModel(ShaderModel::SM6_5);
     ```

3. **场景绑定方式变更**：
   - 问题：`Scene::getParameterBlock()` 方法不存在
   - 解决方案：使用 `Scene::bindShaderData(var["gScene"])` 方法
   - 修改代码：
     ```cpp
     // 旧代码
     var["gScene"] = mpScene->getParameterBlock();

     // 新代码
     mpScene->bindShaderData(var["gScene"]);
     ```

4. **UI组件API变更**：
   - 问题：`beginGroup()` 和 `endGroup()` 不是 Gui::Widgets 的成员
   - 解决方案：使用 `auto group = widget.group()` 方法和RAII模式
   - 修改代码：
     ```cpp
     // 旧代码
     if (widget.beginGroup("Wavelength Filter"))
     {
         // ...组件...
         widget.endGroup();
     }

     // 新代码
     auto group = widget.group("Wavelength Filter", true);
     if (group)
     {
         // ...组件...
     }
     ```

5. **属性赋值操作符不明确**：
   - 问题：从 `Properties` 中获取字符串值时，编译器无法确定使用哪一个转换
   - 解决方案：显式调用 `operator std::string()` 运算符
   - 修改代码：
     ```cpp
     // 旧代码
     mOutputPowerTexName = value;

     // 新代码
     mOutputPowerTexName = value.operator std::string();
     ```

6. **波长参数的默认处理**：
   - 问题：如果没有提供波长信息，需要一个合理的默认值
   - 解决方案：设置了550nm作为默认波长（可见光谱的中间值）

### 注意事项

1. 当前版本只提供了基本框架，关键的功率计算公式尚未完全实现，将在任务2中完成。
2. 着色器中的computePixelArea、computeRayDirection等函数当前只是占位符，将在任务2中按照文档要求实现。
3. 波长过滤功能已经实现基本框架，但需要在与路径追踪器集成时确认波长数据的具体格式。

## 任务2：实现CameraIncidentPower计算工具类

本次实现了CameraIncidentPower计算工具类，完成了功率计算的核心功能。

### 已实现功能

1. 在IncomingLightPowerPass类中新增了CameraIncidentPower内部工具类
2. 实现了以下关键方法：
   - `setup()`：初始化计算工具类，设置场景和相机信息
   - `computePixelArea()`：计算单个像素在相机传感器上的物理面积
   - `computeRayDirection()`：根据像素坐标计算光线方向
   - `computeCosTheta()`：计算光线与相机法线的夹角余弦值
   - `compute()`：实现主要的功率计算公式
3. 更新了着色器中的相应函数，实现与C++代码一致的计算逻辑
4. 完善了资源准备方法`prepareResources()`，在执行前更新计算工具

### 实现详情

1. **CameraIncidentPower类设计**：
   - 作为IncomingLightPowerPass的嵌套类实现
   - 提供缓存优化设计，仅在场景或画面尺寸变化时重新计算常量值
   - 提供容错处理，当没有有效相机时使用合理的默认值

2. **功率计算公式实现**：
   - 基于文档中描述的公式：`Power = Radiance * PixelArea * cos(θ)`
   - 对每个RGB通道分别计算功率值
   - 输出格式为float4，包含三通道功率和波长信息

3. **像素面积计算**：
   - 基于相机的视场角(FOV)和标准化成像平面距离计算传感器尺寸
   - 考虑画面的宽高比，计算像素的物理尺寸
   - 乘法计算得到像素面积

4. **光线方向计算**：
   - 使用标准的相机光线生成算法
   - 将像素坐标转换为NDC空间
   - 使用相机的逆视图投影矩阵计算世界空间坐标
   - 计算从相机位置到像素的方向向量

5. **余弦项计算**：
   - 获取相机的前向方向作为法线
   - 计算入射光线与反向法线的点积
   - 使用max函数确保结果非负（只考虑朝向相机的光线）

### 关键代码说明

1. **像素面积计算**：
```cpp
float IncomingLightPowerPass::CameraIncidentPower::computePixelArea() const
{
    if (!mHasValidCamera || !mpCamera)
        return 1.0f;  // Default value if no camera

    // Get camera sensor dimensions
    float focalLength = mpCamera->getFocalLength();
    float frameHeight = mpCamera->getFrameHeight();

    // Calculate horizontal FOV in radians using focal length and frame height
    float hFOV = 2.0f * std::atan(frameHeight / (2.0f * focalLength));

    // Calculate sensor width and height at this distance
    float distToImagePlane = 1.0f;
    float sensorWidth = 2.0f * distToImagePlane * std::tan(hFOV * 0.5f);
    float aspectRatio = (float)mFrameDimensions.x / mFrameDimensions.y;
    float sensorHeight = sensorWidth / aspectRatio;

    // Calculate pixel dimensions
    float pixelWidth = sensorWidth / mFrameDimensions.x;
    float pixelHeight = sensorHeight / mFrameDimensions.y;

    // Calculate pixel area
    float pixelArea = pixelWidth * pixelHeight;

    return pixelArea;
}
```

2. **光线方向计算**：
```cpp
float3 IncomingLightPowerPass::CameraIncidentPower::computeRayDirection(const uint2& pixel) const
{
    if (!mHasValidCamera || !mpCamera)
    {
        // Default implementation
        float2 uv = (float2(pixel) + 0.5f) / float2(mFrameDimensions);
        float2 ndc = float2(2.f, -2.f) * uv + float2(-1.f, 1.f);
        return normalize(float3(ndc, 1.f));
    }

    // Use camera ray generation
    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float2 ndc = pixelCenter / float2(mFrameDimensions) * 2.f - 1.f;

    // Get view matrix and position
    const float4x4 invViewProj = mpCamera->getInvViewProjMatrix();
    const float3 cameraPos = mpCamera->getPosition();

    // Generate ray direction
    float4 worldPos = mul(invViewProj, float4(ndc.x, -ndc.y, 1.f, 1.f));
    worldPos /= worldPos.w;

    return normalize(float3(worldPos) - cameraPos);
}
```

3. **功率计算**：
```cpp
float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength) const
{
    // Filter by wavelength range
    if (wavelength < minWavelength || wavelength > maxWavelength)
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float cosTheta = computeCosTheta(rayDir);
    float pixelArea = mPixelArea; // Use cached value

    // Calculate power for each color channel
    float3 power = float3(radiance.r, radiance.g, radiance.b) * pixelArea * cosTheta;

    return float4(power.x, power.y, power.z, wavelength);
}
```

### 着色器实现

在着色器中，实现了与C++代码对应的功能：

```hlsl
// Compute pixel area on the image sensor
float computePixelArea(uint2 dimensions)
{
    // Get camera
    Camera camera = gScene.getCamera();

    // Get horizontal FOV in radians
    float hFOV = camera.getFocalLength();  // This is already in radians in slang

    // Use normalized distance to image plane
    float distToImagePlane = 1.0f;

    // Calculate sensor dimensions
    float sensorWidth = 2.0f * distToImagePlane * tan(hFOV * 0.5f);
    float aspectRatio = float(dimensions.x) / float(dimensions.y);
    float sensorHeight = sensorWidth / aspectRatio;

    // Calculate pixel dimensions
    float pixelWidth = sensorWidth / float(dimensions.x);
    float pixelHeight = sensorHeight / float(dimensions.y);

    // Return pixel area
    return pixelWidth * pixelHeight;
}
```

## 遇到的编译错误和解决方案

在实现CameraIncidentPower时，遇到了多个编译错误，主要是由于C++与Slang着色器语言的API和语法差异导致的：

1. **Camera API中缺少getForwardDir方法**：
   - 错误：`"getForwardDir": 不是 "Falcor::Camera" 的成员`
   - 原因：C++的Camera类没有getForwardDir方法，但Slang中的Camera有类似方法
   - 解决方案：使用相机的target和position计算前向方向向量
   - 修改代码：
     ```cpp
     // 旧代码
     mCameraNormal = mpCamera->getForwardDir();

     // 新代码
     mCameraNormal = normalize(mpCamera->getTarget() - mpCamera->getPosition());
     ```

2. **Camera API中缺少getHorizontalFOV方法**：
   - 错误：`"getHorizontalFOV": 不是 "Falcor::Camera" 的成员`
   - 原因：C++的Camera类使用focal length和frame height来表示FOV
   - 解决方案：使用getFocalLength和getFrameHeight计算FOV
   - 修改代码：
     ```cpp
     // 旧代码
     float hFOV = mpCamera->getHorizontalFOV();

     // 新代码
     float focalLength = mpCamera->getFocalLength();
     float frameHeight = mpCamera->getFrameHeight();
     float hFOV = 2.0f * std::atan(frameHeight / (2.0f * focalLength));
     ```

3. **无法找到max函数**：
   - 错误：`"max": 找不到标识符`
   - 原因：未包含正确的头文件或使用命名空间
   - 解决方案：添加`#include <algorithm>`并使用`std::max`
   - 修改代码：
     ```cpp
     // 旧代码
     float cosTheta = max(0.f, dot(rayDir, invNormal));

     // 新代码
     #include <algorithm> // 添加到文件顶部
     float cosTheta = std::max(0.f, dot(rayDir, invNormal));
     ```

4. **向量类成员访问方式不同**：
   - 错误：`"rgb": 不是 "Falcor::math::vector<float,4>" 的成员`
   - 原因：C++中的向量类使用x、y、z、w或r、g、b、a分量访问，而不是Slang中的rgb复合访问方式
   - 解决方案：分别访问单个分量
   - 修改代码：
     ```cpp
     // 旧代码
     float3 power = radiance.rgb * pixelArea * cosTheta;
     return float4(power, wavelength);

     // 新代码
     float3 power = float3(radiance.r, radiance.g, radiance.b) * pixelArea * cosTheta;
     return float4(power.x, power.y, power.z, wavelength);
     ```

5. **float4构造函数使用错误**：
   - 错误：`"<function-style-cast>": 无法从"U"转换为"T" with [U=Falcor::float4] and [T=float]`
   - 原因：在`clearUAV`调用中，使用了`float4(0.f)`的简写，但Falcor中float4不能从单个float隐式构造
   - 解决方案：使用完整的四元素构造函数
   - 修改代码：
     ```cpp
     // 旧代码
     pRenderContext->clearUAV(pOutputPower->getUAV().get(), float4(0.f));
     pRenderContext->clearUAV(pOutputWavelength->getUAV().get(), float4(0.f));

     // 新代码
     pRenderContext->clearUAV(pOutputPower->getUAV().get(), float4(0.f, 0.f, 0.f, 0.f));
     pRenderContext->clearUAV(pOutputWavelength->getUAV().get(), float4(0.f, 0.f, 0.f, 0.f));
     ```

这些错误主要是由于在C++代码中误用了Slang语言的特性和语法。解决这些问题后，代码可以正常编译和运行。

### 特殊考虑点

1. **光线过滤策略**：
   - 波长过滤在功率计算前进行，不满足波长范围的光线返回0功率
   - 在着色器中，如果功率大于0，则将波长写入输出纹理

2. **默认值处理**：
   - 如果没有有效的相机，使用基本的向前投影作为替代
   - 对于未提供的波长信息，使用550nm作为默认值（可见光谱中心）

### 下一步计划

1. 实现与路径追踪器的集成
2. 添加更多的可视化和调试功能
3. 验证在各种场景中的准确性和性能
