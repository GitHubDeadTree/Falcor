#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"

using namespace Falcor;

/** Irradiance calculation render pass.
    This pass takes the initial ray direction and radiance data from the path tracer
    and calculates the irradiance (flux per unit area) for each direction.
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

    // UI variables
    bool mEnabled = true;              ///< Enable/disable the pass
    std::string mOutputTexName = "irradiance"; ///< Name of the output texture
    float mIntensityScale = 1.0f;      ///< Scaling factor for the irradiance intensity
    bool mDebugNormalView = false;     ///< When enabled, visualizes normal directions as colors for debugging
    bool mUseActualNormals = false;    ///< Whether to use actual normals from VBuffer instead of a fixed normal
    float3 mFixedNormal = float3(0, 0, 1);  ///< The fixed normal direction to use when not using actual normals

    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProgram();
};
