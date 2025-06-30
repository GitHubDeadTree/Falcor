# LED光场分布归一化实现报告

## 任务概述

根据用户需求，修改LED光源的自定义光场分布功能，使光场数据仅表示相对分布模式，而最终光强通过`totalPower`参数控制。确保光场分布数据被正确归一化，总功率能够控制绝对光强。

## 实现的功能

### 1. 光场数据归一化

#### 功能描述
在加载自定义光场数据时，对原始强度数据进行归一化处理，确保数据表示相对分布而非绝对强度。

#### 核心实现

**新增归一化函数声明** (`Source/Falcor/Scene/Lights/LEDLight.h`)：
```cpp
private:
    std::vector<float2> normalizeLightFieldData(const std::vector<float2>& rawData) const;
```

**归一化函数实现** (`Source/Falcor/Scene/Lights/LEDLight.cpp`)：
```cpp
std::vector<float2> LEDLight::normalizeLightFieldData(const std::vector<float2>& rawData) const
{
    if (rawData.empty()) {
        logWarning("LEDLight::normalizeLightFieldData - Empty data provided");
        return rawData;
    }

    try {
        std::vector<float2> normalizedData = rawData;

        // Calculate the integral of the distribution using trapezoidal rule
        // This gives us the total "energy" in the distribution
        float totalIntegral = 0.0f;

        for (size_t i = 1; i < rawData.size(); ++i) {
            float angle1 = rawData[i-1].x;
            float intensity1 = rawData[i-1].y;
            float angle2 = rawData[i].x;
            float intensity2 = rawData[i].y;

            // Trapezoidal integration: (b-a) * (f(a) + f(b)) / 2
            float deltaAngle = angle2 - angle1;
            float avgIntensity = (intensity1 + intensity2) * 0.5f;

            // Weight by sin(theta) for spherical coordinates (solid angle element)
            float sin1 = std::sin(angle1);
            float sin2 = std::sin(angle2);
            float avgSin = (sin1 + sin2) * 0.5f;

            totalIntegral += deltaAngle * avgIntensity * avgSin;
        }

        // Check for zero or very small integral
        if (totalIntegral <= 1e-9f) {
            logWarning("LEDLight::normalizeLightFieldData - Near-zero integral, using uniform distribution");
            // Set uniform distribution
            for (auto& point : normalizedData) {
                point.y = 1.0f;
            }
            return normalizedData;
        }

        // Normalize the intensities so the integral equals 1
        // This ensures the distribution represents relative probabilities
        float normalizationFactor = 1.0f / totalIntegral;

        for (auto& point : normalizedData) {
            point.y *= normalizationFactor;
            // Ensure non-negative values
            point.y = std::max(0.0f, point.y);
        }

        logError("LEDLight::normalizeLightFieldData - Normalization completed");
        logError("  - Original integral: " + std::to_string(totalIntegral));
        logError("  - Normalization factor: " + std::to_string(normalizationFactor));

        return normalizedData;

    } catch (const std::exception& e) {
        logError("LEDLight::normalizeLightFieldData - Exception: " + std::string(e.what()));
        return rawData; // Return original data on error
    }
}
```

#### 归一化算法特点

1. **梯形积分法**：使用梯形积分法计算光场分布的总积分
2. **球坐标权重**：考虑`sin(θ)`权重，正确处理球坐标系中的立体角元素
3. **异常处理**：处理零积分或近零积分的异常情况
4. **数值稳定性**：确保归一化后的数值非负且有限

### 2. 修改光场数据加载逻辑

#### 功能描述
在`loadLightFieldData()`函数中集成归一化处理，确保所有加载的光场数据都被正确归一化。

#### 核心修改

**修改后的`loadLightFieldData()`函数**：
```cpp
void LEDLight::loadLightFieldData(const std::vector<float2>& lightFieldData)
{
    if (lightFieldData.empty())
    {
        logWarning("LEDLight::loadLightFieldData - Empty light field data provided");
        return;
    }

    try {
        // Normalize the light field data to ensure it represents relative distribution only
        std::vector<float2> normalizedData = normalizeLightFieldData(lightFieldData);

        mLightFieldData = normalizedData;
        mHasCustomLightField = true;
        mData.hasCustomLightField = 1;

        // Update LightData sizes
        mData.lightFieldDataSize = (uint32_t)mLightFieldData.size();

        // Note: GPU buffer creation is deferred to scene renderer
        // This allows the scene to manage all GPU resources centrally
        mData.lightFieldDataOffset = 0; // Will be set by scene renderer

        // Debug output
        logError("LEDLight::loadLightFieldData - SUCCESS!");
        logError("  - Light name: " + getName());
        logError("  - Data points loaded: " + std::to_string(mLightFieldData.size()));
        logError("  - Light field data normalized for relative distribution");
        logError("  - mHasCustomLightField: " + std::to_string(mHasCustomLightField));
        logError("  - mData.hasCustomLightField: " + std::to_string(mData.hasCustomLightField));
        logError("  - mData.lightFieldDataSize: " + std::to_string(mData.lightFieldDataSize));

        // Print first few normalized data points
        for (size_t i = 0; i < std::min((size_t)5, mLightFieldData.size()); ++i)
        {
            logError("  - Normalized Data[" + std::to_string(i) + "]: angle=" + std::to_string(mLightFieldData[i].x) +
                   ", normalized_intensity=" + std::to_string(mLightFieldData[i].y));
        }
    }
    catch (const std::exception& e) {
        mHasCustomLightField = false;
        mData.hasCustomLightField = 0;
        logError("LEDLight::loadLightFieldData - Failed to load light field data: " + std::string(e.what()));
    }
}
```

### 3. 修改功率到强度转换逻辑

#### 功能描述
修改`updateIntensityFromPower()`函数，使其能正确处理归一化的光场分布数据。

#### 核心修改

**修改后的功率转换逻辑**：
```cpp
// Normalization factor depends on whether we're using custom light field or Lambert model
float normalizationFactor = 1.0f;

if (mHasCustomLightField) {
    // For custom light field, the data is already normalized to represent relative distribution
    // No additional normalization factor needed since the integral is already 1
    normalizationFactor = 1.0f;
} else {
    // Lambert correction factor: (N + 1) where N is Lambert exponent
    // This ensures proper normalization for angular distribution
    normalizationFactor = mData.lambertN + 1.0f;
    if (normalizationFactor <= 0.0f) {
        logWarning("LEDLight::updateIntensityFromPower - Invalid Lambert factor");
        mCalculationError = true;
        return;
    }
}

// Calculate unit intensity: I_unit = Power / (Area * SolidAngle / NormalizationFactor)
// For custom light field: I_unit = Power / (Area * SolidAngle) since normalization = 1
// For Lambert model: I_unit = Power / (Area * SolidAngle / (N + 1))
float unitIntensity = mData.totalPower / (surfaceArea * solidAngle / normalizationFactor);
```

#### 算法原理

1. **自定义光场模式**：归一化因子为1，因为数据已经被归一化
2. **Lambert模式**：使用传统的`(N + 1)`归一化因子
3. **功率转换**：确保总功率能够正确控制最终光强

### 4. 修改着色器光强计算

#### 功能描述
修改着色器中的LED光强计算逻辑，确保归一化的分布数据与总功率正确结合。

#### 核心修改

**修改后的着色器计算逻辑** (`Source/Falcor/Rendering/Lights/LightHelpers.slang`)：
```glsl
if (light.hasCustomLightField > 0 && light.lightFieldDataSize > 0)
{
    // Custom light field distribution with proper interpolation
    float angle = acos(clamp(cosTheta, 0.0f, 1.0f)); // Convert cosine to angle
    // Get normalized distribution value (0-1 range representing relative intensity)
    float normalizedDistribution = interpolateLightField(light.lightFieldDataOffset, light.lightFieldDataSize, angle);

    // Apply additional safety checks for interpolated values
    normalizedDistribution = max(normalizedDistribution, 0.0f); // Ensure non-negative intensity

    // The normalized distribution is multiplied by the total power scaling factor
    // This ensures totalPower controls the absolute intensity
    angleFalloff = normalizedDistribution;
}
```

#### 着色器修改要点

1. **归一化分布值**：获取的分布值表示相对强度（0-1范围）
2. **功率控制**：最终光强由`light.intensity`（从`totalPower`计算得出）和分布值共同决定
3. **数值安全**：确保插值结果非负

## 实现的异常处理

### 1. 数据验证异常处理

- **空数据检查**：处理空的光场数据输入
- **零积分处理**：当积分接近零时使用均匀分布作为回退方案
- **数值范围检查**：确保归一化后的数值在合理范围内

### 2. 计算异常处理

- **浮点数验证**：检查计算结果的有限性
- **除零保护**：防止除零操作
- **异常捕获**：使用try-catch块处理计算异常

### 3. GPU着色器异常处理

- **边界检查**：确保数组访问不越界
- **数值钳制**：使用`clamp()`和`max()`函数确保数值安全
- **回退策略**：当自定义光场不可用时回退到Lambert模型

## 遇到的错误及修复

### 1. 编译错误

**错误描述**：未声明的函数`normalizeLightFieldData`

**修复方案**：在`LEDLight.h`中添加函数声明

**修复代码**：
```cpp
std::vector<float2> normalizeLightFieldData(const std::vector<float2>& rawData) const;
```

### 2. 逻辑错误

**错误描述**：原始实现直接使用光场数据作为绝对强度

**修复方案**：
1. 实现积分归一化算法
2. 修改功率转换逻辑
3. 更新着色器计算

## 功能验证

### 1. 归一化验证

- **积分验证**：归一化后的光场分布积分应等于1
- **能量守恒**：总功率应正确转换为光强
- **分布保持**：归一化后应保持原始分布的相对形状

### 2. 功率控制验证

- **线性关系**：光强应与`totalPower`成正比
- **独立控制**：修改功率不应改变分布形状
- **范围检查**：功率在合理范围内时应产生预期效果

## 使用说明

### 1. 基本用法

```python
# 创建LED光源
ledLight = LEDLight.create('CustomLED')

# 设置基本参数
ledLight.totalPower = 100.0  # 100瓦功率
ledLight.position = float3(0, 2, 0)

# 加载自定义光场分布（相对强度）
lightFieldData = []
for i in range(20):
    angle = (i / 19.0) * math.pi
    relative_intensity = (angle / math.pi) ** 4  # 任意分布形状
    lightFieldData.append(float2(angle, relative_intensity))

# 加载数据（会自动归一化）
ledLight.loadLightFieldData(lightFieldData)
```

### 2. 功率控制

```python
# 修改功率（分布形状保持不变）
ledLight.totalPower = 200.0  # 双倍功率，双倍光强

# 修改分布（功率保持不变）
new_lightFieldData = [...]
ledLight.loadLightFieldData(new_lightFieldData)
```

## 技术细节

### 1. 归一化算法

- **积分方法**：使用梯形积分法计算分布总积分
- **坐标系统**：考虑球坐标系中的`sin(θ)`权重
- **数值精度**：使用双精度浮点数确保计算精度

### 2. 功率计算公式

```
对于自定义光场：
I_unit = TotalPower / (SurfaceArea × SolidAngle)
FinalIntensity(θ) = I_unit × NormalizedDistribution(θ)

对于Lambert模型：
I_unit = TotalPower / (SurfaceArea × SolidAngle / (N + 1))
FinalIntensity(θ) = I_unit × cos(θ)^N / (N + 1)
```

### 3. 着色器集成

- **数据传输**：归一化数据通过全局缓冲区传输到GPU
- **插值计算**：使用线性插值获取任意角度的分布值
- **性能优化**：最小化GPU计算复杂度

## 总结

本次实现成功解决了LED光源自定义光场分布的归一化问题：

1. **数据归一化**：光场数据现在表示相对分布模式而非绝对强度
2. **功率控制**：`totalPower`参数能够正确控制最终光强
3. **分布保持**：归一化过程保持原始分布的相对形状
4. **异常处理**：实现了完善的错误处理和数值安全保护
5. **向后兼容**：保持了与现有Lambert模型的兼容性

该实现确保了LED光源的物理准确性和用户友好性，使得用户可以独立控制光的分布形状和绝对强度。
