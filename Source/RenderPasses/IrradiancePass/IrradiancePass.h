#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"

using namespace Falcor;

/** Irradiance calculation render pass.
    This pass takes the initial ray direction and radiance data from the path tracer
    and calculates the irradiance (flux per unit area) for each direction.

    Note: Scene dependencies are only required when useActualNormals is enabled.
    Otherwise, the pass can run without scene access.
*/
class IrradiancePass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(IrradiancePass, "IrradiancePass", "Render pass for computing irradiance from ray direction and intensity data.");

    static ref<IrradiancePass> create(ref<Device> pDevice, const Properties& props) { return make_ref<IrradiancePass>(pDevice, props); }

    IrradiancePass(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;

    // Scripting functions
    bool useRayDirectionReversal() const { return mReverseRayDirection; }
    void setRayDirectionReversal(bool reverse) { mReverseRayDirection = reverse; }

private:
    // Internal state
    ref<ComputePass> mpComputePass;    ///< Compute pass that calculates irradiance
    bool mReverseRayDirection = true;  ///< Whether to reverse ray direction when calculating irradiance
    uint2 mInputResolution = {0, 0};   ///< Current input resolution for debug display
    uint2 mOutputResolution = {0, 0};  ///< Current output resolution for debug display
    ref<Scene> mpScene;                ///< Scene reference for accessing geometry data
    bool mNeedRecompile = false;       ///< Flag indicating if shader program needs to be recompiled

    // UI variables
    bool mEnabled = true;              ///< Enable/disable the pass
    std::string mOutputTexName = "irradiance"; ///< Name of the output texture
    float mIntensityScale = 1.0f;      ///< Scaling factor for the irradiance intensity
    bool mDebugNormalView = false;     ///< When enabled, visualizes normal directions as colors for debugging
    bool mUseActualNormals = false;    ///< Whether to use actual normals from VBuffer instead of a fixed normal
    float3 mFixedNormal = float3(0, 0, 1);  ///< The fixed normal direction to use when not using actual normals
    bool mPassthrough = false;         ///< When enabled, directly outputs the input rayinfo without any calculations

    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProgram();
};
