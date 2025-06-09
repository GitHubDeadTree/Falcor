# CosTheta修复说明

## 问题背景

在IncomingLightPowerPass渲染通道中，入射光线功率的计算公式为：

**功率(Power) = 辐射度(Radiance) × 有效面积(Effective Area) × cos(θ)**

其中cos(θ)代表光线入射角的余弦值，之前的实现中使用了固定值1.0，忽略了入射角度对功率的影响，导致计算结果不准确。

## 修复内容

### 1. 恢复正确的CosTheta计算

之前的代码使用了固定值1.0作为cosTheta，现在已修复为使用实际的入射角计算：

```cpp
// 之前的实现 - 使用固定值
float computeCosTheta(float3 rayDir)
{
    // 不再使用实际计算的角度，而是返回固定值1.0
    // 这会使所有的光线计算忽略入射角的影响
    return 1.0f;
}

// 修复后的实现 - 使用正确的角度计算
float computeCosTheta(float3 rayDir, out CosThetaDebugInfo debugInfo)
{
    // Calculate camera normal (forward direction)
    float3 cameraNormal = normalize(gCameraTarget - gCameraPosition);

    // For rays entering the camera, we want to use the camera forward direction directly
    // In Falcor, camera looks along its forward direction, so rays entering the camera
    // should have a negative dot product with the camera forward direction
    float dotProduct = dot(rayDir, -cameraNormal);

    // Clamp to non-negative (protect against back-facing rays)
    float cosTheta = max(0.0f, dotProduct);

    // Use a more reasonable minimum value (0.01 instead of 0.00001)
    // This prevents power from being too close to zero for glancing rays
    const float minCosTheta = 0.01f;
    float finalCosTheta = max(cosTheta, minCosTheta);

    // Store debug information
    debugInfo.cameraNormal = cameraNormal;
    debugInfo.rayDirection = rayDir;
    debugInfo.dotProduct = dotProduct;
    debugInfo.cosTheta = cosTheta;
    debugInfo.minValue = minCosTheta;

    return finalCosTheta;
}
```

### 2. 添加调试结构体

为了便于调试和观察计算过程，添加了专门的调试结构体：

```cpp
// Define a debug structure to store cosTheta computation details
struct CosThetaDebugInfo
{
    float3 cameraNormal;     // Camera normal vector
    float3 rayDirection;     // Ray direction vector
    float dotProduct;        // Dot product between ray direction and inverse camera normal
    float cosTheta;          // Final cosTheta value
    float minValue;          // Minimum cosTheta value used
};
```

### 3. 提供两个版本的计算函数

为了同时支持带调试信息和不带调试信息的调用方式，提供了两个版本的函数：

```cpp
// 带调试信息的版本
float computeCosTheta(float3 rayDir, out CosThetaDebugInfo debugInfo);

// 不带调试信息的简化版本
float computeCosTheta(float3 rayDir)
{
    // Temporary debug info that will be discarded
    CosThetaDebugInfo tempDebugInfo;
    return computeCosTheta(rayDir, tempDebugInfo);
}
```

### 4. 调试输出增强

增强了调试输出，为前2×2个像素提供详细的计算数据：

```cpp
// Debug Input Data: Store ray direction and camera normal
gDebugInputData[pixel] = float4(rayDir.xyz, rayLengthVal);

// Debug Output: Store camera normal vector and wavelength
gDebugOutput[pixel] = float4(cosThetaDebug.cameraNormal, wavelength);

// Debug Calculation: Store cosTheta calculation details
// x: dot product, y: cosTheta before min clamp, z: final cosTheta, w: min value
gDebugCalculation[pixel] = float4(
    cosThetaDebug.dotProduct,           // Raw dot product
    cosThetaDebug.cosTheta,             // Before min clamp
    cosTheta,                           // Final value
    cosThetaDebug.minValue              // Min threshold used
);
```

### 5. 向量可视化

添加了一个向量可视化函数，用于直观地显示相机法向量和光线方向的关系：

```cpp
// Function to convert vectors to RGB colors for visualization
float4 visualizeVectors(float3 rayDir, float3 cameraNormal, float cosTheta)
{
    // Convert camera normal to a blueish color
    float3 normalColor = float3(0.2, 0.4, 1.0) * (cameraNormal * 0.5 + 0.5);

    // Convert ray direction to a reddish color
    float3 rayColor = float3(1.0, 0.3, 0.2) * (rayDir * 0.5 + 0.5);

    // Mix colors based on cosTheta
    // When cosTheta is close to 1, the ray direction is close to opposite of camera normal
    // so the color should be more intense (good alignment)
    float alignment = saturate(cosTheta);
    float3 finalColor = lerp(normalColor * 0.5, rayColor, alignment);

    // Return color with cosTheta as alpha
    return float4(finalColor, cosTheta);
}
```

对于左上角的第一个像素(0,0)，使用此函数生成一个可视化结果：
- 蓝色表示相机法向量
- 红色表示光线方向
- 颜色混合程度表示两者的对齐程度（CosTheta值）

```cpp
// For first pixel, store a visualization of vectors and their alignment
if (pixel.x == 0 && pixel.y == 0) {
    // Create a visualization that shows the camera normal and ray direction
    gDebugOutput[pixel] = visualizeVectors(rayDir, cosThetaDebug.cameraNormal, cosTheta);
}
```

## 调试信息说明

修复后的代码在渲染前2×2个像素时，会输出以下调试信息：

1. **gDebugInputData** - 光线方向向量和光线长度
2. **gDebugOutput** - 相机法向量和波长信息（对于像素(0,0)则是向量可视化结果）
3. **gDebugCalculation** - CosTheta的详细计算过程：
   - x: 原始点积值（dot(rayDir, -cameraNormal)）
   - y: 限制为非负后的值（max(dotProduct, 0)）
   - z: 最终使用的CosTheta值
   - w: 使用的最小CosTheta阈值（0.01）

## 正确的计算方法

CosTheta的正确计算流程：

1. 计算相机法向量：`cameraNormal = normalize(gCameraTarget - gCameraPosition)`
2. 对于进入相机的光线，需要计算与相机法向量反方向的点积：`dotProduct = dot(rayDir, -cameraNormal)`
3. 确保结果为非负值（防止背面光线）：`cosTheta = max(0.0f, dotProduct)`
4. 应用最小阈值（防止掠射光线功率过小）：`finalCosTheta = max(cosTheta, 0.01f)`

## 测试方法

可以观察调试输出纹理中的数据来验证计算过程是否正确：

1. 检查相机法向量和光线方向是否正确
2. 验证点积计算是否准确
3. 确认最终的CosTheta值是否在合理范围内
4. 观察功率计算结果是否随入射角度变化而变化
5. 检查像素(0,0)的可视化结果，观察向量之间的对齐情况

## 预期效果

修复后，光线功率计算将更加准确：

1. 正面入射的光线（与相机法向量方向相反）将获得最大功率
2. 斜入射的光线功率将减小
3. 掠射光线（入射角接近90度）将获得最小功率（受0.01最小值限制）
4. 背面入射的光线（若有）将被排除（贡献为0）
