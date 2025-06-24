# LED_Emissive光源系统实现计划

## 任务概述

**目标**：创建LED_Emissive类，基于Falcor的EmissiveLight系统实现LED光源，支持自定义光场分布、光锥角度控制和UI交互。

**与现有LEDLight的区别**：

- LEDLight继承自Light基类，使用解析光源方式
- LED_Emissive基于EmissiveLight系统，将LED建模为发光三角形网格
- 支持光线击中LED表面的视距链路模拟

## 现有LEDLight功能分析

### ✅ 已有功能

1. **自定义光场分布**：支持角度-强度数据，从文件加载
2. **默认Lambert分布**：Lambert指数可调(0.1-100.0)，UI支持修改
3. **光锥角度控制**：支持0-π角度范围，UI中可调节
4. **形状支持**：球体、椭球体、矩形三种形状
5. **功率控制**：总功率设置，自动计算光强
6. **光谱分布**：自定义波长-强度数据

### 🔄 需要适配到EmissiveLight的功能

1. **几何体生成**：创建发光三角形网格
2. **材质集成**：与Falcor材质系统集成
3. **LightProfile集成**：利用现有LightProfile系统
4. **场景可见性**：支持光线击中LED表面

## 子任务分解

### 子任务1：LED_Emissive基础框架

**1. 任务目标**
创建LED_Emissive类，继承或集成EmissiveLight系统基础功能

**2. 实现方案**

```cpp
// LED_Emissive.h
#pragma once
#include "Scene/Scene.h"
#include "Scene/Material/Material.h"
#include "Scene/Lights/LightProfile.h"
#include "Core/Framework.h"

namespace Falcor {

enum class LED_EmissiveShape {
    Sphere = 0,
    Rectangle = 1,
    Ellipsoid = 2
};

class FALCOR_API LED_Emissive {
public:
    static ref<LED_Emissive> create(const std::string& name = "LED_Emissive");
    LED_Emissive(const std::string& name);
    ~LED_Emissive() = default;

    // 基础属性
    void setShape(LED_EmissiveShape shape);
    void setPosition(const float3& pos);
    void setScaling(const float3& scale);
    void setDirection(const float3& dir);
    void setTotalPower(float power);
    void setColor(const float3& color);

    // 光场分布控制
    void setLambertExponent(float n);
    void setOpeningAngle(float angle);
    void loadLightFieldData(const std::vector<float2>& data);
    void loadLightFieldFromFile(const std::string& filePath);
    void clearLightFieldData();

    // 场景集成
    void addToScene(Scene* pScene);
    void removeFromScene();
    void renderUI(Gui::Widgets& widget);

    // 获取属性
    LED_EmissiveShape getShape() const { return mShape; }
    float3 getPosition() const { return mPosition; }
    float getTotalPower() const { return mTotalPower; }
    float getLambertExponent() const { return mLambertN; }
    float getOpeningAngle() const { return mOpeningAngle; }
    bool hasCustomLightField() const { return mHasCustomLightField; }

private:
    std::string mName;
    LED_EmissiveShape mShape = LED_EmissiveShape::Sphere;
    float3 mPosition = float3(0.0f);
    float3 mScaling = float3(1.0f);
    float3 mDirection = float3(0, 0, -1);
    float mTotalPower = 1.0f;
    float3 mColor = float3(1.0f);

    // 光场分布参数
    float mLambertN = 1.0f;
    float mOpeningAngle = (float)M_PI;
    float mCosOpeningAngle = -1.0f;

    // 自定义光场数据
    std::vector<float2> mLightFieldData;
    bool mHasCustomLightField = false;
    ref<LightProfile> mpLightProfile;

    // 场景集成
    Scene* mpScene = nullptr;
    std::vector<uint32_t> mMeshIndices;
    uint32_t mMaterialID = 0;
    bool mIsAddedToScene = false;
    bool mCalculationError = false;

    // 私有方法
    void generateGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void updateLightProfile();
    void updateEmissiveIntensity();
    float calculateSurfaceArea() const;
    ref<Material> createEmissiveMaterial();
};

} // namespace Falcor
```

**3. 错误处理**

- 参数验证：scaling>0, power>=0, angle∈[0,π]
- 无效参数时使用默认值0.666f并记录logWarning
- 设置mCalculationError标志追踪计算错误

**4. 验证方法**

- 成功创建LED_Emissive对象
- 属性设置和获取正确
- 无错误或警告日志

### 子任务2：LightProfile集成与光场分布

**1. 任务目标**
实现与Falcor LightProfile系统的集成，支持自定义光场分布

**2. 实现方案**

```cpp
void LED_Emissive::updateLightProfile() {
    try {
        if (mHasCustomLightField) {
            // 创建自定义LightProfile
            mpLightProfile = LightProfile::createFromCustomData(mLightFieldData);
        } else {
            // 使用Lambert分布创建LightProfile
            mpLightProfile = createLambertLightProfile();
        }

        if (!mpLightProfile) {
            logError("LED_Emissive::updateLightProfile - Failed to create light profile");
            mCalculationError = true;
            return;
        }

        // 烘焙LightProfile到GPU纹理
        mpLightProfile->bake();
        mCalculationError = false;

    } catch (const std::exception& e) {
        logError("LED_Emissive::updateLightProfile - Exception: " + std::string(e.what()));
        mCalculationError = true;

        // 创建默认的Lambert分布
        mpLightProfile = createDefaultLightProfile();
    }
}

ref<LightProfile> LED_Emissive::createLambertLightProfile() {
    try {
        // 生成Lambert分布数据
        std::vector<float2> lambertData;
        const uint32_t samples = 64;

        for (uint32_t i = 0; i < samples; ++i) {
            float angle = (float)i / (samples - 1) * mOpeningAngle;
            float cosAngle = std::cos(angle);

            // Lambert分布: I(θ) = I₀ * cos(θ)^n
            float intensity = std::pow(std::max(0.0f, cosAngle), mLambertN);

            // 应用开角限制
            if (angle > mOpeningAngle) intensity = 0.0f;

            lambertData.push_back({angle, intensity});
        }

        return LightProfile::createFromCustomData(lambertData);

    } catch (const std::exception& e) {
        logError("LED_Emissive::createLambertLightProfile - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

void LED_Emissive::loadLightFieldData(const std::vector<float2>& data) {
    if (data.empty()) {
        logWarning("LED_Emissive::loadLightFieldData - Empty data provided");
        return;
    }

    try {
        // 验证数据格式 (angle, intensity)
        for (const auto& point : data) {
            if (point.x < 0.0f || point.x > M_PI || point.y < 0.0f) {
                logWarning("LED_Emissive::loadLightFieldData - Invalid data point");
                return;
            }
        }

        mLightFieldData = data;
        mHasCustomLightField = true;
        updateLightProfile();
        updateEmissiveIntensity();

    } catch (const std::exception& e) {
        logError("LED_Emissive::loadLightFieldData - Exception: " + std::string(e.what()));
        mCalculationError = true;
    }
}
```

**3. 错误处理**

- 验证光场数据格式（角度∈[0,π]，强度≥0）
- LightProfile创建失败时使用默认Lambert分布
- 记录详细错误信息

**4. 验证方法**

- LightProfile对象创建成功
- 自定义数据加载后hasCustomLightField()返回true
- UI显示正确的光场分布状态

### 子任务3：发光材质与EmissiveLight集成

**1. 任务目标**
创建发光材质并集成LightProfile，实现EmissiveLight功能

**2. 实现方案**

```cpp
ref<Material> LED_Emissive::createEmissiveMaterial() {
    try {
        auto pMaterial = StandardMaterial::create(mpScene->getDevice(), mName + "_Material");

        // 设置基础属性
        pMaterial->setBaseColor(float4(0.05f, 0.05f, 0.05f, 1.0f));
        pMaterial->setRoughness(0.9f);
        pMaterial->setMetallic(0.0f);

        // 设置发光属性
        pMaterial->setEmissiveColor(mColor);
        pMaterial->setEmissiveFactor(calculateEmissiveIntensity());

        // 集成LightProfile
        if (mpLightProfile) {
            pMaterial->setEnableLightProfile(true);
            pMaterial->setLightProfile(mpLightProfile);
        }

        // 验证发光强度
        if (pMaterial->getEmissiveFactor() <= 0.0f) {
            logWarning("LED_Emissive::createEmissiveMaterial - Zero emissive intensity");
            pMaterial->setEmissiveFactor(0.666f);
        }

        return pMaterial;

    } catch (const std::exception& e) {
        logError("LED_Emissive::createEmissiveMaterial - Exception: " + std::string(e.what()));

        // 返回错误标识材质
        auto pMaterial = StandardMaterial::create(mpScene->getDevice(), mName + "_Error");
        pMaterial->setEmissiveColor(float3(0.666f, 0.333f, 0.0f));
        pMaterial->setEmissiveFactor(0.666f);
        return pMaterial;
    }
}

float LED_Emissive::calculateEmissiveIntensity() {
    try {
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f) {
            logWarning("LED_Emissive::calculateEmissiveIntensity - Invalid surface area");
            return 0.666f;
        }

        // 计算光锥立体角
        float solidAngle = 2.0f * (float)M_PI * (1.0f - mCosOpeningAngle);
        if (solidAngle <= 0.0f) {
            logWarning("LED_Emissive::calculateEmissiveIntensity - Invalid solid angle");
            return 0.666f;
        }

        // 考虑Lambert分布的归一化因子
        float lambertFactor = mHasCustomLightField ? 1.0f : (mLambertN + 1.0f);

        // 计算单位发光强度：Power / (SurfaceArea * SolidAngle / LambertFactor)
        float intensity = mTotalPower * lambertFactor / (surfaceArea * solidAngle);

        if (!std::isfinite(intensity) || intensity < 0.0f) {
            logError("LED_Emissive::calculateEmissiveIntensity - Invalid result");
            return 0.666f;
        }

        return intensity;

    } catch (const std::exception& e) {
        logError("LED_Emissive::calculateEmissiveIntensity - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}
```

**3. 错误处理**

- 验证表面积和立体角计算
- 材质创建失败时返回错误标识材质
- 强度计算异常时返回0.666f

**4. 验证方法**

- 材质启用了LightProfile功能
- 发光强度基于功率正确计算
- 材质颜色与设置一致

### 子任务4：几何体生成与场景集成

**1. 任务目标**
生成LED几何体并作为发光三角形添加到场景

**2. 实现方案**

```cpp
void LED_Emissive::addToScene(Scene* pScene) {
    try {
        if (!pScene || mIsAddedToScene) {
            logError("LED_Emissive::addToScene - Invalid scene or already added");
            return;
        }

        mpScene = pScene;

        // 1. 更新LightProfile
        updateLightProfile();

        // 2. 生成几何体
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        generateGeometry(vertices, indices);

        if (mCalculationError) {
            logError("LED_Emissive::addToScene - Geometry generation failed");
            return;
        }

        // 3. 创建发光材质
        auto pMaterial = createEmissiveMaterial();
        mMaterialID = pScene->addMaterial(pMaterial);

        // 4. 创建网格
        auto pMeshData = createMeshData(vertices, indices);
        auto pMesh = Mesh::create(pMeshData);

        // 5. 应用变换
        float4x4 transform = createTransformMatrix();
        pMesh->setTransform(transform);

        // 6. 设置材质
        pMesh->setMaterialID(mMaterialID);

        // 7. 添加到场景
        uint32_t meshIndex = pScene->addMesh(pMesh);
        mMeshIndices.push_back(meshIndex);

        // 8. 创建场景节点
        auto pNode = SceneBuilder::Node::create(mName);
        pNode->meshID = meshIndex;
        pNode->transform = transform;
        pScene->addNode(pNode);

        mIsAddedToScene = true;
        logInfo("LED_Emissive::addToScene - Successfully added '" + mName + "' to scene");

    } catch (const std::exception& e) {
        logError("LED_Emissive::addToScene - Exception: " + std::string(e.what()));
        mCalculationError = true;
        cleanup();
    }
}

float4x4 LED_Emissive::createTransformMatrix() {
    try {
        // 缩放矩阵
        float4x4 scale = math::scale(float4x4::identity(), mScaling);

        // 旋转矩阵（将-Z轴对齐到方向向量）
        float3 forward = normalize(mDirection);
        float3 up = float3(0, 1, 0);

        // 处理方向与up平行的情况
        if (abs(dot(up, forward)) > 0.999f) {
            up = float3(1, 0, 0);
        }

        float3 right = normalize(cross(up, forward));
        up = cross(forward, right);

        float4x4 rotation = float4x4::identity();
        rotation[0] = float4(right, 0);
        rotation[1] = float4(up, 0);
        rotation[2] = float4(-forward, 0); // Falcor使用-Z作为前方

        // 平移矩阵
        float4x4 translation = math::translate(float4x4::identity(), mPosition);

        return mul(translation, mul(rotation, scale));

    } catch (const std::exception& e) {
        logError("LED_Emissive::createTransformMatrix - Exception: " + std::string(e.what()));
        return float4x4::identity();
    }
}
```

**3. 错误处理**

- 验证场景指针和重复添加
- 几何生成失败时停止添加
- 异常时清理已创建的资源

**4. 验证方法**

- 场景中添加了新的发光网格
- 网格位置和朝向正确
- EmissiveLight系统自动检测到发光三角形

### 子任务5：UI界面实现

**1. 任务目标**
实现完整的UI控制界面，支持所有参数的实时调节

**2. 实现方案**

```cpp
void LED_Emissive::renderUI(Gui::Widgets& widget) {
    // 基础属性
    widget.text("LED_Emissive: " + mName);

    float3 pos = mPosition;
    if (widget.var("Position", pos, -100.0f, 100.0f, 0.01f)) {
        setPosition(pos);
    }

    float3 dir = mDirection;
    if (widget.direction("Direction", dir)) {
        setDirection(dir);
    }

    // 形状设置
    static const Gui::DropdownList kShapeList = {
        { (uint32_t)LED_EmissiveShape::Sphere, "Sphere" },
        { (uint32_t)LED_EmissiveShape::Rectangle, "Rectangle" },
        { (uint32_t)LED_EmissiveShape::Ellipsoid, "Ellipsoid" }
    };

    uint32_t shape = (uint32_t)mShape;
    if (widget.dropdown("Shape", kShapeList, shape)) {
        setShape((LED_EmissiveShape)shape);
    }

    float3 scale = mScaling;
    if (widget.var("Scale", scale, 0.001f, 10.0f, 0.001f)) {
        setScaling(scale);
    }

    // 光学属性
    widget.separator();
    widget.text("Light Properties");

    float power = mTotalPower;
    if (widget.var("Total Power (W)", power, 0.0f, 1000.0f)) {
        setTotalPower(power);
    }

    float3 color = mColor;
    if (widget.rgbColor("Color", color)) {
        setColor(color);
    }

    // 光场分布控制
    widget.separator();
    widget.text("Light Field Distribution");

    float openingAngle = mOpeningAngle;
    if (widget.var("Opening Angle", openingAngle, 0.0f, (float)M_PI)) {
        setOpeningAngle(openingAngle);
    }

    if (!mHasCustomLightField) {
        float lambertN = mLambertN;
        if (widget.var("Lambert Exponent", lambertN, 0.1f, 100.0f)) {
            setLambertExponent(lambertN);
        }
    }

    // 自定义光场状态
    widget.separator();
    if (mHasCustomLightField) {
        widget.text("Custom Light Field: " + std::to_string(mLightFieldData.size()) + " points");
        if (widget.button("Clear Custom Data")) {
            clearLightFieldData();
        }
    } else {
        widget.text("Using Lambert Distribution (N=" + std::to_string(mLambertN) + ")");
        if (widget.button("Load Light Field File")) {
            // 触发文件选择对话框
            // loadLightFieldFromFile(selectedFile);
        }
    }

    // 状态信息
    widget.separator();
    widget.text("Status");
    widget.text("Surface Area: " + std::to_string(calculateSurfaceArea()));
    widget.text("Scene Integration: " + (mIsAddedToScene ? "Yes" : "No"));

    if (mCalculationError) {
        widget.text("⚠️ Calculation errors detected!");
    }
}
```

**3. 错误处理**

- UI控件变化时验证参数范围
- 文件加载失败时显示错误信息
- 实时显示计算状态

**4. 验证方法**

- 所有参数在UI中正确显示
- 参数修改立即生效
- 状态信息准确反映当前状态

### 子任务6：Python脚本接口支持

**1. 任务目标**
添加Python脚本接口，支持通过Python创建和控制LED_Emissive

**2. 实现方案**

```cpp
// LED_EmissivePython.cpp - Python绑定
#include "LED_Emissive.h"
#include "Utils/Scripting/Scripting.h"

namespace Falcor {

void LED_Emissive::registerPythonBindings(pybind11::module& m) {
    pybind11::class_<LED_Emissive, ref<LED_Emissive>>(m, "LED_Emissive")
        .def_static("create", &LED_Emissive::create, "name"_a = "LED_Emissive")

        // 基础属性设置
        .def("setShape", &LED_Emissive::setShape)
        .def("setPosition", &LED_Emissive::setPosition)
        .def("setScaling", &LED_Emissive::setScaling)
        .def("setDirection", &LED_Emissive::setDirection)
        .def("setTotalPower", &LED_Emissive::setTotalPower)
        .def("setColor", &LED_Emissive::setColor)

        // 光场分布控制
        .def("setLambertExponent", &LED_Emissive::setLambertExponent)
        .def("setOpeningAngle", &LED_Emissive::setOpeningAngle)
        .def("loadLightFieldFromFile", &LED_Emissive::loadLightFieldFromFile)
        .def("clearLightFieldData", &LED_Emissive::clearLightFieldData)

        // 场景集成
        .def("addToScene", &LED_Emissive::addToScene)
        .def("removeFromScene", &LED_Emissive::removeFromScene)

        // 属性获取
        .def("getShape", &LED_Emissive::getShape)
        .def("getPosition", &LED_Emissive::getPosition)
        .def("getTotalPower", &LED_Emissive::getTotalPower)
        .def("hasCustomLightField", &LED_Emissive::hasCustomLightField);

    // 形状枚举绑定
    pybind11::enum_<LED_EmissiveShape>(m, "LED_EmissiveShape")
        .value("Sphere", LED_EmissiveShape::Sphere)
        .value("Rectangle", LED_EmissiveShape::Rectangle)
        .value("Ellipsoid", LED_EmissiveShape::Ellipsoid);
}

} // namespace Falcor
```

**Python使用示例**：

```python
# create_spherical_led.py
import falcor

def create_spherical_led_at_position(scene, name, position, power, light_profile_file):
    """
    在指定位置创建球形LED灯珠并加载光分布文件

    Args:
        scene: Falcor场景对象
        name: LED名称
        position: [x, y, z] 位置坐标
        power: 功率（瓦特）
        light_profile_file: 光分布文件路径
    """
    try:
        # 创建LED_Emissive对象
        led = falcor.LED_Emissive.create(name)

        # 设置为球形
        led.setShape(falcor.LED_EmissiveShape.Sphere)

        # 设置位置
        led.setPosition(falcor.float3(position[0], position[1], position[2]))

        # 设置功率
        led.setTotalPower(power)

        # 设置缩放（球体半径）
        led.setScaling(falcor.float3(0.01, 0.01, 0.01))  # 1cm半径

        # 加载光分布文件
        if light_profile_file:
            led.loadLightFieldFromFile(light_profile_file)
            print(f"Loaded light profile from: {light_profile_file}")

        # 添加到场景
        led.addToScene(scene)

        print(f"Successfully created spherical LED '{name}' at {position}")
        return led

    except Exception as e:
        print(f"Error creating LED: {e}")
        return None

# 使用示例
def setup_led_array(scene):
    """创建LED阵列示例"""
    leds = []

    # 创建多个LED灯珠
    positions = [
        [0, 0, 5],      # 中心LED
        [1, 0, 5],      # 右侧LED
        [-1, 0, 5],     # 左侧LED
        [0, 1, 5],      # 上方LED
        [0, -1, 5]      # 下方LED
    ]

    for i, pos in enumerate(positions):
        led = create_spherical_led_at_position(
            scene=scene,
            name=f"LED_{i}",
            position=pos,
            power=1.0,  # 1瓦特
            light_profile_file="data/light_profiles/led_profile.csv"
        )

        if led:
            leds.append(led)

    return leds

# 光分布文件格式示例 (led_profile.csv)
"""
# angle(radians), intensity(normalized)
0.0, 1.0
0.1, 0.98
0.2, 0.92
0.3, 0.83
0.4, 0.71
0.5, 0.56
0.6, 0.39
0.7, 0.22
0.8, 0.08
0.9, 0.01
1.0, 0.0
"""
```

**3. 错误处理**

- Python绑定中捕获C++异常
- 参数类型检查和转换
- 文件路径验证
- 详细的Python错误信息

**4. 验证方法**

- Python脚本成功导入falcor模块
- 能够创建LED_Emissive对象
- 文件加载功能正常工作
- 场景集成无错误

**5. 集成到Falcor构建系统**

```cmake
# CMakeLists.txt 添加Python绑定
if(FALCOR_ENABLE_PYTHON)
    target_sources(Falcor PRIVATE
        Scene/Lights/LED_EmissivePython.cpp
    )
endif()
```

这样，您就可以通过Python脚本：

1. ✅ **在指定位置创建球形LED灯珠**
2. ✅ **加载指定的光分布文件**
3. ✅ **LED是球形几何体，但由发光三角形网格构成**

完整的工作流程：Python脚本 → 创建球形LED → 加载光分布 → 生成三角形网格 → 添加到场景作为发光表面

## 总结

LED_Emissive系统在现有LEDLight基础上，通过EmissiveLight集成实现了：

1. **完整的光场分布控制**：支持自定义数据和Lambert分布
2. **光锥角度控制**：0-π范围可调
3. **UI实时交互**：所有参数可在界面中修改
4. **几何可见性**：作为发光三角形支持光线击中
5. **材质系统集成**：利用LightProfile实现角度相关发光

这个系统将为VLC研究提供完整的LED光源建模解决方案。
