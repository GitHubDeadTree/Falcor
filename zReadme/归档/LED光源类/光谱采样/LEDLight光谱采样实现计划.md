# LEDLight光谱采样实现计划

## 任务概述

### 核心任务（最简化版本）
为Falcor渲染引擎的LEDLight类添加光谱采样功能，实现完整的光谱到波长的采样流程：

1. **光谱数据存储**：使用DenseleySampledSpectrum存储LED的光谱分布数据
2. **CDF构建**：为光谱数据构建累积分布函数，支持重要性采样
3. **波长采样**：在GPU着色器中从光谱分布随机采样波长
4. **PathState集成**：将采样的波长传入PathState进行光线追踪

### 最终目标
每次LEDLight采样时，除获得空间位置和方向信息外，还能获得一个根据光谱分布随机采样的波长值，支持物理准确的光谱渲染。

## 子任务分解

### 子任务1：修改LEDLight类支持光谱数据存储

#### 1. 任务目标
在LEDLight类中添加基于DenseleySampledSpectrum的光谱数据存储和管理功能。

#### 2. 实现方案

**修改LEDLight.h头文件**：
```cpp
#include "Utils/Color/Spectrum.h"

class LEDLight : public Light {
private:
    // 新增光谱相关成员
    std::unique_ptr<DenseleySampledSpectrum> mpSpectrum;
    std::vector<float> mSpectrumCDF;
    bool mHasCustomSpectrum = false;

    // 光谱处理函数
    void buildSpectrumCDF();
    float sampleWavelengthFromSpectrum(float u) const;

public:
    void setSpectrum(const std::vector<float2>& spectrumData);
    bool hasCustomSpectrum() const { return mHasCustomSpectrum; }
    size_t getSpectrumSampleCount() const;
    float2 getSpectrumRange() const;
};
```

**修改LEDLight.cpp实现**：
```cpp
void LEDLight::setSpectrum(const std::vector<float2>& spectrumData) {
    try {
        if (spectrumData.empty()) {
            logWarning("LEDLight::setSpectrum - Empty spectrum data");
            mHasCustomSpectrum = false;
            return;
        }

        if (spectrumData.size() < 2) {
            logError("LEDLight::setSpectrum - Insufficient data points");
            mHasCustomSpectrum = false;
            return;
        }

        // 创建分段线性光谱
        std::vector<float> wavelengths, intensities;
        for (const auto& point : spectrumData) {
            wavelengths.push_back(point.x);
            intensities.push_back(point.y);
        }

        PiecewiseLinearSpectrum piecewise(wavelengths, intensities);
        mpSpectrum = std::make_unique<DenseleySampledSpectrum>(piecewise, 1.0f);

        buildSpectrumCDF();
        mHasCustomSpectrum = true;

        logInfo("LEDLight::setSpectrum - Loaded spectrum with " +
                std::to_string(spectrumData.size()) + " points");

    } catch (const std::exception& e) {
        logError("LEDLight::setSpectrum - Exception: " + std::string(e.what()));
        mHasCustomSpectrum = false;
    }
}

void LEDLight::buildSpectrumCDF() {
    if (!mpSpectrum) {
        logError("LEDLight::buildSpectrumCDF - No spectrum data");
        return;
    }

    try {
        auto range = mpSpectrum->getWavelengthRange();
        float wavelengthStep = (range.y - range.x) / (mpSpectrum->size() - 1);

        mSpectrumCDF.clear();
        mSpectrumCDF.reserve(mpSpectrum->size());

        float cumulativeSum = 0.0f;
        for (size_t i = 0; i < mpSpectrum->size(); ++i) {
            float wavelength = range.x + i * wavelengthStep;
            float intensity = mpSpectrum->eval(wavelength);
            cumulativeSum += intensity * wavelengthStep;
            mSpectrumCDF.push_back(cumulativeSum);
        }

        // 归一化CDF
        if (cumulativeSum > 1e-6f) {
            for (float& cdfValue : mSpectrumCDF) {
                cdfValue /= cumulativeSum;
            }
        } else {
            logError("LEDLight::buildSpectrumCDF - Zero integral, using uniform");
            for (size_t i = 0; i < mSpectrumCDF.size(); ++i) {
                mSpectrumCDF[i] = float(i + 1) / mSpectrumCDF.size();
            }
        }

    } catch (const std::exception& e) {
        logError("LEDLight::buildSpectrumCDF - Exception: " + std::string(e.what()));
        // 创建默认均匀CDF
        mSpectrumCDF.clear();
        for (size_t i = 0; i < 128; ++i) {
            mSpectrumCDF.push_back(float(i + 1) / 128.0f);
        }
    }
}

float LEDLight::sampleWavelengthFromSpectrum(float u) const {
    if (!mHasCustomSpectrum || !mpSpectrum || mSpectrumCDF.empty()) {
        logWarning("LEDLight::sampleWavelengthFromSpectrum - No custom spectrum");
        return 0.666f; // 错误标识值
    }

    try {
        auto it = std::lower_bound(mSpectrumCDF.begin(), mSpectrumCDF.end(), u);
        size_t index = std::distance(mSpectrumCDF.begin(), it);

        if (index >= mSpectrumCDF.size()) {
            index = mSpectrumCDF.size() - 1;
        }

        auto range = mpSpectrum->getWavelengthRange();
        float wavelengthStep = (range.y - range.x) / (mpSpectrum->size() - 1);
        float wavelength = range.x + index * wavelengthStep;

        // 区间内插值
        if (index > 0 && index < mSpectrumCDF.size()) {
            float cdf0 = mSpectrumCDF[index - 1];
            float cdf1 = mSpectrumCDF[index];
            float t = (u - cdf0) / (cdf1 - cdf0);

            float wavelength0 = range.x + (index - 1) * wavelengthStep;
            float wavelength1 = range.x + index * wavelengthStep;
            wavelength = wavelength0 + t * (wavelength1 - wavelength0);
        }

        return wavelength;

    } catch (const std::exception& e) {
        logError("LEDLight::sampleWavelengthFromSpectrum - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}
```

#### 3. 计算正确性验证
- 检查光谱数据是否为空或过少
- 验证CDF构建过程中的数值稳定性
- 确保CDF最后一个值为1.0（在误差范围内）
- 验证采样函数返回的波长在合理范围内

#### 4. 验证成功的方法
- 调试信息显示"Loaded spectrum with X points"
- CDF构建成功，最后一个值接近1.0
- 采样函数返回的波长在光谱数据的波长范围内
- 没有出现0.666的错误标识值

### 子任务2：实现GPU端光谱数据传输

#### 1. 任务目标
将CPU端构建的光谱CDF数据传输到GPU，并在着色器中实现高效的光谱采样函数。

#### 2. 实现方案

**修改LightData.slang添加光谱数据字段**：
```glsl
struct LightData {
    // ... 现有字段 ...
    uint32_t spectrumCDFOffset = 0;        // CDF数据偏移
    uint32_t spectrumCDFSize = 0;          // CDF数据大小
    float spectrumMinWavelength = 380.0f;  // 最小波长
    float spectrumMaxWavelength = 780.0f;  // 最大波长
    uint32_t hasCustomSpectrum = 0;        // 是否有自定义光谱
    uint32_t _spectrumPad = 0;             // 对齐填充
};
```

**修改Scene.cpp添加光谱数据缓冲区管理**：
```cpp
class Scene {
private:
    ref<Buffer> mpSpectrumCDFBuffer;

public:
    void updateSpectrumData() {
        try {
            std::vector<float> allCDFData;
            uint32_t currentOffset = 0;

            for (auto& light : mLights) {
                if (auto ledLight = std::dynamic_pointer_cast<LEDLight>(light)) {
                    if (ledLight->hasCustomSpectrum()) {
                        const auto& cdfData = ledLight->getSpectrumCDF();
                        ledLight->setSpectrumCDFOffset(currentOffset);
                        allCDFData.insert(allCDFData.end(), cdfData.begin(), cdfData.end());
                        currentOffset += cdfData.size();
                    }
                }
            }

            if (!allCDFData.empty()) {
                size_t bufferSize = allCDFData.size() * sizeof(float);
                mpSpectrumCDFBuffer = Buffer::createStructured(sizeof(float), allCDFData.size());
                mpSpectrumCDFBuffer->setBlob(allCDFData.data(), 0, bufferSize);

                logInfo("Scene::updateSpectrumData - Updated buffer with " +
                        std::to_string(allCDFData.size()) + " entries");
            }

        } catch (const std::exception& e) {
            logError("Scene::updateSpectrumData - Exception: " + std::string(e.what()));
        }
    }
};
```

**创建GPU端光谱采样函数**：
```glsl
// SpectrumSampling.slang
StructuredBuffer<float> gSpectrumCDFData;

float sampleLEDWavelength(LightData light, float u) {
    if (light.hasCustomSpectrum == 0 || light.spectrumCDFSize == 0) {
        return lerp(380.0f, 780.0f, u);  // 默认均匀采样
    }

    uint cdfOffset = light.spectrumCDFOffset;
    uint cdfSize = light.spectrumCDFSize;

    // 线性查找CDF
    uint sampleIndex = 0;
    for (uint i = 0; i < cdfSize; ++i) {
        float cdfValue = gSpectrumCDFData[cdfOffset + i];
        if (u <= cdfValue) {
            sampleIndex = i;
            break;
        }
    }

    // 计算对应的波长
    float wavelengthStep = (light.spectrumMaxWavelength - light.spectrumMinWavelength) / (cdfSize - 1);
    float wavelength = light.spectrumMinWavelength + sampleIndex * wavelengthStep;

    // 区间内插值
    if (sampleIndex > 0 && sampleIndex < cdfSize) {
        float cdf0 = gSpectrumCDFData[cdfOffset + sampleIndex - 1];
        float cdf1 = gSpectrumCDFData[cdfOffset + sampleIndex];

        if (abs(cdf1 - cdf0) > 1e-6f) {
            float t = (u - cdf0) / (cdf1 - cdf0);
            float wavelength0 = light.spectrumMinWavelength + (sampleIndex - 1) * wavelengthStep;
            float wavelength1 = light.spectrumMinWavelength + sampleIndex * wavelengthStep;
            wavelength = wavelength0 + t * (wavelength1 - wavelength0);
        }
    }

    wavelength = clamp(wavelength, light.spectrumMinWavelength, light.spectrumMaxWavelength);

    return wavelength;
}

float evaluateSpectrumPDF(LightData light, float wavelength) {
    if (light.hasCustomSpectrum == 0) {
        return 1.0f / (780.0f - 380.0f);
    }
    return 1.0f / (light.spectrumMaxWavelength - light.spectrumMinWavelength);
}
```

#### 3. 计算正确性验证
- 检查CDF数据是否正确传输到GPU
- 验证采样函数返回的波长在合理范围内
- 确保默认情况下的回退机制正常工作
- 检查PDF计算的数值稳定性

#### 4. 验证成功的方法
- GPU缓冲区创建成功，大小匹配预期
- 采样函数返回的波长在光谱范围内
- 调试信息显示"Updated buffer with X entries"

### 子任务3：修改LEDLight采样函数集成光谱采样

#### 1. 任务目标
修改sampleLEDLight函数，集成光谱采样功能，将波长传递到PathState中。

#### 2. 实现方案

**修改LightHelpers.slang的sampleLEDLight函数**：
```glsl
#include "SpectrumSampling.slang"

bool sampleLEDLightWithSpectrum(
    const float3 shadingPosW,
    const LightData light,
    const float3 u,
    out AnalyticLightSample ls,
    out float sampledWavelength
) {
    // 1. 几何采样逻辑
    float3 localPos = float3(0.0f);
    uint32_t shapeType = clamp(light.shapeType, 0u, 2u);

    switch (shapeType) {
    case 0: localPos = sample_sphere(u.xy); break;
    case 1: localPos = sample_sphere(u.xy); break;
    case 2: localPos = float3(u.x * 2.0f - 1.0f, u.y * 2.0f - 1.0f, 0.0f); break;
    default: localPos = sample_sphere(u.xy); break;
    }

    float3 transformedLocalPos = mul(light.transMat, float4(localPos, 1.0f)).xyz;
    ls.posW = light.posW + transformedLocalPos;

    float3 localNormal = localPos;
    if (shapeType == 2) {
        localNormal = float3(0.0f, 0.0f, 1.0f);
    }
    ls.normalW = normalize(mul(light.transMatIT, float4(localNormal, 0.0f)).xyz);

    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    float cosTheta = dot(-ls.dir, light.dirW);
    if (cosTheta < light.cosOpeningAngle) {
        sampledWavelength = 0.666f;
        return false;
    }

    // 2. 光谱采样
    sampledWavelength = sampleLEDWavelength(light, u.z);

    if (sampledWavelength < 300.0f || sampledWavelength > 1000.0f) {
        sampledWavelength = 550.0f;  // 默认绿光
    }

    // 3. 光强计算
    float angleFalloff = 1.0f;
    if (light.hasCustomLightField > 0 && light.lightFieldDataSize > 0) {
        float angle = acos(clamp(cosTheta, 0.0f, 1.0f));
        float normalizedDistribution = interpolateLightField(light.lightFieldDataOffset, light.lightFieldDataSize, angle);
        angleFalloff = max(normalizedDistribution, 0.0f);
    } else {
        float lambertN = max(light.lambertN, 0.1f);
        float normalizationFactor = max(lambertN + 1.0f, 0.1f);
        angleFalloff = pow(max(cosTheta, 0.0f), lambertN) / normalizationFactor;
    }

    float surfaceArea = max(light.surfaceArea, 1e-9f);
    float cosNormalTheta = dot(ls.normalW, -ls.dir);
    if (cosNormalTheta <= 0.0f) {
        sampledWavelength = 0.666f;
        return false;
    }

    ls.Li = light.intensity * angleFalloff * (surfaceArea * cosNormalTheta / distSqr);

    // 4. PDF计算
    float geometricPDF = distSqr / (cosNormalTheta * surfaceArea);
    float spectralPDF = evaluateSpectrumPDF(light, sampledWavelength);
    ls.pdf = geometricPDF * spectralPDF;

    return true;
}

// 保持兼容性
bool sampleLEDLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls) {
    float dummyWavelength;
    return sampleLEDLightWithSpectrum(shadingPosW, light, float3(u, 0.5f), ls, dummyWavelength);
}
```

#### 3. 计算正确性验证
- 验证几何采样部分的正确性
- 检查光谱采样返回的波长范围
- 确保PDF计算的数值稳定性
- 验证异常情况下的回退机制

#### 4. 验证成功的方法
- 几何采样正常工作，不影响现有功能
- 光谱采样返回合理的波长值（300-1000nm范围内）
- PDF值为正数且数值合理

### 子任务4：PathTracer集成和波长传播

#### 1. 任务目标
修改PathTracer的光源采样逻辑，将LED光谱采样的波长传递到PathState中。

#### 2. 实现方案

**修改PathTracer.slang的光源采样逻辑**：
```glsl
void handleLEDLightSampling(inout PathState path, inout SampleGenerator sg) {
    LightData lightData = getLightData(path.hit.instanceID);

    if (lightData.type == LightType::LED) {
        AnalyticLightSample ls;
        float sampledWavelength;

        bool validSample = sampleLEDLightWithSpectrum(
            path.origin,
            lightData,
            sg.next3D(),
            ls,
            sampledWavelength
        );

        if (validSample) {
            if (path.getWavelength() == 0.0f) {
                path.setWavelength(sampledWavelength);
            }

            float3 lightContrib = ls.Li * evalBSDF(path.hit, ls.dir);
            path.L += path.thp * lightContrib;

        } else {
            if (path.getWavelength() == 0.0f) {
                path.setWavelength(550.0f);
            }
        }
    }
}

static void updatePathWavelengthFromLED(inout PathState path, const LightData lightData, const float3 Le) {
    if (path.getWavelength() == 0.0f && lightData.type == LightType::LED) {
        float u = sampleNext1D(path.sg);
        float wavelength = sampleLEDWavelength(lightData, u);

        if (wavelength >= 350.0f && wavelength <= 800.0f) {
            path.setWavelength(wavelength);
        } else {
            path.setWavelength(550.0f);
        }
    }
}

void tracePath(uint pathID) {
    PathState path = generatePath(pathID);

    while (path.isActive()) {
        if (path.isHit()) {
            HitInfo hit = path.hit;
            ShadingData sd = loadShadingData(hit, path.origin, path.dir);

            if (isEmissiveSurface(sd)) {
                LightData lightData = getLightData(hit.instanceID);

                if (lightData.type == LightType::LED) {
                    updatePathWavelengthFromLED(path, lightData, sd.emissive);
                }
            }

            if (shouldPerformNEE(path)) {
                handleLEDLightSampling(path, path.sg);
            }
        }
    }
}
```

#### 3. 计算正确性验证
- 验证波长设置逻辑的正确性
- 检查路径在多次bounce后波长是否保持
- 确保异常情况下有合理的默认值

#### 4. 验证成功的方法
- PathState中包含合理的波长值（350-800nm）
- 波长在路径传播过程中保持不变
- 没有出现0.0f的未初始化波长

### 子任务5：集成测试和验证

#### 1. 任务目标
创建完整的测试流程，验证LEDLight光谱采样功能的正确性和性能。

#### 2. 实现方案

**创建测试用例**：
```cpp
class LEDSpectrumTest {
public:
    void testSpectrumLoading() {
        std::vector<float2> testSpectrum = {
            {400.0f, 0.1f}, {450.0f, 0.5f}, {500.0f, 1.0f},
            {550.0f, 0.8f}, {600.0f, 0.3f}, {650.0f, 0.1f}
        };

        auto ledLight = LEDLight::create("TestLED");
        ledLight->setSpectrum(testSpectrum);

        assert(ledLight->hasCustomSpectrum());
        assert(ledLight->getSpectrumSampleCount() > 0);

        auto range = ledLight->getSpectrumRange();
        assert(range.x == 400.0f && range.y == 650.0f);

        logInfo("LEDSpectrumTest::testSpectrumLoading - PASSED");
    }

    void testWavelengthSampling() {
        auto ledLight = LEDLight::create("TestLED");

        std::vector<float2> peakSpectrum;
        for (int i = 400; i <= 700; i += 10) {
            float intensity = exp(-0.01f * (i - 550) * (i - 550));
            peakSpectrum.push_back({float(i), intensity});
        }

        ledLight->setSpectrum(peakSpectrum);

        std::vector<float> samples;
        for (int i = 0; i < 1000; ++i) {
            float u = float(i) / 999.0f;
            float wavelength = ledLight->sampleWavelengthFromSpectrum(u);
            samples.push_back(wavelength);
        }

        float mean = 0.0f;
        for (float w : samples) {
            mean += w;
        }
        mean /= samples.size();

        assert(abs(mean - 550.0f) < 20.0f);

        logInfo("LEDSpectrumTest::testWavelengthSampling - Mean: " +
                std::to_string(mean) + "nm - PASSED");
    }
};
```

**创建调试输出功能**：
```cpp
void LEDLight::printSpectrumDebugInfo() const {
    if (!mHasCustomSpectrum) {
        logInfo("LEDLight::printSpectrumDebugInfo - No custom spectrum");
        return;
    }

    logInfo("LEDLight::printSpectrumDebugInfo - Spectrum Info:");
    logInfo("  Sample count: " + std::to_string(mpSpectrum->size()));

    auto range = mpSpectrum->getWavelengthRange();
    logInfo("  Wavelength range: " + std::to_string(range.x) + " - " + std::to_string(range.y) + " nm");

    logInfo("  CDF size: " + std::to_string(mSpectrumCDF.size()));
    if (!mSpectrumCDF.empty()) {
        logInfo("  CDF range: 0.0 - " + std::to_string(mSpectrumCDF.back()));
    }

    std::vector<float> testSamples;
    for (int i = 0; i < 10; ++i) {
        float u = float(i) / 9.0f;
        float wavelength = sampleWavelengthFromSpectrum(u);
        testSamples.push_back(wavelength);
    }

    logInfo("  Sample wavelengths: ");
    for (float w : testSamples) {
        logInfo("    " + std::to_string(w) + " nm");
    }
}
```

#### 3. 计算正确性验证
- 验证光谱加载和CDF构建的正确性
- 检查采样分布是否符合预期
- 确保与PathState集成后波长传播正常

#### 4. 验证成功的方法
- 所有单元测试通过
- 采样分布的统计特性符合预期
- 在实际渲染中能看到波长信息正确传播
- 调试信息显示合理的数值范围

## 实施顺序建议

1. **按顺序完成子任务1-4**：每个子任务完成后进行单独验证
2. **增量测试**：每完成一个子任务后测试基本功能
3. **集成测试**：所有子任务完成后进行完整的集成测试
4. **性能测试**：确保新功能不会显著影响渲染性能

## 风险和注意事项

1. **GPU内存限制**：大量光谱数据可能影响GPU内存使用
2. **分支开销**：着色器中的条件分支可能影响性能
3. **数值精度**：CDF计算和采样可能存在精度问题
4. **兼容性**：确保新功能不破坏现有LED功能

## 预期结果

完成后的LEDLight将支持：
- 从任意光谱分布采样波长
- 将波长信息传递到PathState
- 保持与现有功能的兼容性
- 提供完整的调试和验证功能
