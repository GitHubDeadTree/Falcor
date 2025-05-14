# 光线追踪中的波长与强度记录实现

## 1. 概述

本次修改主要针对Falcor渲染引擎中的Path Tracer组件进行了扩展，实现了光线波长和强度的记录功能。修改遵循以下原则：
- 仅修改PathTracer相关代码，不影响其他组件
- 为SpectrumUtils添加了RGB到波长转换的实用工具函数
- 在PathState中添加了波长字段和相关方法
- 在PathPayload中添加了携带波长数据的额外字段
- 在PathTracer中实现了波长的更新和跟踪逻辑

## 2. 实现细节

### 2.1 RGB到波长转换函数

在`Source/Falcor/Utils/Color/SpectrumUtils.h`中添加了函数声明：

```cpp
/**
 * Converts RGB color to dominant wavelength and purity.
 * Based on CIE 1931 chromaticity diagram for physical accuracy.
 * @param[in] rgb RGB color value, range [0,1].
 * @param[out] purity Returns the color's purity value (0-1).
 * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
 */
static float RGBtoDominantWavelength(const float3& rgb, float& purity);

/**
 * Simplified version that converts RGB color to dominant wavelength without returning purity.
 * @param[in] rgb RGB color value, range [0,1].
 * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
 */
static float RGBtoDominantWavelength(const float3& rgb);
```

在`Source/Falcor/Utils/Color/SpectrumUtils.cpp`中添加了具体实现：

```cpp
// CIE 1931 standard observer spectral locus points
// Wavelength range: 380-700nm, step 5nm
static const std::vector<float2> spectralLocus = {
    {0.1741f, 0.0050f}, // 380nm
    {0.1740f, 0.0050f}, // 385nm
    {0.1738f, 0.0049f}, // 390nm
    {0.1736f, 0.0049f}, // 395nm
    {0.1733f, 0.0048f}, // 400nm
    // ... (省略中间数据)
    {0.7346f, 0.2654f}, // 695nm
    {0.7347f, 0.2653f}  // 700nm
};

// D65 white point
static const float2 whitePoint = {0.3127f, 0.3290f};

float SpectrumUtils::RGBtoDominantWavelength(const float3& rgb, float& purity)
{
    // Default output
    purity = 0.0f;

    // Normalize RGB values
    float r = std::max(0.0f, rgb.x);
    float g = std::max(0.0f, rgb.y);
    float b = std::max(0.0f, rgb.z);

    // If color is too dark, return 0 (cannot determine wavelength)
    if (r < 0.01f && g < 0.01f && b < 0.01f)
        return 0.0f;

    // Convert to XYZ color space (using sRGB to XYZ matrix)
    float X = 0.4124f * r + 0.3576f * g + 0.1805f * b;
    float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float Z = 0.0193f * r + 0.1192f * g + 0.9505f * b;

    float sum = X + Y + Z;

    // Prevent division by zero and convert to CIE xy chromaticity coordinates
    if (sum < 1e-6f)
        return 0.0f;

    float x = X / sum;
    float y = Y / sum;

    // Calculate vector from white point to current color
    float2 colorVec = {x - whitePoint.x, y - whitePoint.y};
    float colorVecLen = std::sqrt(colorVec.x * colorVec.x + colorVec.y * colorVec.y);

    // If color is too close to white point, cannot determine dominant wavelength
    if (colorVecLen < 1e-6f)
        return 0.0f;

    // Normalize vector
    colorVec.x /= colorVecLen;
    colorVec.y /= colorVecLen;

    // Find intersection with spectral locus
    float minWavelength = 380.0f;
    float wavelengthStep = 5.0f;
    float bestWavelength = 0.0f;
    float bestDotProduct = -1.0f;
    bool isComplement = false;

    for (size_t i = 0; i < spectralLocus.size() - 1; i++) {
        // Calculate vector from white point to spectral locus point
        float2 spectralVec = {
            spectralLocus[i].x - whitePoint.x,
            spectralLocus[i].y - whitePoint.y
        };

        float spectralVecLen = std::sqrt(spectralVec.x * spectralVec.x + spectralVec.y * spectralVec.y);
        if (spectralVecLen < 1e-6f) continue;

        // Normalize spectral vector
        spectralVec.x /= spectralVecLen;
        spectralVec.y /= spectralVecLen;

        // Calculate dot product between vectors
        float dotProduct = colorVec.x * spectralVec.x + colorVec.y * spectralVec.y;

        // If dot product is greater than previous best, update result
        if (std::abs(dotProduct) > std::abs(bestDotProduct)) {
            bestDotProduct = dotProduct;
            bestWavelength = minWavelength + i * wavelengthStep;
            isComplement = (dotProduct < 0);
        }
    }

    // Calculate purity
    // Find closest point on spectral locus
    int index = (int)((bestWavelength - minWavelength) / wavelengthStep);
    index = std::min(std::max(0, index), (int)spectralLocus.size() - 1);

    float2 spectralPoint = spectralLocus[index];

    // Calculate distance from white point to spectral point
    float dws = std::sqrt(
        (spectralPoint.x - whitePoint.x) * (spectralPoint.x - whitePoint.x) +
        (spectralPoint.y - whitePoint.y) * (spectralPoint.y - whitePoint.y)
    );

    // Calculate distance from white point to chromaticity point
    float dc = std::sqrt(
        (x - whitePoint.x) * (x - whitePoint.x) +
        (y - whitePoint.y) * (y - whitePoint.y)
    );

    // Calculate purity = distance to white point / distance to spectral locus
    purity = dc / dws;
    purity = std::min(std::max(0.0f, purity), 1.0f);

    // Return negative wavelength for complement colors
    return isComplement ? -bestWavelength : bestWavelength;
}

// Simplified version without purity
float SpectrumUtils::RGBtoDominantWavelength(const float3& rgb)
{
    float purity;
    return RGBtoDominantWavelength(rgb, purity);
}
```

在`Source/Falcor/Utils/Color/SpectrumUtils.slang`中添加了着色器可访问的版本：

```slang
// CIE 1931 standard observer spectral locus points
// Wavelength range: 380-700nm, step 5nm
static const float2 spectralLocus[] = {
    float2(0.1741f, 0.0050f), // 380nm
    float2(0.1740f, 0.0050f), // 385nm
    float2(0.1738f, 0.0049f), // 390nm
    // ... (省略中间数据)
    float2(0.7346f, 0.2654f), // 695nm
    float2(0.7347f, 0.2653f)  // 700nm
};

// D65 white point
static const float2 whitePoint = float2(0.3127f, 0.3290f);

/**
 * Converts RGB color to dominant wavelength and purity.
 * Based on CIE 1931 chromaticity diagram for physical accuracy.
 * @param[in] rgb RGB color value, range [0,1].
 * @param[out] purity Returns the color's purity value (0-1).
 * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
 */
static float RGBtoDominantWavelength(float3 rgb, out float purity)
{
    // Default output
    purity = 0.0f;

    // Normalize RGB values
    rgb = max(0.0f, rgb);

    // If color is too dark, return 0 (cannot determine wavelength)
    if (all(rgb < 0.01f))
        return 0.0f;

    // Convert to XYZ color space (using sRGB to XYZ matrix)
    float X = 0.4124f * rgb.r + 0.3576f * rgb.g + 0.1805f * rgb.b;
    float Y = 0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b;
    float Z = 0.0193f * rgb.r + 0.1192f * rgb.g + 0.9505f * rgb.b;

    float sum = X + Y + Z;

    // Prevent division by zero and convert to CIE xy chromaticity coordinates
    if (sum < 1e-6f)
        return 0.0f;

    float x = X / sum;
    float y = Y / sum;

    // Calculate vector from white point to current color
    float2 colorVec = float2(x - whitePoint.x, y - whitePoint.y);
    float colorVecLen = length(colorVec);

    // If color is too close to white point, cannot determine dominant wavelength
    if (colorVecLen < 1e-6f)
        return 0.0f;

    // Normalize vector
    colorVec /= colorVecLen;

    // Find intersection with spectral locus
    float minWavelength = 380.0f;
    float wavelengthStep = 5.0f;
    float bestWavelength = 0.0f;
    float bestDotProduct = -1.0f;
    bool isComplement = false;

    for (int i = 0; i < spectralLocus.Length - 1; i++) {
        // Calculate vector from white point to spectral locus point
        float2 spectralVec = float2(
            spectralLocus[i].x - whitePoint.x,
            spectralLocus[i].y - whitePoint.y
        );

        float spectralVecLen = length(spectralVec);
        if (spectralVecLen < 1e-6f) continue;

        // Normalize spectral vector
        spectralVec /= spectralVecLen;

        // Calculate dot product between vectors
        float dotProduct = dot(colorVec, spectralVec);

        // If dot product is greater than previous best, update result
        if (abs(dotProduct) > abs(bestDotProduct)) {
            bestDotProduct = dotProduct;
            bestWavelength = minWavelength + i * wavelengthStep;
            isComplement = (dotProduct < 0);
        }
    }

    // Calculate purity
    // Find closest point on spectral locus
    int index = int((bestWavelength - minWavelength) / wavelengthStep);
    index = clamp(index, 0, spectralLocus.Length - 1);

    float2 spectralPoint = spectralLocus[index];

    // Calculate distance from white point to spectral point
    float dws = length(spectralPoint - whitePoint);

    // Calculate distance from white point to chromaticity point
    float dc = length(float2(x, y) - whitePoint);

    // Calculate purity = distance to white point / distance to spectral locus
    purity = dc / dws;
    purity = clamp(purity, 0.0f, 1.0f);

    // Return negative wavelength for complement colors
    return isComplement ? -bestWavelength : bestWavelength;
}

/**
 * Simplified version that converts RGB color to dominant wavelength without returning purity.
 * @param[in] rgb RGB color value, range [0,1].
 * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
 */
static float RGBtoDominantWavelength(float3 rgb)
{
    float purity;
    return RGBtoDominantWavelength(rgb, purity);
}

/**
 * Gets the effective wavelength value, handling complement colors.
 * @param[in] dominantWavelength Potentially negative dominant wavelength
 * @return Effective wavelength value, converting complement colors to actual wavelength
 */
static float getEffectiveWavelength(float dominantWavelength)
{
    // For negative values (complement colors), use absolute value as effective wavelength
    if (dominantWavelength < 0.0f)
        return abs(dominantWavelength);
    else
        return dominantWavelength;
}

/**
 * Calculates light intensity from RGB color.
 * @param[in] rgb RGB color value.
 * @return Luminance value representing light intensity.
 */
static float calculateLightIntensity(float3 rgb)
{
    // Use standard luminance calculation (Rec. 709 coefficients)
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}
```

### 2.2 修改PathState结构体

在`Source/RenderPasses/PathTracer/PathState.slang`中添加了波长字段和相关方法：

```slang
struct PathState
{
    // ... 现有字段 ...
    float       wavelength;             ///< Light wavelength in nm (0 = not determined or white light)

    // ... 现有方法 ...

    // Wavelength and intensity methods
    [mutating] void setWavelength(float lambda) { wavelength = lambda; }
    float getWavelength() { return wavelength; }

    // Calculate light intensity based on luminance
    float getLightIntensity()
    {
        return luminance(L);
    }

    // ... 剩余现有方法 ...
}
```

### 2.3 修改PathPayload结构体

在`Source/RenderPasses/PathTracer/TracePass.rt.slang`中更新了PathPayload结构体以包含波长信息：

```slang
struct PathPayload
{
    uint4 packed[5];
    uint  packed_extra;        ///< Additional data: x component stores wavelength

    PackedHitInfo hit;
    GuideData guideData;
    InteriorList interiorList;
    SampleGenerator sg;

    static PathPayload pack(const PathState path)
    {
        PathPayload p = {};

        // ... 现有打包代码 ...

        // Pack additional data
        p.packed_extra = asuint(path.wavelength);

        // ... 现有打包代码 ...

        return p;
    }

    static PathState unpack(const PathPayload p)
    {
        PathState path = {};

        // ... 现有解包代码 ...

        // Unpack additional data
        path.wavelength = asfloat(p.packed_extra);

        // ... 现有解包代码 ...

        return path;
    }
}
```

### 2.4 更新PathTracer

在`Source/RenderPasses/PathTracer/PathTracer.slang`中添加了对SpectrumUtils的导入：

```slang
import Utils.Color.SpectrumUtils;
```

并添加了一个新的方法用于更新路径的波长信息：

```slang
/** Updates path wavelength based on emitted light properties.
    This is called when a path segment contributes emitted light.
*/
static void updatePathWavelength(inout PathState path, const float3 Le)
{
    // Skip if no emission or path already has a valid wavelength
    if (all(Le <= 0.0f) || path.getWavelength() != 0.0f)
        return;

    // Calculate dominant wavelength from emitted light color
    float wavelength = SpectrumUtils::RGBtoDominantWavelength(Le);

    // Only set wavelength if a valid value was determined
    // (RGBtoDominantWavelength returns 0 if color is too dark or too close to white)
    if (wavelength != 0.0f)
    {
        path.setWavelength(wavelength);
    }
}
```

在`handleHit`方法中添加了波长的更新：

```slang
// 在添加发射贡献后更新波长
float3 Le = path.thp * emission;
path.L += Le;

// Update path wavelength based on emission color
updatePathWavelength(path, Le);
```

在`handleMiss`方法中也添加了波长的更新：

```slang
float3 Le = envMapSampler.eval(path.dir);
emitterRadiance = misWeight * Le;
float3 contribution = path.thp * emitterRadiance;
addToPathContribution(path, emitterRadiance);

// Update path wavelength based on environment light
if (path.getWavelength() == 0.0f && any(contribution > 0.0f))
{
    float wavelength = SpectrumUtils::RGBtoDominantWavelength(contribution);
    if (wavelength != 0.0f)
    {
        path.setWavelength(wavelength);
    }
}
```

## 3. 工作原理

### 3.1 波长计算原理

RGB到波长的转换基于CIE 1931色度图，其工作流程如下：

1. 将RGB颜色转换为XYZ颜色空间
2. 计算xy色度坐标
3. 从白点(D65)引射线通过当前色度点，直到与光谱轨迹相交
4. 交点对应的波长即为主波长
5. 对于非光谱颜色（如紫色，在光谱轨迹上没有对应的单一波长），返回其补色波长的负值

算法处理了以下特殊情况：
- 太暗的颜色（RGB接近0）：返回0
- 接近白色的颜色（靠近白点）：返回0
- 非光谱颜色（紫色系列）：返回补色波长的负值

### 3.2 光强计算原理

光强度计算使用了标准的亮度公式（使用Rec.709系数）：

```
intensity = 0.2126*R + 0.7152*G + 0.0722*B
```

这些系数反映了人眼对不同颜色的感知敏感度，绿色权重最高。

### 3.3 波长追踪过程

在路径跟踪过程中，波长信息以以下方式进行跟踪：

1. 路径初始化时，波长为0（未确定）
2. 当路径击中光源或采样环境光时，根据光源的RGB颜色计算主波长
3. 波长信息随着路径在场景中传播而保持不变（一旦确定）
4. 波长和光强数据在PathPayload中打包传递，以便在光线传播过程中保持

## 4. 总结

本次修改为Falcor的Path Tracer添加了波长和光强度的追踪功能，提供了物理上准确的CIE色度学方法，可用于更丰富的光线分析和特效。修改保持了最小侵入性，专注于PathTracer组件，不影响其他部分的功能。

## 5. 故障排除

### 5.1 接口不匹配问题

在实现过程中，我们遇到了PathTracer.slang中的`handleHit`函数与TracePass.rt.slang中的调用不匹配的问题。TracePass.rt.slang中仅使用两个参数调用此函数：

```slang
gPathTracer.handleHit(path, vq);
```

而PathTracer.slang中的`handleHit`函数需要五个参数：

```slang
void handleHit<B : IBSDF, V : IVisibilityQuery>(inout PathState path, const PathVertex pathVertex, const ShadingData sd, B bsdf, V visibilityQuery)
```

为解决此问题，我们添加了一个新的函数重载，该函数只接受两个参数并内部创建所需的数据结构，直接实现了完整的光线处理逻辑，而不是调用原始的`handleHit`函数：

```slang
void handleHit<V : IVisibilityQuery>(inout PathState path, V visibilityQuery)
{
    // 完整实现光线处理逻辑，直接复制并修改原始handleHit函数的代码
    // ...
}
```

最初，我们尝试在简化版的`handleHit`函数中调用完整版的函数，但遇到了泛型类型不匹配的问题：

```
no overload for 'handleHit' applicable to arguments of type (PathState, PathVertex, ShadingData, .This, V)
```

这是因为材料实例`mi`的具体类型无法自动匹配泛型参数`B : IBSDF`。为解决这个问题，我们选择了直接将完整版函数的实现代码复制到简化版函数中，这样就避免了需要调用泛型函数的问题。

## 6. 技术深度分析

### 6.1 光的波长计算机制

**问：如何计算光的波长？**

光的波长计算基于色度学原理，特别是CIE 1931色度图的特性。在物理世界中，可见光的波长范围约为380-700纳米，不同波长的单色光对应于色度图上光谱轨迹的不同点。我们使用以下步骤将RGB颜色转换为主波长：

1. **RGB到XYZ转换**：首先使用标准的sRGB到CIE XYZ转换矩阵将RGB颜色转换到XYZ色彩空间：
   ```
   X = 0.4124*R + 0.3576*G + 0.1805*B
   Y = 0.2126*R + 0.7152*G + 0.0722*B
   Z = 0.0193*R + 0.1192*G + 0.9505*B
   ```

2. **XYZ到xy色度坐标**：计算xy色度坐标（CIE 1931色度图上的点）：
   ```
   x = X/(X+Y+Z)
   y = Y/(X+Y+Z)
   ```

3. **确定主波长**：从标准白点(D65: x=0.3127, y=0.3290)引一条直线穿过计算得到的色度点，延长至与光谱轨迹相交。交点对应的波长即为该颜色的主波长。

4. **处理非光谱颜色**：对于落在紫线（连接光谱两端的直线）下方的颜色，没有对应的单一波长。此时，我们返回其补色波长的负值，以便区分。

5. **计算纯度**：纯度表示颜色离白点有多远（相对于白点到光谱轨迹的距离），计算公式为：
   ```
   purity = distance(xy点, 白点) / distance(光谱点, 白点)
   ```

此方法比简单的直接映射更物理准确，因为它考虑了人眼对不同波长光的感知和CIE标准色度系统。

在代码实现中，我们预存了CIE 1931色度图上光谱轨迹的坐标数据（380-700nm，步长5nm）：
```cpp
static const std::vector<float2> spectralLocus = {
    {0.1741f, 0.0050f}, // 380nm
    {0.1740f, 0.0050f}, // 385nm
    // ...更多数据点
    {0.7347f, 0.2653f}  // 700nm
};
```

这些数据点由CIE发布的官方数据计算得出，代表了单色光在色度图上的精确位置。

### 6.2 光强度计算方法

**问：如何计算光的强度？**

光强度计算基于光度学原理，使用标准的亮度公式：

```cpp
float calculateLightIntensity(float3 rgb)
{
    // 使用Rec.709亮度系数
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}
```

这个公式反映了人眼对不同颜色光的敏感度差异：
- 绿色贡献最大 (0.7152)，因为人眼对绿色最敏感
- 红色次之 (0.2126)
- 蓝色贡献最小 (0.0722)

Rec.709系数是HDTV标准定义的，被广泛接受为计算亮度的标准方法。这种方法比简单地取RGB通道平均值更准确，因为它考虑了人眼的感知特性。

在路径追踪器中，我们通过调用`PathState`结构中的`getLightIntensity`方法来计算光强：

```slang
float getLightIntensity()
{
    return luminance(L);
}
```

这里的`luminance`函数内部使用了相同的Rec.709系数实现。

### 6.3 光学属性计算时机

**问：在什么时候计算光的波长？**

光的波长在以下关键点被计算：

1. **光源发射事件**：当路径追踪器遇到发光表面（emissive surface）时，会根据发射的RGB颜色计算主波长：
   ```slang
   // 在handleHit函数中，处理发光表面时：
   float3 emission = misWeight * bsdfProperties.emission;
   addToPathContribution(path, emission);
   attenuatedEmission = path.thp * emission;

   // 根据发射光颜色更新波长
   updatePathWavelength(path, attenuatedEmission);
   ```

2. **环境光采样**：当光线与任何物体都不相交，采样环境光时：
   ```slang
   // 在handleMiss函数中，处理环境光贡献时：
   if (path.getWavelength() == 0.0f && any(contribution > 0.0f))
   {
       float wavelength = SpectrumUtils::RGBtoDominantWavelength(contribution);
       if (wavelength != 0.0f)
       {
           path.setWavelength(wavelength);
       }
   }
   ```

波长计算的具体实现在`updatePathWavelength`函数中：
```slang
static void updatePathWavelength(inout PathState path, const float3 Le)
{
    // 如果没有发射光或路径已有有效波长，则跳过
    if (all(Le <= 0.0f) || path.getWavelength() != 0.0f)
        return;

    // 计算发射光颜色的主波长
    float wavelength = SpectrumUtils::RGBtoDominantWavelength(Le);

    // 只有确定了有效波长才设置
    if (wavelength != 0.0f)
    {
        path.setWavelength(wavelength);
    }
}
```

波长计算仅在光路中第一次遇到光源时进行，一旦确定波长后，该光路后续传播过程中波长保持不变。这模拟了现实世界中光的单色性质。

**问：在什么时候计算光的强度？**

光强度在以下情况下被计算：

1. **当调用`PathState.getLightIntensity()`方法时**：这个方法计算当前累积路径辐射（path.L）的亮度，作为光强的度量：
   ```slang
   float getLightIntensity()
   {
       return luminance(L);
   }
   ```

2. **路径追踪完成后**：在生成最终图像时，可以通过luminance函数计算每个像素的强度。

与波长不同，强度在光传播过程中会随着反射、吸收等交互而变化，通过路径追踪器中的`path.thp`（throughput，吞吐量）变量跟踪。每次表面交互后，`path.thp`会根据材质的BRDF和其他因素进行更新。

### 6.4 光路跟踪与属性记录

**问：计算的是摄像机发出的每一根光线吗？**

在路径追踪中，我们模拟的是从摄像机到光源的光路（逆向光线传输），而非从光源到摄像机的物理光路。对于每个像素，我们会发射一个或多个样本光线，每个光线构成一条完整的路径。对这些光线，我们都会：

1. **追踪它们的波长**：一旦路径遇到光源（发光表面或环境光），就根据光源颜色计算并记录波长。

2. **跟踪它们的强度变化**：通过路径吞吐量(throughput)记录光线与场景交互导致的能量变化。

在Falcor实现中，每个像素的采样数量由`kSamplesPerPixel`参数控制：
```slang
// 路径追踪器中的像素采样循环
while (samplesRemaining > 0)
{
    samplesRemaining -= 1;
    uint pathID = pixel.x | (pixel.y << 12) | (samplesRemaining << 24);
    tracePath(pathID);
    // ...
}
```

对每条光路，我们不仅追踪它的波长和强度，还跟踪:
- 散射类型（漫反射、镜面反射、透射等）
- 与材质的交互
- 路径长度（弹射次数）
- 累积辐射贡献

### 6.5 数据输出与存储

**问：光的波长和强度数据会被输出到什么地方？**

波长和强度数据主要存储在两个地方：

1. **路径状态(PathState)结构体**：在光线传播过程中，波长作为路径状态的一部分被存储：
   ```slang
   struct PathState
   {
       // ... 其他字段
       float wavelength;  // 光波长(nm)，0表示未确定或白光
       // ... 其他字段
   }
   ```

2. **路径负载(PathPayload)数据**：为了在光线追踪过程中传递波长信息，数据被打包到PathPayload结构：
   ```slang
   struct PathPayload
   {
       uint4 packed[5];
       uint packed_extra;  // 额外数据：x分量存储波长
       // ... 其他字段
   }
   ```

3. **每像素样本输出**：最终的渲染结果（包括波长计算后的颜色）被写入到输出缓冲区：
   ```slang
   if (kSamplesPerPixel == 1)
   {
       // 直接写入帧缓冲
       outputColor[pixel] = float4(path.L, 1.f);
   }
   else
   {
       // 写入每样本缓冲
       sampleColor[outIdx].set(path.L);
   }
   ```

然而，当前实现还没有专门的机制将波长和强度数据作为独立通道输出。若要提取这些数据，需要进一步扩展代码，例如添加专用的输出缓冲区：
```slang
// 潜在的扩展
RWStructuredBuffer<float> sampleWavelength;     // 每样本波长输出
RWStructuredBuffer<float> sampleIntensity;      // 每样本强度输出
```

### 6.6 方法局限性分析

**问：这种方法有什么局限性？**

当前实现的主要局限性包括：

1. **波长精度限制**：
   - **离散采样问题**：我们使用预定义的CIE光谱轨迹点（380-700nm，步长5nm），限制了波长计算的精度。
   - **非光谱颜色处理**：对于非光谱颜色（如紫色），返回补色波长的负值，这是一种近似处理，不能完全表示这些颜色的光谱特性。
   - **波长单一化简化**：真实光源通常发射多个波长的光，而我们只记录单一的主波长，这是一种简化。

2. **物理模拟限制**：
   - **波长不影响传播**：在真实世界中，不同波长的光在材质中传播特性不同（如色散），但我们的实现中波长不影响光线传播行为。
   - **缺乏波动光学特性**：没有模拟干涉、衍射等波动光学现象。
   - **忽略偏振**：没有考虑光的偏振特性，这对某些材质（如金属）的反射特性有影响。

3. **计算与存储限制**：
   - **额外计算开销**：RGB到波长的转换增加了计算负担。
   - **存储开销**：需要额外存储波长数据。
   - **光谱表示单一**：使用单一波长值表示光谱分布是一种很大的简化。

4. **精度和覆盖范围问题**：
   - **白色和近白色处理**：接近白点的颜色难以确定主波长，当前实现返回0，可能导致数据不连续。
   - **暗色处理**：对于非常暗的颜色（RGB接近0），无法可靠地确定波长，同样返回0。

5. **算法选择限制**：
   - **RGB到光谱转换不唯一**：不同的光谱分布可能产生相同的RGB值，我们的算法只能提供一种可能的解释。
   - **色度空间局限**：基于CIE 1931色度图，存在已知的感知均匀性问题。

6. **离线渲染限制**：
   - **缺乏交互性**：波长数据当前只在内部使用，没有实时可视化机制。
   - **缺乏专用的分析工具**：没有内置工具来分析和可视化波长分布。

改进方向可包括：
- 实现基于波长的色散效果
- 添加光谱渲染模式，直接以波长模拟光传播
- 增加波长数据的专用输出通道
- 实现更复杂的光谱表示，如使用多个波长样本或光谱曲线

## 7. 总结

本次实现为Falcor的路径追踪器添加了光波长和强度的记录功能，使用了物理上合理的方法来估计光源的主波长。这种扩展可用于光学效果研究、光谱渲染和可视化工具的开发，同时保持了与现有渲染管线的兼容性。

尽管存在一些简化和局限性，这个实现为后续更复杂的基于波长的渲染系统提供了基础。未来可以在此基础上添加色散、干涉等波动光学效果，实现更加真实的光学模拟。

## 8. 光线追踪实例分析

为了更直观地理解波长和光强度的计算与输出过程，以下是一个完整的光线追踪示例。我们将跟踪一条从摄像机出发，经过场景中物体反射后最终达到光源的光线路径，并分析在这个过程中波长和光强的计算与更新时机。

### 8.1 具体示例：一条光线的完整生命周期

考虑以下场景：一个室内场景，包含一个白色墙壁、一个蓝色球体、一个红色的灯光源和一个窗户（环境光源）。我们将跟踪从摄像机发出击中蓝色球体后反射到红色灯光的一条光线。

#### 步骤1：光线初始化
```slang
// 在PathTracer.slang中的generatePath函数
void generatePath(const uint pathID, out PathState path)
{
    path = {};
    path.setActive();
    path.id = pathID;
    path.thp = float3(1.f);    // 初始吞吐量为1
    path.wavelength = 0.0f;    // 初始波长未确定

    // 创建从摄像机发出的主光线
    Ray cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
    path.origin = cameraRay.origin;
    path.dir = cameraRay.dir;

    // 其他初始化...
}
```
**分析**：此时光线刚从摄像机发出，波长设为0（未确定），吞吐量为1（未衰减），还没有遇到任何光源，所以不需要计算波长。

#### 步骤2：光线击中蓝色球体
```slang
// 在handleHit函数中处理球体碰撞
// 光线已经击中蓝色球体，计算着色点信息
ShadingData sd = loadShadingData(path.hit, path.origin, path.dir);

// 获取材质属性
const IMaterialInstance mi = gScene.materials.getMaterialInstance(sd, lod, hints);
BSDFProperties bsdfProperties = mi.getProperties(sd);

// 检查是否为发光表面（蓝色球体不发光，所以跳过发射光波长计算）
if (computeEmissive && any(bsdfProperties.emission > 0.f))
{
    // 对于发光物体，这里会计算波长，但蓝色球体不发光，所以不进入此分支
}

// 计算散射方向（在这里，光线会从蓝色球体表面散射）
bool valid = generateScatterRay(sd, mi, path);

// 更新光线吞吐量 - 考虑球体表面的蓝色反射率
// 假设蓝色球体的反射率为 (0.1, 0.1, 0.8)
// path.thp *= float3(0.1, 0.1, 0.8);  // 这在generateScatterRay内部完成
```
**分析**：光线击中蓝色球体后，由于球体不是光源，所以还没有计算波长。但是光线的吞吐量会根据材质的反射特性更新（在此例中为蓝色，所以RGB中的B分量保留较多，R和G分量衰减较多）。

#### 步骤3：从球体表面散射后击中红色灯光
```slang
// 续前，光线从蓝色球体反射，现在击中了红色灯光
// 再次调用handleHit函数
ShadingData sd = loadShadingData(path.hit, path.origin, path.dir);

// 获取灯光材质属性
const IMaterialInstance mi = gScene.materials.getMaterialInstance(sd, lod, hints);
BSDFProperties bsdfProperties = mi.getProperties(sd);

// 检查是否为发光表面（红色灯光是发光表面）
if (computeEmissive && any(bsdfProperties.emission > 0.f))
{
    // 假设红色灯光发射RGB = (1.0, 0.1, 0.1)的光
    float3 emission = misWeight * bsdfProperties.emission; // 这里emission ≈ (1.0, 0.1, 0.1)
    addToPathContribution(path, emission);  // 将发射光添加到路径贡献

    // 计算经过蓝色球体后的衰减发射光
    // 由于蓝色球体主要反射蓝光，吸收红光，所以经过衰减后的红灯光变暗，
    // 且颜色混合后偏紫色 (R较低，G很低，B较高)
    attenuatedEmission = path.thp * emission; // 例如 (0.1, 0.01, 0.08)

    // ！！！ 波长计算发生在这里 ！！！
    updatePathWavelength(path, attenuatedEmission);

    // updatePathWavelength内部实现
    float wavelength = SpectrumUtils::RGBtoDominantWavelength(attenuatedEmission);
    // 由于红光经过蓝色物体反射后颜色变成偏紫色，
    // 波长可能计算为约380-450nm之间（紫色波长范围）
    path.setWavelength(wavelength);
}

// 到这里光线生命周期结束（击中光源后不再散射）
path.terminate();
```
**分析**：这是**波长计算发生的关键时刻**。当光线击中红色灯光（光源）时，根据光源的发射光颜色和光线的当前吞吐量（已经被蓝色球体调整过，偏蓝），计算衰减后的光色，并据此确定主波长。

#### 步骤4：光线完成并输出结果
```slang
// 在PathTracer.slang中的writeOutput函数
void writeOutput(const PathState path)
{
    const uint2 pixel = path.getPixel();
    const uint outIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();

    if (kSamplesPerPixel == 1)
    {
        // 直接写入帧缓冲
        outputColor[pixel] = float4(path.L, 1.f);
    }
    else
    {
        // 写入每样本缓冲
        sampleColor[outIdx].set(path.L);
    }

    // ！！！ 如果需要，这里可以计算并输出光强度 ！！！
    // 比如计算最终像素颜色的亮度：
    float intensity = path.getLightIntensity(); // 调用luminance(path.L)

    // 如果有专门的波长和强度输出缓冲区（当前实现没有），可以在这里写入
    // 例如：
    // sampleWavelength[outIdx] = path.getWavelength();  // 输出波长（例如430nm）
    // sampleIntensity[outIdx] = intensity;              // 输出强度
}
```
**分析**：在整个光线路径完成后，最终的颜色贡献被写入到帧缓冲区或样本缓冲区。这一步是**光强计算和最终结果输出的时刻**。通过调用`getLightIntensity()`可以计算最终颜色的亮度值，但当前实现没有专门的波长和强度输出缓冲区。

### 8.2 波长计算和光强更新的特殊情况

#### 场景1：光线直接击中光源
如果光线从摄像机发出后直接击中红色灯光，而不是先击中蓝色球体：
```slang
// 在第一次handleHit时，直接击中光源
if (computeEmissive && any(bsdfProperties.emission > 0.f))
{
    // 红色灯光发射RGB = (1.0, 0.1, 0.1)的光
    float3 emission = misWeight * bsdfProperties.emission;
    addToPathContribution(path, emission);

    // 此时path.thp仍为(1,1,1)，所以attenuatedEmission ≈ (1.0, 0.1, 0.1)
    attenuatedEmission = path.thp * emission;

    // 波长计算得到约为~620nm（红色光波长范围）
    updatePathWavelength(path, attenuatedEmission);
}
```

#### 场景2：光线未击中任何物体（环境光）
如果光线从摄像机发出后未击中任何物体，而是采样环境光（窗户）：
```slang
// 在handleMiss函数中
float3 Le = envMapSampler.eval(path.dir); // 假设窗外是蓝天，Le ≈ (0.5, 0.7, 1.0)
emitterRadiance = misWeight * Le;
float3 contribution = path.thp * emitterRadiance; // ≈ (0.5, 0.7, 1.0)

// ！！！ 环境光波长计算发生在这里 ！！！
if (path.getWavelength() == 0.0f && any(contribution > 0.0f))
{
    float wavelength = SpectrumUtils::RGBtoDominantWavelength(contribution);
    // 蓝天颜色的主波长约为~475nm（蓝色波长范围）
    if (wavelength != 0.0f)
    {
        path.setWavelength(wavelength);
    }
}
```

### 8.3 结果分析

在以上示例中：

1. **波长计算的时机**：
   - 当光线击中发光表面（红色灯光）时
   - 当光线未击中任何物体采样环境光（蓝天）时
   - 注意：波长只计算一次，且发生在光线首次遇到光源时

2. **光强计算的时机**：
   - 光强在光线传播过程中通过`path.thp`（吞吐量）持续更新
   - 最终光强可在`writeOutput`函数中通过`path.getLightIntensity()`计算
   - 光强会随着光线与场景的每次交互而变化（如被蓝色球体削弱）

3. **结果输出的时机**：
   - 当光线路径完成后（击中光源或达到最大弹射次数）
   - 在`writeOutput`函数中写入帧缓冲区或样本缓冲区
   - 当前实现没有专门输出波长和强度的机制，这些信息存储在PathState中但不会单独导出

这个示例说明了波长如何根据光源颜色与场景物体的交互计算得出，以及光强如何在整个光线传播过程中更新。波长计算和光强计算取决于光线的具体路径和它遇到的表面材质特性。
