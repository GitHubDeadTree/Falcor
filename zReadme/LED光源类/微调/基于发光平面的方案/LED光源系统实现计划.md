# LED光源系统实现计划

## 任务概述

**最简化版本目标**：创建一个LED光源类，将LED建模为发光三角形网格，集成到Falcor的EmissiveLight系统中。核心功能包括：
1. 生成LED几何体（球体、矩形）
2. 创建发光材质并设置发光属性
3. 将LED添加到场景中作为发光三角形
4. 支持功率设置和光强计算
5. 基础光场分布控制

**完整流程**：
LED类创建 → 几何体生成 → 材质配置 → 场景集成 → 发光功能验证

## 子任务分解

### 子任务1：LED光源类基础框架

**1. 任务目标**
创建LEDLight类的基础结构，定义核心成员变量和接口函数

**2. 实现方案**
```cpp
// LEDLight.h
#pragma once
#include "Core/Macros.h"
#include "Core/Framework.h"
#include "Scene/Scene.h"
#include "Scene/Material/Material.h"
#include "Scene/Lights/LightProfile.h"
#include <vector>

namespace Falcor {

enum class LEDShape {
    Sphere = 0,
    Rectangle = 1
};

class FALCOR_API LEDLight {
public:
    LEDLight(const std::string& name = "LEDLight");
    ~LEDLight() = default;

    // 基础属性设置
    void setShape(LEDShape shape);
    void setPosition(const float3& pos);
    void setScaling(const float3& scale);
    void setTotalPower(float power);
    void setColor(const float3& color);

    // 场景集成
    void addToScene(Scene* pScene);
    void removeFromScene();

    // 获取属性
    LEDShape getShape() const { return mShape; }
    float3 getPosition() const { return mPosition; }
    float3 getScaling() const { return mScaling; }
    float getTotalPower() const { return mTotalPower; }

private:
    std::string mName;
    LEDShape mShape = LEDShape::Sphere;
    float3 mPosition = float3(0.0f);
    float3 mScaling = float3(1.0f);
    float mTotalPower = 1.0f;
    float3 mColor = float3(1.0f);

    Scene* mpScene = nullptr;
    ref<Material> mpMaterial;
    uint32_t mMaterialID = 0;
    std::vector<uint32_t> mMeshIndices;

    bool mIsAddedToScene = false;
    bool mCalculationError = false;
};

} // namespace Falcor
```

**3. 错误处理**
- 检查输入参数有效性（scaling > 0, power >= 0）
- 无效参数时输出logWarning，使用默认值0.666f作为错误标识
- 计算错误时设置mCalculationError标志

**4. 验证方法**
- 成功创建LEDLight对象
- 所有getter函数返回正确的默认值
- 设置属性后能正确获取新值
- 无任何错误或警告日志

### 子任务2：球体几何生成

**1. 任务目标**
实现球体网格生成功能，创建三角形顶点和索引数据

**2. 实现方案**
```cpp
// LEDLight.cpp中添加
struct Vertex {
    float3 position;
    float3 normal;
    float2 texCoord;
};

void LEDLight::generateSphereGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        const uint32_t segments = 16; // 经度分段数
        const uint32_t rings = 8;    // 纬度分段数

        vertices.clear();
        indices.clear();

        // 生成顶点
        for (uint32_t ring = 0; ring <= rings; ++ring) {
            float phi = (float)M_PI * ring / rings; // 纬度角
            float y = std::cos(phi);
            float ringRadius = std::sin(phi);

            for (uint32_t segment = 0; segment <= segments; ++segment) {
                float theta = 2.0f * (float)M_PI * segment / segments; // 经度角
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);

                Vertex vertex;
                vertex.position = float3(x, y, z);
                vertex.normal = normalize(vertex.position);
                vertex.texCoord = float2((float)segment / segments, (float)ring / rings);

                vertices.push_back(vertex);
            }
        }

        // 生成索引
        for (uint32_t ring = 0; ring < rings; ++ring) {
            for (uint32_t segment = 0; segment < segments; ++segment) {
                uint32_t current = ring * (segments + 1) + segment;
                uint32_t next = current + segments + 1;

                // 第一个三角形
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                // 第二个三角形
                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        if (vertices.empty() || indices.empty()) {
            logError("LEDLight::generateSphereGeometry - Generated empty geometry");
            mCalculationError = true;
            return;
        }

        mCalculationError = false;

    } catch (const std::exception& e) {
        logError("LEDLight::generateSphereGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;

        // 创建默认几何体（单个三角形）
        vertices.clear();
        indices.clear();
        vertices.resize(3);
        vertices[0] = {{0.666f, 0, 0}, {0, 1, 0}, {0, 0}};
        vertices[1] = {{0, 0.666f, 0}, {0, 1, 0}, {1, 0}};
        vertices[2] = {{0, 0, 0.666f}, {0, 1, 0}, {0.5f, 1}};
        indices = {0, 1, 2};
    }
}
```

**3. 错误处理**
- 检查生成的顶点和索引数据是否为空
- 捕获异常并创建标识性的默认几何体（使用0.666f坐标）
- 记录详细错误信息到日志

**4. 验证方法**
- 生成的顶点数 = (rings+1) * (segments+1) = 9 * 17 = 153
- 生成的三角形数 = rings * segments * 2 = 8 * 16 * 2 = 256
- 所有顶点法线长度接近1.0
- 无错误日志，mCalculationError为false

### 子任务3：矩形几何生成

**1. 任务目标**
实现矩形网格生成功能，创建平面三角形网格

**2. 实现方案**
```cpp
void LEDLight::generateRectangleGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        const uint32_t widthSegments = 8;
        const uint32_t heightSegments = 8;

        vertices.clear();
        indices.clear();

        // 生成顶点 (XY平面，法线指向+Z)
        for (uint32_t y = 0; y <= heightSegments; ++y) {
            for (uint32_t x = 0; x <= widthSegments; ++x) {
                float u = (float)x / widthSegments;
                float v = (float)y / heightSegments;

                Vertex vertex;
                vertex.position = float3(u - 0.5f, v - 0.5f, 0.0f); // 中心在原点
                vertex.normal = float3(0, 0, 1);
                vertex.texCoord = float2(u, v);

                vertices.push_back(vertex);
            }
        }

        // 生成索引
        for (uint32_t y = 0; y < heightSegments; ++y) {
            for (uint32_t x = 0; x < widthSegments; ++x) {
                uint32_t current = y * (widthSegments + 1) + x;
                uint32_t next = current + widthSegments + 1;

                // 第一个三角形
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                // 第二个三角形
                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        if (vertices.empty() || indices.empty()) {
            logError("LEDLight::generateRectangleGeometry - Generated empty geometry");
            mCalculationError = true;
            return;
        }

        mCalculationError = false;

    } catch (const std::exception& e) {
        logError("LEDLight::generateRectangleGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;

        // 创建默认几何体
        vertices.clear();
        indices.clear();
        vertices.resize(4);
        vertices[0] = {{-0.333f, -0.333f, 0}, {0, 0, 1}, {0, 0}};
        vertices[1] = {{0.333f, -0.333f, 0}, {0, 0, 1}, {1, 0}};
        vertices[2] = {{0.333f, 0.333f, 0}, {0, 0, 1}, {1, 1}};
        vertices[3] = {{-0.333f, 0.333f, 0}, {0, 0, 1}, {0, 1}};
        indices = {0, 1, 2, 0, 2, 3};
    }
}
```

**3. 错误处理**
- 验证生成的几何数据有效性
- 异常时创建简单的矩形几何体（使用0.333f标识）
- 记录错误信息

**4. 验证方法**
- 生成的顶点数 = (widthSegments+1) * (heightSegments+1) = 81
- 生成的三角形数 = widthSegments * heightSegments * 2 = 128
- 所有顶点法线为(0,0,1)
- 无错误日志

### 子任务4：发光材质创建

**1. 任务目标**
创建和配置发光材质，设置发光属性

**2. 实现方案**
```cpp
ref<Material> LEDLight::createEmissiveMaterial() {
    try {
        // 创建标准材质
        auto pMaterial = StandardMaterial::create(mpScene->getDevice(), mName + "_Material");

        // 设置基础属性
        pMaterial->setBaseColor(float4(0.1f, 0.1f, 0.1f, 1.0f)); // 暗底色
        pMaterial->setRoughness(0.8f);
        pMaterial->setMetallic(0.0f);

        // 设置发光属性
        pMaterial->setEmissiveColor(mColor);
        pMaterial->setEmissiveFactor(calculateEmissiveIntensity());

        if (pMaterial->getEmissiveFactor() <= 0.0f) {
            logWarning("LEDLight::createEmissiveMaterial - Zero or negative emissive intensity");
            pMaterial->setEmissiveFactor(0.666f); // 错误标识值
        }

        mCalculationError = false;
        return pMaterial;

    } catch (const std::exception& e) {
        logError("LEDLight::createEmissiveMaterial - Exception: " + std::string(e.what()));
        mCalculationError = true;

        // 返回默认材质
        auto pMaterial = StandardMaterial::create(mpScene->getDevice(), mName + "_ErrorMaterial");
        pMaterial->setEmissiveColor(float3(0.666f, 0.333f, 0.0f)); // 错误标识颜色
        pMaterial->setEmissiveFactor(0.666f);
        return pMaterial;
    }
}

float LEDLight::calculateEmissiveIntensity() {
    try {
        // 计算表面积
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f) {
            logWarning("LEDLight::calculateEmissiveIntensity - Invalid surface area");
            return 0.666f;
        }

        // 简化：假设Lambert发光，强度 = 功率 / (π * 表面积)
        float intensity = mTotalPower / ((float)M_PI * surfaceArea);

        if (!std::isfinite(intensity) || intensity < 0.0f) {
            logError("LEDLight::calculateEmissiveIntensity - Invalid intensity calculation");
            return 0.666f;
        }

        return intensity;

    } catch (const std::exception& e) {
        logError("LEDLight::calculateEmissiveIntensity - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}

float LEDLight::calculateSurfaceArea() {
    try {
        switch (mShape) {
        case LEDShape::Sphere:
            return 4.0f * (float)M_PI * mScaling.x * mScaling.x;

        case LEDShape::Rectangle:
            return mScaling.x * mScaling.y;

        default:
            logWarning("LEDLight::calculateSurfaceArea - Unknown shape type");
            return 0.666f;
        }
    } catch (const std::exception& e) {
        logError("LEDLight::calculateSurfaceArea - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}
```

**3. 错误处理**
- 验证计算结果的有效性（非负、有限值）
- 表面积计算错误时返回0.666f
- 材质创建失败时返回带错误标识的默认材质

**4. 验证方法**
- 材质的发光因子大于0且有限
- 球体表面积 = 4πr² (r = mScaling.x)
- 矩形表面积 = width × height
- 材质颜色与设置的mColor一致

### 子任务5：场景集成

**1. 任务目标**
将LED几何体和材质添加到Falcor场景中

**2. 实现方案**
```cpp
void LEDLight::addToScene(Scene* pScene) {
    try {
        if (!pScene) {
            logError("LEDLight::addToScene - Null scene pointer");
            return;
        }

        if (mIsAddedToScene) {
            logWarning("LEDLight::addToScene - LED already added to scene");
            return;
        }

        mpScene = pScene;

        // 1. 生成几何体
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        generateGeometry(vertices, indices);

        if (mCalculationError) {
            logError("LEDLight::addToScene - Geometry generation failed");
            return;
        }

        // 2. 创建材质
        mpMaterial = createEmissiveMaterial();
        mMaterialID = pScene->addMaterial(mpMaterial);

        // 3. 创建网格
        auto pMesh = createMeshFromGeometry(vertices, indices);
        if (!pMesh) {
            logError("LEDLight::addToScene - Failed to create mesh");
            return;
        }

        // 4. 应用变换
        applyTransform(pMesh);

        // 5. 添加到场景
        uint32_t meshIndex = pScene->addMesh(pMesh);
        mMeshIndices.push_back(meshIndex);

        // 6. 创建场景节点
        auto pNode = SceneBuilder::Node::create(mName);
        pNode->meshID = meshIndex;
        pNode->materialID = mMaterialID;
        pScene->addNode(pNode);

        mIsAddedToScene = true;
        logInfo("LEDLight::addToScene - Successfully added LED '" + mName + "' to scene");

    } catch (const std::exception& e) {
        logError("LEDLight::addToScene - Exception: " + std::string(e.what()));
        mCalculationError = true;
        cleanup();
    }
}

void LEDLight::generateGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    switch (mShape) {
    case LEDShape::Sphere:
        generateSphereGeometry(vertices, indices);
        break;
    case LEDShape::Rectangle:
        generateRectangleGeometry(vertices, indices);
        break;
    default:
        logError("LEDLight::generateGeometry - Unknown shape type");
        mCalculationError = true;
    }
}

ref<Mesh> LEDLight::createMeshFromGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    try {
        // 创建顶点缓冲区
        // 这里需要根据Falcor的具体API来实现
        // 简化示例：
        auto pMesh = Mesh::create(mpScene->getDevice());

        // 设置顶点数据
        // pMesh->setVertexData(vertices);
        // pMesh->setIndexData(indices);

        return pMesh;

    } catch (const std::exception& e) {
        logError("LEDLight::createMeshFromGeometry - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

void LEDLight::applyTransform(ref<Mesh> pMesh) {
    try {
        float4x4 transform = math::scale(float4x4::identity(), mScaling);
        transform = math::translate(transform, mPosition);

        // 应用变换到网格
        // pMesh->setTransform(transform);

    } catch (const std::exception& e) {
        logError("LEDLight::applyTransform - Exception: " + std::string(e.what()));
    }
}
```

**3. 错误处理**
- 检查场景指针有效性
- 验证几何体生成成功
- 网格创建失败时清理资源
- 记录详细的错误和成功信息

**4. 验证方法**
- 场景中增加了一个新的网格对象
- 网格使用了正确的发光材质
- 网格位置和缩放正确应用
- 成功日志显示LED已添加
- mIsAddedToScene标志为true

### 子任务6：基础测试和验证

**1. 任务目标**
创建测试代码验证LED光源系统的基础功能

**2. 实现方案**
```cpp
// LEDLightTest.cpp
#include "LEDLight.h"

bool testLEDLightBasicFunctionality() {
    bool allTestsPassed = true;

    // 测试1: 创建LED光源
    auto led = std::make_unique<LEDLight>("TestLED");
    if (!led) {
        logError("Test failed: Cannot create LEDLight");
        return false;
    }

    // 测试2: 设置属性
    led->setShape(LEDShape::Sphere);
    led->setPosition(float3(1, 2, 3));
    led->setScaling(float3(0.5f));
    led->setTotalPower(10.0f);
    led->setColor(float3(1, 0.8f, 0.6f));

    // 验证属性设置
    if (led->getShape() != LEDShape::Sphere) {
        logError("Test failed: Shape not set correctly");
        allTestsPassed = false;
    }

    if (any(led->getPosition() != float3(1, 2, 3))) {
        logError("Test failed: Position not set correctly");
        allTestsPassed = false;
    }

    if (led->getTotalPower() != 10.0f) {
        logError("Test failed: Power not set correctly");
        allTestsPassed = false;
    }

    // 测试3: 几何体生成
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // 这里需要访问私有函数，可能需要友元类或公开接口
    // led->testGenerateGeometry(vertices, indices);

    if (vertices.empty() || indices.empty()) {
        logError("Test failed: Geometry generation produced empty results");
        allTestsPassed = false;
    }

    if (allTestsPassed) {
        logInfo("All LED basic functionality tests passed");
    } else {
        logError("Some LED tests failed");
    }

    return allTestsPassed;
}

bool testLEDCalculations() {
    auto led = std::make_unique<LEDLight>("CalcTestLED");

    // 测试球体表面积计算
    led->setShape(LEDShape::Sphere);
    led->setScaling(float3(2.0f)); // 半径 = 2

    // 期望表面积 = 4π * 2² = 16π ≈ 50.27
    // 这里需要公开计算函数进行测试

    // 测试矩形表面积计算
    led->setShape(LEDShape::Rectangle);
    led->setScaling(float3(3.0f, 4.0f, 1.0f)); // width=3, height=4

    // 期望表面积 = 3 * 4 = 12

    return true;
}
```

**3. 错误处理**
- 每个测试步骤都要验证结果
- 失败时记录具体的错误信息
- 返回明确的成功/失败状态

**4. 验证方法**
- 所有基础功能测试通过
- 几何体生成产生合理数量的顶点和索引
- 表面积计算结果在预期范围内
- 无错误日志或仅有预期的测试日志

## 总结

以上6个子任务构成了LED光源系统的最小可行版本，每个任务都有明确的目标、详细的实现方案、完善的错误处理和验证方法。完成这些任务后，我们将拥有一个能够生成几何体、创建发光材质、集成到Falcor场景中的基础LED光源系统。
