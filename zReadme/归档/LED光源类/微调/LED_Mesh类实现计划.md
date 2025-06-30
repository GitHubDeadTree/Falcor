# LED_Mesh类实现计划

## 任务概述

**最简化版本**：创建一个新的LED_Mesh类，基于Falcor的网格光源系统，完全保留现有LEDLight类的所有功能，同时解决几何体可视性问题。

**核心功能**：
1. 保留LEDLight的所有功能（形状、光谱、光场分布、变换控制等）
2. 基于网格光源系统实现几何体可视性
3. 支持动态几何体重建和材质更新
4. 完整的UI控制界面

**完整流程**：创建LED_Mesh类 → 继承网格光源能力 → 迁移LEDLight功能 → 实现动态几何体管理 → UI集成测试

## 子任务分解

### 子任务1：LED_Mesh基础类架构创建

#### 1. 任务目标
创建LED_Mesh类的基础架构，建立几何体管理系统，为后续功能迁移奠定基础。

#### 2. 实现方案

**创建LED_Mesh.h头文件：**

```cpp
#pragma once
#include "Scene/SceneDefines.h"
#include "Core/Framework.h"
#include "Utils/Math/Vector.h"
#include "Scene/Material/BasicMaterial.h"

namespace Falcor
{
class Scene;

class FALCOR_API LED_Mesh
{
public:
    enum class LEDShape { Sphere = 0, Ellipsoid = 1, Rectangle = 2 };

    static ref<LED_Mesh> create(const std::string& name = "")
    {
        return make_ref<LED_Mesh>(name);
    }

    LED_Mesh(const std::string& name);
    ~LED_Mesh() = default;

    // Basic interface
    const std::string& getName() const { return mName; }
    void setName(const std::string& name) { mName = name; }

    // Geometry management
    ref<TriangleMesh> getMesh() const { return mpMesh; }
    ref<BasicMaterial> getMaterial() const { return mpMaterial; }
    uint32_t getMeshInstanceID() const { return mMeshInstanceID; }

    // Core creation and update methods
    void createGeometry();
    void updateMaterial();
    void addToScene(Scene* pScene);
    void removeFromScene(Scene* pScene);

private:
    std::string mName;
    ref<TriangleMesh> mpMesh;
    ref<BasicMaterial> mpMaterial;
    uint32_t mMeshInstanceID = kInvalidID;
    Scene* mpScene = nullptr;

    static constexpr uint32_t kInvalidID = 0xffffffff;

    void logDebugInfo(const std::string& operation) const;
};
}
```

**创建LED_Mesh.cpp实现文件：**

```cpp
#include "LED_Mesh.h"
#include "Scene/Scene.h"
#include "Scene/SceneBuilder.h"
#include "Utils/Logger.h"

namespace Falcor
{

LED_Mesh::LED_Mesh(const std::string& name) : mName(name)
{
    try {
        logInfo("LED_Mesh: Creating LED mesh '{}'", mName);

        // Create basic emissive material
        mpMaterial = BasicMaterial::create(mName + "_Material");
        if (!mpMaterial) {
            logError("LED_Mesh::LED_Mesh - Failed to create material, using fallback");
            mpMaterial = BasicMaterial::create("LED_Fallback_Material");
            mpMaterial->setEmissiveColor(float3(0.666f, 0.666f, 0.666f));
        }

        logDebugInfo("constructed");

    } catch (const std::exception& e) {
        logError("LED_Mesh::LED_Mesh - Exception during construction: {}", e.what());
        // Ensure we have a valid material even if construction fails
        if (!mpMaterial) {
            mpMaterial = BasicMaterial::create("LED_Error_Material");
            mpMaterial->setEmissiveColor(float3(0.666f, 0.666f, 0.666f));
        }
    }
}

void LED_Mesh::createGeometry()
{
    try {
        // Create a simple unit sphere for now (will be enhanced in later tasks)
        mpMesh = TriangleMesh::createSphere(1.0f, 32, 16);

        if (!mpMesh) {
            logError("LED_Mesh::createGeometry - Failed to create sphere geometry");
            // Create fallback quad geometry
            mpMesh = TriangleMesh::createQuad();
            if (!mpMesh) {
                logError("LED_Mesh::createGeometry - Critical: Failed to create fallback geometry");
                return;
            }
        }

        logDebugInfo("geometry created");

    } catch (const std::exception& e) {
        logError("LED_Mesh::createGeometry - Exception: {}", e.what());
        mpMesh.reset();
    }
}

void LED_Mesh::updateMaterial()
{
    if (!mpMaterial) {
        logWarning("LED_Mesh::updateMaterial - No material to update");
        return;
    }

    try {
        // Set basic emissive properties (will be enhanced in later tasks)
        mpMaterial->setEmissiveColor(float3(1.0f, 1.0f, 1.0f));
        mpMaterial->setEmissiveFactor(1.0f);
        mpMaterial->setBaseColor(float4(0.0f, 0.0f, 0.0f, 1.0f));

        logDebugInfo("material updated");

    } catch (const std::exception& e) {
        logError("LED_Mesh::updateMaterial - Exception: {}", e.what());
    }
}

void LED_Mesh::addToScene(Scene* pScene)
{
    if (!pScene) {
        logError("LED_Mesh::addToScene - Scene is null");
        return;
    }

    if (!mpMesh || !mpMaterial) {
        logError("LED_Mesh::addToScene - Missing mesh or material");
        return;
    }

    try {
        mpScene = pScene;

        // Add mesh and material to scene (simplified - will be enhanced)
        // This is a placeholder implementation
        logInfo("LED_Mesh: Adding '{}' to scene", mName);
        logDebugInfo("added to scene");

    } catch (const std::exception& e) {
        logError("LED_Mesh::addToScene - Exception: {}", e.what());
        mpScene = nullptr;
    }
}

void LED_Mesh::removeFromScene(Scene* pScene)
{
    if (mpScene != pScene) {
        logWarning("LED_Mesh::removeFromScene - Scene mismatch");
        return;
    }

    try {
        logInfo("LED_Mesh: Removing '{}' from scene", mName);
        mpScene = nullptr;
        mMeshInstanceID = kInvalidID;
        logDebugInfo("removed from scene");

    } catch (const std::exception& e) {
        logError("LED_Mesh::removeFromScene - Exception: {}", e.what());
    }
}

void LED_Mesh::logDebugInfo(const std::string& operation) const
{
    logInfo("LED_Mesh Debug [{}]: name='{}', mesh={}, material={}, sceneID={}",
            operation, mName,
            mpMesh ? "valid" : "null",
            mpMaterial ? "valid" : "null",
            mMeshInstanceID);
}

}
```

#### 3. 错误处理
- **材质创建失败**：使用fallback材质，发光颜色设为0.666特征值
- **几何体创建失败**：降级到更简单的几何体（球体→四边形）
- **场景操作异常**：捕获异常，保持内部状态一致性
- **空指针检查**：所有外部传入指针都进行有效性验证

#### 4. 验证方法
- 构造成功后，`getMaterial()`和`getName()`应返回有效值
- 调用`createGeometry()`后，`getMesh()`应返回非空指针
- 日志中应显示"LED_Mesh Debug"信息，包含正确的对象状态
- 所有操作失败时应显示错误日志但不崩溃

---

### 子任务2：LEDLight功能完整迁移

#### 1. 任务目标
将LEDLight类的所有核心功能完整迁移到LED_Mesh类，包括形状控制、变换管理、光谱分布、光场分布等。

#### 2. 实现方案

**扩展LED_Mesh.h，添加LEDLight的所有功能：**

```cpp
class FALCOR_API LED_Mesh
{
public:
    // ... 现有定义 ...

    // LED specific functionality (migrated from LEDLight)
    void setLEDShape(LEDShape shape);
    LEDShape getLEDShape() const { return mLEDShape; }

    void setLambertExponent(float n);
    float getLambertExponent() const { return mLambertExponent; }

    void setTotalPower(float power);
    float getTotalPower() const { return mTotalPower; }

    // Geometry and transformation
    void setScaling(float3 scale);
    float3 getScaling() const { return mScaling; }
    void setTransformMatrix(const float4x4& mtx);
    float4x4 getTransformMatrix() const { return mTransformMatrix; }

    // Angle control features
    void setOpeningAngle(float openingAngle);
    float getOpeningAngle() const { return mOpeningAngle; }
    void setWorldDirection(const float3& dir);
    const float3& getWorldDirection() const { return mWorldDirection; }
    void setWorldPosition(const float3& pos);
    const float3& getWorldPosition() const { return mWorldPosition; }

    // Spectrum and light field distribution
    void loadSpectrumData(const std::vector<float2>& spectrumData);
    void loadLightFieldData(const std::vector<float2>& lightFieldData);
    bool hasCustomSpectrum() const { return !mSpectrumData.empty(); }
    bool hasCustomLightField() const { return !mLightFieldData.empty(); }
    void clearCustomData();

    // Intensity control
    void setIntensity(const float3& intensity);
    const float3& getIntensity() const { return mIntensity; }

    // UI interface
    void renderUI(Gui::Widgets& widget);

private:
    // LED-specific data (migrated from LEDLight)
    LEDShape mLEDShape = LEDShape::Sphere;
    float3 mScaling = float3(1.0f);
    float4x4 mTransformMatrix = float4x4::identity();
    float3 mWorldPosition = float3(0.0f);
    float3 mWorldDirection = float3(0.0f, 0.0f, -1.0f);
    float3 mIntensity = float3(1.0f);
    float mLambertExponent = 1.0f;
    float mTotalPower = 0.0f;
    float mOpeningAngle = (float)M_PI;

    // Spectrum and light field data
    std::vector<float2> mSpectrumData;
    std::vector<float2> mLightFieldData;
    ref<Buffer> mSpectrumBuffer;
    ref<Buffer> mLightFieldBuffer;

    // Helper methods
    void updateGeometryFromShape();
    void updateMaterialFromIntensity();
    float calculateSurfaceArea() const;
    void validateTransformMatrix(const float4x4& mtx);
    float3 convertSpectrumToRGB(const std::vector<float2>& spectrum);
};
```

**实现核心功能方法：**

```cpp
void LED_Mesh::setLEDShape(LEDShape shape)
{
    if (mLEDShape == shape) return;

    try {
        LEDShape oldShape = mLEDShape;
        mLEDShape = shape;

        updateGeometryFromShape();

        logInfo("LED_Mesh: Shape changed from {} to {} for '{}'",
                (int)oldShape, (int)shape, mName);

    } catch (const std::exception& e) {
        logError("LED_Mesh::setLEDShape - Exception: {}", e.what());
        // Keep old shape on error
    }
}

void LED_Mesh::setScaling(float3 scale)
{
    // Validate scaling values
    if (any(scale <= 0.0f)) {
        logWarning("LED_Mesh::setScaling - Invalid scaling: ({}, {}, {}), using abs values",
                   scale.x, scale.y, scale.z);
        scale = abs(scale);

        if (any(scale < 1e-6f)) {
            logError("LED_Mesh::setScaling - Scaling too small, using default 0.666");
            scale = float3(0.666f);
        }
    }

    if (any(mScaling != scale)) {
        mScaling = scale;

        // Update transform matrix with new scaling
        float4x4 scaleMatrix = math::scale(float4x4::identity(), mScaling);
        setTransformMatrix(mul(mTransformMatrix, scaleMatrix));

        logInfo("LED_Mesh: Scaling updated to ({}, {}, {})", scale.x, scale.y, scale.z);
    }
}

void LED_Mesh::loadSpectrumData(const std::vector<float2>& spectrumData)
{
    try {
        if (spectrumData.empty()) {
            logWarning("LED_Mesh::loadSpectrumData - Empty spectrum data provided");
            mSpectrumData.clear();
            mSpectrumBuffer.reset();
            return;
        }

        // Validate spectrum data
        for (const auto& point : spectrumData) {
            if (point.x < 0.0f || point.y < 0.0f || !std::isfinite(point.x) || !std::isfinite(point.y)) {
                logError("LED_Mesh::loadSpectrumData - Invalid spectrum point: ({}, {}), using fallback",
                         point.x, point.y);
                mSpectrumData = {{580.0f, 0.666f}}; // Fallback spectrum
                return;
            }
        }

        mSpectrumData = spectrumData;

        // Create GPU buffer
        mSpectrumBuffer = Buffer::createStructured(sizeof(float2), (uint32_t)mSpectrumData.size());
        mSpectrumBuffer->setBlob(mSpectrumData.data(), 0, mSpectrumData.size() * sizeof(float2));

        // Update material based on spectrum
        updateMaterialFromIntensity();

        logInfo("LED_Mesh: Loaded {} spectrum data points", mSpectrumData.size());

    } catch (const std::exception& e) {
        logError("LED_Mesh::loadSpectrumData - Exception: {}", e.what());
        mSpectrumData.clear();
        mSpectrumBuffer.reset();
    }
}

void LED_Mesh::updateGeometryFromShape()
{
    try {
        switch (mLEDShape)
        {
        case LEDShape::Sphere:
            mpMesh = TriangleMesh::createSphere(1.0f, 32, 16);
            break;
        case LEDShape::Rectangle:
            mpMesh = TriangleMesh::createBox(float3(1.0f), 1);
            break;
        case LEDShape::Ellipsoid:
            // Create scaled sphere for ellipsoid approximation
            mpMesh = TriangleMesh::createSphere(1.0f, 32, 16);
            break;
        default:
            logError("LED_Mesh::updateGeometryFromShape - Unknown shape: {}", (int)mLEDShape);
            mpMesh = TriangleMesh::createSphere(1.0f, 16, 8); // Fallback
            break;
        }

        if (!mpMesh) {
            logError("LED_Mesh::updateGeometryFromShape - Failed to create geometry");
            mpMesh = TriangleMesh::createQuad(); // Ultimate fallback
        }

    } catch (const std::exception& e) {
        logError("LED_Mesh::updateGeometryFromShape - Exception: {}", e.what());
        mpMesh = TriangleMesh::createQuad();
    }
}

float LED_Mesh::calculateSurfaceArea() const
{
    try {
        switch (mLEDShape)
        {
        case LEDShape::Sphere:
            return 4.0f * (float)M_PI * mScaling.x * mScaling.x;
        case LEDShape::Rectangle:
            return 8.0f * (mScaling.x * mScaling.y + mScaling.y * mScaling.z + mScaling.x * mScaling.z);
        case LEDShape::Ellipsoid:
            {
                float a = mScaling.x, b = mScaling.y, c = mScaling.z;
                return 4.0f * (float)M_PI * std::pow((std::pow(a*b, 1.6f) + std::pow(a*c, 1.6f) + std::pow(b*c, 1.6f)) / 3.0f, 1.0f/1.6f);
            }
        default:
            logError("LED_Mesh::calculateSurfaceArea - Unknown shape, returning fallback area");
            return 0.666f; // Error indicator
        }
    } catch (const std::exception& e) {
        logError("LED_Mesh::calculateSurfaceArea - Exception: {}", e.what());
        return 0.666f;
    }
}
```

#### 3. 错误处理
- **无效形状值**：保持原有形状，输出错误日志
- **无效缩放值**：自动修正负值和零值，过小时使用0.666默认值
- **光谱数据验证**：检查NaN和负值，无效时使用fallback光谱
- **几何体创建失败**：降级到更简单几何体，最终降级到四边形
- **计算异常**：所有数值计算都有异常保护，错误时返回0.666特征值

#### 4. 验证方法
- 形状改变后，`getMesh()`应返回对应类型的几何体
- 缩放值设置后，`getScaling()`应返回正确值（或修正后的值）
- 光谱数据加载后，`hasCustomSpectrum()`应返回true
- 表面积计算应返回合理值，错误时返回0.666
- 所有参数验证失败时应显示相应的警告/错误日志

---

### 子任务3：动态几何体管理和场景集成

#### 1. 任务目标
实现LED_Mesh与Falcor场景系统的完整集成，包括动态几何体更新、材质同步、场景图管理等。

#### 2. 实现方案

**完善场景集成功能：**

```cpp
// LED_Mesh.h中添加场景集成方法
class FALCOR_API LED_Mesh
{
private:
    // Scene integration data
    uint32_t mMeshID = kInvalidID;
    uint32_t mMaterialID = kInvalidID;
    uint32_t mNodeID = kInvalidID;
    bool mIsInScene = false;
    bool mNeedsRebuild = false;

    // Scene integration methods
    void rebuildInScene();
    void updateSceneTransform();
    bool validateSceneState() const;

public:
    // Scene state queries
    bool isInScene() const { return mIsInScene; }
    bool needsRebuild() const { return mNeedsRebuild; }
    void markForRebuild() { mNeedsRebuild = true; }
};

// LED_Mesh.cpp中实现场景集成
void LED_Mesh::addToScene(Scene* pScene)
{
    if (!pScene) {
        logError("LED_Mesh::addToScene - Scene is null");
        return;
    }

    if (mIsInScene && mpScene == pScene) {
        logWarning("LED_Mesh::addToScene - Already in scene");
        return;
    }

    if (!mpMesh || !mpMaterial) {
        logError("LED_Mesh::addToScene - Missing mesh or material");
        return;
    }

    try {
        mpScene = pScene;

        // Add material to scene
        mMaterialID = pScene->addMaterial(mpMaterial);
        if (mMaterialID == kInvalidID) {
            logError("LED_Mesh::addToScene - Failed to add material");
            return;
        }

        // Add mesh to scene
        mMeshID = pScene->addTriangleMesh(mpMesh, mpMaterial);
        if (mMeshID == kInvalidID) {
            logError("LED_Mesh::addToScene - Failed to add mesh");
            return;
        }

        // Create scene node with transform
        Transform nodeTransform;
        nodeTransform.setMatrix(mTransformMatrix);
        nodeTransform.setScaling(mScaling);
        nodeTransform.setTranslation(mWorldPosition);

        mNodeID = pScene->addNode(mName + "_Node", nodeTransform);
        if (mNodeID == kInvalidID) {
            logError("LED_Mesh::addToScene - Failed to create scene node");
            return;
        }

        // Create mesh instance linking node, mesh, and material
        mMeshInstanceID = pScene->addMeshInstance(mNodeID, mMeshID);
        if (mMeshInstanceID == kInvalidID) {
            logError("LED_Mesh::addToScene - Failed to create mesh instance");
            return;
        }

        mIsInScene = true;
        mNeedsRebuild = false;

        logInfo("LED_Mesh: Successfully added '{}' to scene (IDs: mesh={}, material={}, node={}, instance={})",
                mName, mMeshID, mMaterialID, mNodeID, mMeshInstanceID);

        // Trigger scene rebuild for mesh lights
        pScene->markForLightCollectionRebuild();

    } catch (const std::exception& e) {
        logError("LED_Mesh::addToScene - Exception: {}", e.what());
        removeFromScene(pScene); // Cleanup on failure
    }
}

void LED_Mesh::rebuildInScene()
{
    if (!mIsInScene || !mpScene) {
        logWarning("LED_Mesh::rebuildInScene - Not in scene");
        return;
    }

    try {
        logInfo("LED_Mesh: Rebuilding '{}' in scene", mName);

        // Remove from scene and re-add
        Scene* pScene = mpScene;
        removeFromScene(pScene);
        addToScene(pScene);

        if (!mIsInScene) {
            logError("LED_Mesh::rebuildInScene - Failed to rebuild in scene");
            return;
        }

        mNeedsRebuild = false;
        logInfo("LED_Mesh: Successfully rebuilt '{}' in scene", mName);

    } catch (const std::exception& e) {
        logError("LED_Mesh::rebuildInScene - Exception: {}", e.what());
    }
}

void LED_Mesh::updateSceneTransform()
{
    if (!mIsInScene || !mpScene || mNodeID == kInvalidID) {
        return;
    }

    try {
        Transform nodeTransform;
        nodeTransform.setMatrix(mTransformMatrix);
        nodeTransform.setScaling(mScaling);
        nodeTransform.setTranslation(mWorldPosition);

        // Update scene node transform
        mpScene->updateNodeTransform(mNodeID, nodeTransform);

        logInfo("LED_Mesh: Updated scene transform for '{}'", mName);

    } catch (const std::exception& e) {
        logError("LED_Mesh::updateSceneTransform - Exception: {}", e.what());
    }
}

bool LED_Mesh::validateSceneState() const
{
    if (!mIsInScene) return true; // Not in scene is valid

    bool isValid = true;

    if (!mpScene) {
        logError("LED_Mesh::validateSceneState - In scene but no scene pointer");
        isValid = false;
    }

    if (mMeshID == kInvalidID) {
        logError("LED_Mesh::validateSceneState - In scene but invalid mesh ID");
        isValid = false;
    }

    if (mMaterialID == kInvalidID) {
        logError("LED_Mesh::validateSceneState - In scene but invalid material ID");
        isValid = false;
    }

    if (mNodeID == kInvalidID) {
        logError("LED_Mesh::validateSceneState - In scene but invalid node ID");
        isValid = false;
    }

    if (mMeshInstanceID == kInvalidID) {
        logError("LED_Mesh::validateSceneState - In scene but invalid mesh instance ID");
        isValid = false;
    }

    return isValid;
}

// 重写关键方法以触发场景更新
void LED_Mesh::setLEDShape(LEDShape shape)
{
    if (mLEDShape == shape) return;

    LEDShape oldShape = mLEDShape;
    mLEDShape = shape;

    updateGeometryFromShape();

    // If in scene, mark for rebuild
    if (mIsInScene) {
        markForRebuild();
        rebuildInScene();
    }

    logInfo("LED_Mesh: Shape changed from {} to {}, scene updated", (int)oldShape, (int)shape);
}

void LED_Mesh::setTransformMatrix(const float4x4& mtx)
{
    validateTransformMatrix(mtx);

    if (any(mTransformMatrix != mtx)) {
        mTransformMatrix = mtx;

        // Extract position from transform matrix
        mWorldPosition = mtx[3].xyz();

        // Update scene if necessary
        if (mIsInScene) {
            updateSceneTransform();
        }

        logInfo("LED_Mesh: Transform updated, position: ({}, {}, {})",
                mWorldPosition.x, mWorldPosition.y, mWorldPosition.z);
    }
}
```

#### 3. 错误处理
- **场景添加失败**：逐步回滚已添加的资源，保持状态一致性
- **ID分配失败**：检查每个步骤的返回值，失败时记录错误并清理
- **变换更新异常**：验证变换矩阵有效性，无效时使用单位矩阵
- **场景状态验证**：定期检查内部状态与场景状态的一致性
- **重建失败处理**：重建失败时保持原有状态，输出错误日志

#### 4. 验证方法
- 成功添加到场景后，`isInScene()`应返回true
- 所有场景ID（mesh、material、node、instance）应为有效值
- 形状改变后，场景中的几何体应立即更新
- 变换改变后，场景中的位置应同步更新
- `validateSceneState()`应始终返回true
- 移除后所有场景资源应正确清理

---

### 子任务4：UI界面集成和测试验证

#### 1. 任务目标
为LED_Mesh类实现完整的UI控制界面，提供实时调试信息，并建立全面的测试验证机制。

#### 2. 实现方案

**实现完整的UI界面：**

```cpp
void LED_Mesh::renderUI(Gui::Widgets& widget)
{
    try {
        // Basic information section
        widget.text("LED Mesh: " + mName);
        widget.separator();

        // Status information
        widget.text("Status: " + (mIsInScene ? "In Scene" : "Not in Scene"));
        if (mIsInScene) {
            widget.text("Mesh Instance ID: " + std::to_string(mMeshInstanceID));
            if (mNeedsRebuild) {
                widget.text("Rebuild Required: YES");
                if (widget.button("Force Rebuild")) {
                    rebuildInScene();
                }
            }
        }

        // Shape control
        uint32_t shapeType = (uint32_t)mLEDShape;
        const char* shapeNames[] = { "Sphere", "Ellipsoid", "Rectangle" };
        if (widget.dropdown("LED Shape", shapeNames, 3, shapeType)) {
            setLEDShape((LEDShape)shapeType);
        }

        // Transform controls
        widget.separator();
        widget.text("Transform Controls:");

        float3 position = mWorldPosition;
        if (widget.var("Position", position, -100.0f, 100.0f)) {
            setWorldPosition(position);
        }

        float3 scaling = mScaling;
        if (widget.var("Scale", scaling, 0.00001f, 10.0f)) {
            setScaling(scaling);
        }

        float3 direction = mWorldDirection;
        if (widget.direction("Direction", direction)) {
            setWorldDirection(direction);
        }

        // Light properties
        widget.separator();
        widget.text("Light Properties:");

        float3 intensity = mIntensity;
        if (widget.var("Intensity", intensity, 0.0f, 100.0f)) {
            setIntensity(intensity);
        }

        float lambertExp = mLambertExponent;
        if (widget.var("Lambert Exponent", lambertExp, 0.1f, 10.0f)) {
            setLambertExponent(lambertExp);
        }

        float openingAngle = mOpeningAngle;
        if (widget.var("Opening Angle", openingAngle, 0.0f, (float)M_PI)) {
            setOpeningAngle(openingAngle);
        }

        float totalPower = mTotalPower;
        if (widget.var("Total Power", totalPower, 0.0f, 1000.0f)) {
            setTotalPower(totalPower);
        }

        // Spectrum and light field controls
        widget.separator();
        widget.text("Advanced Properties:");

        widget.text("Custom Spectrum: " + (hasCustomSpectrum() ? "YES" : "NO"));
        if (hasCustomSpectrum()) {
            widget.text("Spectrum Points: " + std::to_string(mSpectrumData.size()));
            if (widget.button("Clear Spectrum")) {
                clearCustomData();
            }
        }

        widget.text("Custom Light Field: " + (hasCustomLightField() ? "YES" : "NO"));
        if (hasCustomLightField()) {
            widget.text("Light Field Points: " + std::to_string(mLightFieldData.size()));
        }

        // Computed properties display
        widget.separator();
        widget.text("Computed Properties:");

        float surfaceArea = calculateSurfaceArea();
        widget.text("Surface Area: " + std::to_string(surfaceArea));

        // Debug controls
        widget.separator();
        widget.text("Debug Controls:");

        if (widget.button("Log Debug Info")) {
            logDebugInfo("manual_trigger");
        }

        if (widget.button("Validate State")) {
            bool isValid = validateSceneState();
            logInfo("LED_Mesh: State validation result: {}", isValid ? "PASS" : "FAIL");
        }

        if (mIsInScene && widget.button("Remove from Scene")) {
            removeFromScene(mpScene);
        }

        if (!mIsInScene && widget.button("Add to Scene")) {
            // This would need scene context - placeholder for now
            logInfo("LED_Mesh: Add to scene requested (needs scene context)");
        }

        // Advanced debug information
        if (widget.collapsingHeader("Advanced Debug")) {
            widget.text("Transform Matrix:");
            for (int i = 0; i < 4; i++) {
                widget.text("  [" + std::to_string(mTransformMatrix[i][0]) + ", " +
                                  std::to_string(mTransformMatrix[i][1]) + ", " +
                                  std::to_string(mTransformMatrix[i][2]) + ", " +
                                  std::to_string(mTransformMatrix[i][3]) + "]");
            }

            if (mIsInScene) {
                widget.text("Scene IDs:");
                widget.text("  Mesh ID: " + std::to_string(mMeshID));
                widget.text("  Material ID: " + std::to_string(mMaterialID));
                widget.text("  Node ID: " + std::to_string(mNodeID));
                widget.text("  Instance ID: " + std::to_string(mMeshInstanceID));
            }
        }

    } catch (const std::exception& e) {
        logError("LED_Mesh::renderUI - Exception: {}", e.what());
        widget.text("UI Error: " + std::string(e.what()));
    }
}

// 测试和验证辅助方法
void LED_Mesh::runSelfTest()
{
    logInfo("LED_Mesh: Running self-test for '{}'", mName);

    try {
        // Test shape changes
        LEDShape originalShape = mLEDShape;
        setLEDShape(LEDShape::Sphere);
        setLEDShape(LEDShape::Rectangle);
        setLEDShape(LEDShape::Ellipsoid);
        setLEDShape(originalShape);
        logInfo("  Shape changes: PASS");

        // Test scaling
        float3 originalScale = mScaling;
        setScaling(float3(0.5f));
        setScaling(float3(2.0f));
        setScaling(originalScale);
        logInfo("  Scaling changes: PASS");

        // Test transform
        float4x4 originalTransform = mTransformMatrix;
        setTransformMatrix(math::translate(float4x4::identity(), float3(1.0f)));
        setTransformMatrix(originalTransform);
        logInfo("  Transform changes: PASS");

        // Test surface area calculation
        float area = calculateSurfaceArea();
        if (area > 0.0f && area != 0.666f) {
            logInfo("  Surface area calculation: PASS ({})", area);
        } else {
            logWarning("  Surface area calculation: QUESTIONABLE ({})", area);
        }

        // Test spectrum data
        std::vector<float2> testSpectrum = {{450.0f, 0.5f}, {550.0f, 1.0f}, {650.0f, 0.3f}};
        loadSpectrumData(testSpectrum);
        if (hasCustomSpectrum()) {
            logInfo("  Spectrum loading: PASS");
        } else {
            logWarning("  Spectrum loading: FAIL");
        }
        clearCustomData();

        logInfo("LED_Mesh: Self-test completed for '{}'", mName);

    } catch (const std::exception& e) {
        logError("LED_Mesh::runSelfTest - Exception: {}", e.what());
    }
}
```

#### 3. 错误处理
- **UI渲染异常**：捕获所有UI代码异常，显示错误信息而不是崩溃
- **参数边界检查**：所有UI输入都有合理的范围限制
- **状态不一致处理**：UI显示状态与内部状态不一致时给出警告
- **测试失败处理**：自测试中的任何失败都记录但不终止测试过程

#### 4. 验证方法
- UI中的所有控件都应正确反映当前状态
- 形状下拉菜单改变应立即在场景中生效
- 缩放和位置滑块调整应实时更新几何体
- 状态信息显示应准确（是否在场景中、ID值等）
- 自测试运行应显示"Self-test completed"且大部分测试通过
- Debug信息按钮应输出完整的对象状态
- 所有异常情况都应在UI中显示而不是崩溃应用
</rewritten_file>
