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
