#pragma once
#include "Scene/Material/Material.h"
#include "Scene/Lights/LightProfile.h"
#include "Scene/SceneIDs.h"
#include "Core/Macros.h"
#include "Core/Object.h"
#include "Utils/Math/Vector.h"
#include "Utils/Math/Matrix.h"
#include "Utils/UI/Gui.h"
#include "Scene/SceneBuilder.h"
#include "Scene/TriangleMesh.h"

namespace Falcor {

// Forward declarations
class Scene;
class SceneBuilder;

enum class LED_EmissiveShape {
    Sphere = 0,
    Rectangle = 1,
    Ellipsoid = 2
};

class FALCOR_API LED_Emissive : public Object {
    FALCOR_OBJECT(LED_Emissive)
public:
    static ref<LED_Emissive> create(const std::string& name = "LED_Emissive");
    LED_Emissive(const std::string& name);
    ~LED_Emissive() = default;

    // Basic properties
    void setShape(LED_EmissiveShape shape);
    void setPosition(const float3& pos);
    void setScaling(const float3& scale);
    void setDirection(const float3& dir);
    void setTotalPower(float power);
    void setColor(const float3& color);

    // Light field distribution control
    void setLambertExponent(float n);
    void setOpeningAngle(float angle);
    void loadLightFieldData(const std::vector<float2>& data);
    void loadLightFieldFromFile(const std::string& filePath);
    void clearLightFieldData();

    // Scene integration
    void addToScene(Scene* pScene);
    void addToSceneBuilder(SceneBuilder& sceneBuilder);
    void removeFromScene();
    void renderUI(Gui::Widgets& widget);

    // Property getters
    std::string getName() const { return mName; }
    LED_EmissiveShape getShape() const { return mShape; }
    float3 getPosition() const { return mPosition; }
    float3 getScaling() const { return mScaling; }
    float3 getDirection() const { return mDirection; }
    float3 getColor() const { return mColor; }
    float getTotalPower() const { return mTotalPower; }
    float getLambertExponent() const { return mLambertN; }
    float getOpeningAngle() const { return mOpeningAngle; }
    bool hasCustomLightField() const { return mHasCustomLightField; }

private:
    // Forward declarations and types
    struct Vertex {
        float3 position;
        float3 normal;
        float2 texCoord;
    };

    std::string mName;
    LED_EmissiveShape mShape = LED_EmissiveShape::Sphere;
    float3 mPosition = float3(0.0f);
    float3 mScaling = float3(1.0f);
    float3 mDirection = float3(0, 0, -1);
    float mTotalPower = 1.0f;
    float3 mColor = float3(1.0f);

    // Light field distribution parameters
    float mLambertN = 1.0f;
    float mOpeningAngle = (float)M_PI;
    float mCosOpeningAngle = -1.0f;

    // Custom light field data
    std::vector<float2> mLightFieldData;
    bool mHasCustomLightField = false;
    ref<LightProfile> mpLightProfile;

    // Scene integration
    Scene* mpScene = nullptr;
    ref<Device> mpDevice = nullptr;
    std::vector<MeshID> mMeshIndices;
    std::vector<NodeID> mNodeIndices;
    MaterialID mMaterialID;
    bool mIsAddedToScene = false;
    bool mCalculationError = false;

    // Private methods
    void generateGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void updateLightProfile();
    void updateEmissiveIntensity();
    float calculateSurfaceArea() const;
    ref<Material> createEmissiveMaterial();

    // Light profile creation methods
    ref<LightProfile> createLambertLightProfile();
    ref<LightProfile> createCustomLightProfile();
    ref<LightProfile> createDefaultLightProfile();

    // Material creation methods
    float calculateEmissiveIntensity();
    float calculateDirectionalEmissiveIntensity();
    ref<Material> createErrorMaterial();

    // Geometry generation methods
    void generateSphereGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void generateRectangleGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void generateEllipsoidGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    // Scene integration methods
    float4x4 createTransformMatrix();
    ref<TriangleMesh> createTriangleMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void cleanup();
};

} // namespace Falcor
