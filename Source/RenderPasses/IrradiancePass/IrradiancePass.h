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
    bool mNormalsSuccessfullyExtracted = false; ///< Whether actual normals were successfully extracted in the last execution

    // Computation interval control
    float mComputeInterval = 1.0f;     ///< Time interval between computations (in seconds)
    uint32_t mFrameInterval = 0;       ///< Frame interval between computations (0 means use time-based interval)
    float mTimeSinceLastCompute = 0.0f; ///< Time elapsed since last computation
    uint32_t mFrameCount = 0;          ///< Frame counter for interval tracking
    bool mUseLastResult = true;        ///< Whether to use the last result when skipping computation
    ref<Texture> mpLastIrradianceResult; ///< Texture to store the last irradiance result
    ref<Texture> mpLastIrradianceScalarResult; ///< Texture to store the last scalar irradiance result

    // UI variables
    bool mEnabled = true;              ///< Enable/disable the pass
    std::string mOutputTexName = "irradiance"; ///< Name of the output texture
    std::string mOutputScalarTexName = "irradianceScalar"; ///< Name of the single-channel output texture
    float mIntensityScale = 1.0f;      ///< Scaling factor for the irradiance intensity
    bool mDebugNormalView = false;     ///< When enabled, visualizes normal directions as colors for debugging
    bool mUseActualNormals = false;    ///< Whether to use actual normals from VBuffer instead of a fixed normal
    float3 mFixedNormal = float3(0, 0, 1);  ///< The fixed normal direction to use when not using actual normals
    bool mPassthrough = false;         ///< When enabled, directly outputs the input rayinfo without any calculations

    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProgram();
    bool shouldCompute(RenderContext* pRenderContext); ///< Determines if computation should be performed this frame
    void copyLastResultToOutput(RenderContext* pRenderContext, const ref<Texture>& pOutputIrradiance); ///< Copies last result to output
    void copyLastScalarResultToOutput(RenderContext* pRenderContext, const ref<Texture>& pOutputScalarIrradiance); ///< Copies last scalar result to output
};
