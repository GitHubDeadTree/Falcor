/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "Light.h"
#include "LEDLight.h"
#include "LED_Emissive.h"
#include "Core/Program/ShaderVar.h"
#include "Utils/Logger.h"
#include "Utils/UI/Gui.h"
#include "Utils/Color/ColorHelpers.slang"
#include "Utils/Scripting/ScriptBindings.h"

namespace Falcor
{
    static_assert(sizeof(LightData) % 16 == 0, "LightData size should be a multiple of 16B");

    // PointLight static members
    bool PointLight::sDebugUIEnabled = false;

    // Debug logging macro for PointLight
    #define POINTLIGHT_LOG_DEBUG(...) do { if (PointLight::sDebugUIEnabled) { logInfo(__VA_ARGS__); } } while(0)

    // Light

    void Light::setActive(bool active)
    {
        if (active != mActive)
        {
            mActive = active;
            mActiveChanged = true;
        }
    }

    void Light::setIntensity(const float3& intensity)
    {
        mData.intensity = intensity;
    }

    Light::Changes Light::beginFrame()
    {
        mChanges = Changes::None;
        if (mActiveChanged) mChanges |= Changes::Active;
        if (any(mPrevData.posW != mData.posW)) mChanges |= Changes::Position;
        if (any(mPrevData.dirW != mData.dirW)) mChanges |= Changes::Direction;
        if (any(mPrevData.intensity != mData.intensity)) mChanges |= Changes::Intensity;
        if (mPrevData.openingAngle != mData.openingAngle) mChanges |= Changes::SurfaceArea;
        if (mPrevData.penumbraAngle != mData.penumbraAngle) mChanges |= Changes::SurfaceArea;
        if (mPrevData.cosSubtendedAngle != mData.cosSubtendedAngle) mChanges |= Changes::SurfaceArea;
        if (mPrevData.surfaceArea != mData.surfaceArea) mChanges |= Changes::SurfaceArea;
        if (mPrevData.transMat != mData.transMat) mChanges |= (Changes::Position | Changes::Direction);

        FALCOR_ASSERT(all(mPrevData.tangent == mData.tangent));
        FALCOR_ASSERT(all(mPrevData.bitangent == mData.bitangent));

        mPrevData = mData;
        mActiveChanged = false;

        return getChanges();
    }

    void Light::bindShaderData(const ShaderVar& var)
    {
#define check_offset(_a) FALCOR_ASSERT(var.getType()->getMemberOffset(#_a).getByteOffset() == offsetof(LightData, _a))
        check_offset(dirW);
        check_offset(intensity);
        check_offset(penumbraAngle);
#undef check_offset

        var.setBlob(mData);
    }

    float3 Light::getColorForUI()
    {
        if (any((mUiLightIntensityColor * mUiLightIntensityScale) != mData.intensity))
        {
            float mag = std::max(mData.intensity.x, std::max(mData.intensity.y, mData.intensity.z));
            if (mag <= 1.f)
            {
                mUiLightIntensityColor = mData.intensity;
                mUiLightIntensityScale = 1.0f;
            }
            else
            {
                mUiLightIntensityColor = mData.intensity / mag;
                mUiLightIntensityScale = mag;
            }
        }

        return mUiLightIntensityColor;
    }

    void Light::setColorFromUI(const float3& uiColor)
    {
        mUiLightIntensityColor = uiColor;
        setIntensity(mUiLightIntensityColor * mUiLightIntensityScale);
    }

    float Light::getIntensityForUI()
    {
        if (any((mUiLightIntensityColor * mUiLightIntensityScale) != mData.intensity))
        {
            float mag = std::max(mData.intensity.x, std::max(mData.intensity.y, mData.intensity.z));
            if (mag <= 1.f)
            {
                mUiLightIntensityColor = mData.intensity;
                mUiLightIntensityScale = 1.0f;
            }
            else
            {
                mUiLightIntensityColor = mData.intensity / mag;
                mUiLightIntensityScale = mag;
            }
        }

        return mUiLightIntensityScale;
    }

    void Light::setIntensityFromUI(float intensity)
    {
        mUiLightIntensityScale = intensity;
        setIntensity(mUiLightIntensityColor * mUiLightIntensityScale);
    }

    void Light::renderUI(Gui::Widgets& widget)
    {
        bool active = isActive();
        if (widget.checkbox("Active", active)) setActive(active);

        if (mHasAnimation) widget.checkbox("Animated", mIsAnimated);

        float3 color = getColorForUI();
        if (widget.rgbColor("Color", color))
        {
            setColorFromUI(color);
        }

        float intensity = getIntensityForUI();
        if (widget.var("Intensity", intensity))
        {
            setIntensityFromUI(intensity);
        }
    }

    Light::Light(const std::string& name, LightType type)
        : mName(name)
    {
        mData.type = (uint32_t)type;
    }

    // PointLight

    PointLight::PointLight(const std::string& name)
        : Light(name, LightType::Point)
    {
        mPrevData = mData;
    }

    void PointLight::setWorldDirection(const float3& dir)
    {
        if (!(length(dir) > 0.f)) // NaNs propagate
        {
            logWarning("Can't set light direction to zero length vector. Ignoring call.");
            return;
        }
        mData.dirW = normalize(dir);
    }

    void PointLight::setWorldPosition(const float3& pos)
    {
        mData.posW = pos;
    }

    float PointLight::getPower() const
    {
        // Check if power was manually set
        if (mPowerManuallySet)
        {
            POINTLIGHT_LOG_DEBUG("DEBUG: getPower mode = manual");
            POINTLIGHT_LOG_DEBUG("DEBUG: Returning manual power = {}", mManualPower);
            return mManualPower;
        }

        // Calculate power from current intensity
        POINTLIGHT_LOG_DEBUG("DEBUG: getPower mode = calculated");
        float intensityMagnitude = luminance(mData.intensity);
        POINTLIGHT_LOG_DEBUG("DEBUG: Intensity magnitude = {}", intensityMagnitude);

        // Calculate solid angle based on opening angle
        float solidAngle;
        if (mData.openingAngle >= (float)M_PI)
        {
            // Isotropic point light: Ω = 4π
            solidAngle = 4.0f * (float)M_PI;
            POINTLIGHT_LOG_DEBUG("DEBUG: Opening angle = {}, using isotropic formula", mData.openingAngle);
        }
        else
        {
            // Spot light: Ω = 2π(1 - cos(θc))
            solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
            POINTLIGHT_LOG_DEBUG("DEBUG: Opening angle = {}, cos = {}, using spot light formula",
                   mData.openingAngle, mData.cosOpeningAngle);
        }

        POINTLIGHT_LOG_DEBUG("DEBUG: Solid angle = {}", solidAngle);

        // Calculate power: Φ = I * Ω
        float power;
        if (solidAngle > 0.0f && std::isfinite(intensityMagnitude))
        {
            power = intensityMagnitude * solidAngle;
        }
        else
        {
            logError("ERROR: Power calculation failed, returning default 1.0W");
            power = 1.0f;
        }

        POINTLIGHT_LOG_DEBUG("DEBUG: Calculated power = {}", power);
        return power;
    }

    void PointLight::renderUI(Gui::Widgets& widget)
    {
        Light::renderUI(widget);

        widget.var("World Position", mData.posW, -FLT_MAX, FLT_MAX);
        widget.direction("Direction", mData.dirW);

        float openingAngle = getOpeningAngle();
        if (widget.var("Opening Angle", openingAngle, 0.f, (float)M_PI)) setOpeningAngle(openingAngle);
        float penumbraAngle = getPenumbraAngle();
        if (widget.var("Penumbra Width", penumbraAngle, 0.f, (float)M_PI)) setPenumbraAngle(penumbraAngle);

        // Power control UI section
        widget.separator();

        // Debug UI toggle control
        widget.separator();
        widget.checkbox("Enable Debug Output", sDebugUIEnabled);

        // Display current mode
        std::string modeText = mPowerManuallySet ? "Power-driven" : "Intensity-driven";
        widget.text("Control Mode: " + modeText);
        POINTLIGHT_LOG_DEBUG("DEBUG: UI displaying mode: {}", modeText);

        // Power input control
        float currentPower = getPower();
        POINTLIGHT_LOG_DEBUG("DEBUG: Power UI widget value = {}", currentPower);

        if (widget.var("Power (Watts)", currentPower, 0.0f, 10000.0f))
        {
            // Input validation for UI widget
            if (currentPower < 0.0f)
            {
                logError("ERROR: UI power widget error, showing default");
                currentPower = 0.0f;
            }
            if (!std::isfinite(currentPower))
            {
                logError("ERROR: UI power widget error, showing default");
                currentPower = 1.0f;
            }

            POINTLIGHT_LOG_DEBUG("DEBUG: User changed power via UI to {}", currentPower);
            setPower(currentPower);
        }

        // Show tooltip with power information
        widget.tooltip("Total radiant power in watts. When set, light switches to power-driven mode.\n"
                      "Opening angle changes will preserve power and adjust intensity accordingly.");
    }

    void PointLight::setOpeningAngle(float openingAngle)
    {
        // Input validation and debug logging
        float oldAngle = mData.openingAngle;
        POINTLIGHT_LOG_DEBUG("DEBUG: setOpeningAngle called with angle = {}", openingAngle);

        // Validate and clamp input angle
        if (!std::isfinite(openingAngle))
        {
            logError("PointLight::setOpeningAngle - Invalid opening angle {}, keeping {}", openingAngle, oldAngle);
            return;
        }

        openingAngle = std::clamp(openingAngle, 0.f, (float)M_PI);
        if (openingAngle == mData.openingAngle) return;

        POINTLIGHT_LOG_DEBUG("DEBUG: Opening angle changed from {} to {}", oldAngle, openingAngle);

        // Store old angle for power preservation logic
        float previousAngle = mData.openingAngle;

        // Update angle data
        mData.openingAngle = openingAngle;
        mData.penumbraAngle = std::min(mData.penumbraAngle, openingAngle);

        // Prepare an auxiliary cosine of the opening angle to quickly check whether we're within the cone of a spot light.
        mData.cosOpeningAngle = std::cos(openingAngle);

        // If power was manually set, preserve power and recalculate intensity
        if (mPowerManuallySet)
        {
            POINTLIGHT_LOG_DEBUG("DEBUG: Power is manually set, recalculating intensity for new angle");

            // Calculate new solid angle
            float newSolidAngle;
            if (mData.openingAngle >= (float)M_PI)
            {
                // Isotropic point light: Ω = 4π
                newSolidAngle = 4.0f * (float)M_PI;
                POINTLIGHT_LOG_DEBUG("DEBUG: New opening angle = {}, using isotropic formula", mData.openingAngle);
            }
            else
            {
                // Spot light: Ω = 2π(1 - cos(θc))
                newSolidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
                POINTLIGHT_LOG_DEBUG("DEBUG: New opening angle = {}, cos = {}, using spot light formula",
                       mData.openingAngle, mData.cosOpeningAngle);
            }

            POINTLIGHT_LOG_DEBUG("DEBUG: Preserving power = {}, new solid angle = {}", mManualPower, newSolidAngle);

            // Calculate new intensity magnitude: I = Φ / Ω
            float newIntensityMagnitude;
            if (newSolidAngle > 0.0f)
            {
                newIntensityMagnitude = mManualPower / newSolidAngle;
            }
            else
            {
                logError("PointLight::setOpeningAngle - Invalid new solid angle, using default intensity 1.0");
                newIntensityMagnitude = 1.0f;
            }

            POINTLIGHT_LOG_DEBUG("DEBUG: Calculated new intensity magnitude = {}", newIntensityMagnitude);

            // Preserve color ratio while adjusting intensity magnitude
            float3 currentIntensity = mData.intensity;
            float currentMagnitude = luminance(currentIntensity);

            if (currentMagnitude > 0.0f)
            {
                // Preserve color ratio
                mData.intensity = currentIntensity * (newIntensityMagnitude / currentMagnitude);
                POINTLIGHT_LOG_DEBUG("DEBUG: Preserved color ratio, new intensity = ({}, {}, {})",
                       mData.intensity.x, mData.intensity.y, mData.intensity.z);
            }
            else
            {
                // Current intensity is zero, set to white light
                mData.intensity = float3(newIntensityMagnitude);
                POINTLIGHT_LOG_DEBUG("DEBUG: Current intensity was zero, setting to white light");
            }

            // Validate final result
            if (!all(isfinite(mData.intensity)))
            {
                logError("ERROR: Intensity recalculation failed, using default intensity");
                mData.intensity = float3(1.0f);
            }
        }
        else
        {
            POINTLIGHT_LOG_DEBUG("DEBUG: Intensity-driven mode, no power preservation needed");
        }
    }

    void PointLight::setPenumbraAngle(float angle)
    {
        angle = std::clamp(angle, 0.0f, mData.openingAngle);
        if (mData.penumbraAngle == angle) return;
        mData.penumbraAngle = angle;
    }

        void PointLight::setPower(float power)
    {
        // Input validation and debug logging
        if (power < 0.0f)
        {
            logWarning("PointLight::setPower - Invalid negative power value: {}. Using 0.0.", power);
            power = 0.0f;
        }
        if (!std::isfinite(power))
        {
            logError("PointLight::setPower - Non-finite power value detected. Using default 1.0W.");
            power = 1.0f;
        }

        POINTLIGHT_LOG_DEBUG("DEBUG: setPower called with power = {}", power);

        // Store the manual power value and set the flag
        mManualPower = power;
        mPowerManuallySet = true;

        // Calculate solid angle based on opening angle
        float solidAngle;
        if (mData.openingAngle >= (float)M_PI)
        {
            // Isotropic point light: Ω = 4π
            solidAngle = 4.0f * (float)M_PI;
            POINTLIGHT_LOG_DEBUG("DEBUG: Opening angle = {}, using isotropic formula", mData.openingAngle);
        }
        else
        {
            // Spot light: Ω = 2π(1 - cos(θc))
            solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
            POINTLIGHT_LOG_DEBUG("DEBUG: Opening angle = {}, cos = {}, using spot light formula",
                   mData.openingAngle, mData.cosOpeningAngle);
        }

        POINTLIGHT_LOG_DEBUG("DEBUG: Solid angle = {}", solidAngle);

        // Calculate intensity magnitude: I = Φ / Ω
        float intensityMagnitude;
        if (solidAngle > 0.0f)
        {
            intensityMagnitude = power / solidAngle;
        }
        else
        {
            logError("PointLight::setPower - Invalid solid angle, using default intensity 1.0");
            intensityMagnitude = 1.0f;
        }

        POINTLIGHT_LOG_DEBUG("DEBUG: Calculated intensity magnitude = {}", intensityMagnitude);

        // Preserve color ratio while adjusting intensity magnitude
        float3 currentIntensity = mData.intensity;
        float currentMagnitude = luminance(currentIntensity);

        POINTLIGHT_LOG_DEBUG("DEBUG: Current magnitude = {}, preserving color ratio", currentMagnitude);

        if (currentMagnitude > 0.0f)
        {
            // Preserve color ratio
            mData.intensity = currentIntensity * (intensityMagnitude / currentMagnitude);
        }
        else
        {
            // Current intensity is zero, set to white light
            mData.intensity = float3(intensityMagnitude);
            POINTLIGHT_LOG_DEBUG("DEBUG: Current intensity was zero, setting to white light");
        }

        // Validate final result
        if (!all(isfinite(mData.intensity)))
        {
            logError("ERROR: Power calculation failed, using default intensity");
            mData.intensity = float3(1.0f);
        }
    }

    void PointLight::setIntensity(const float3& intensity)
    {
        // Input validation and debug logging
        POINTLIGHT_LOG_DEBUG("DEBUG: setIntensity called with intensity = ({}, {}, {})", intensity.x, intensity.y, intensity.z);

        // Validate input intensity
        if (any(intensity < 0.0f))
        {
            logWarning("PointLight::setIntensity - Negative intensity components detected. Using absolute values.");
        }
        if (!all(isfinite(intensity)))
        {
            logError("PointLight::setIntensity - Non-finite intensity values detected. Using default intensity.");
            Light::setIntensity(float3(1.0f));
            mPowerManuallySet = false;
            POINTLIGHT_LOG_DEBUG("DEBUG: Switching from power-driven to intensity-driven mode");
            return;
        }

        // Call parent class method to set the intensity
        Light::setIntensity(abs(intensity));

        // Switch to intensity-driven mode
        mPowerManuallySet = false;
        POINTLIGHT_LOG_DEBUG("DEBUG: Switching from power-driven to intensity-driven mode");
    }

    void PointLight::updateFromAnimation(const float4x4& transform)
    {
        float3 fwd = -transform.getCol(2).xyz();
        float3 pos = transform.getCol(3).xyz();
        setWorldPosition(pos);
        setWorldDirection(fwd);
    }

    // DirectionalLight

    DirectionalLight::DirectionalLight(const std::string& name)
        : Light(name, LightType::Directional)
    {
        mPrevData = mData;
    }

    void DirectionalLight::renderUI(Gui::Widgets& widget)
    {
        Light::renderUI(widget);

        if (widget.direction("Direction", mData.dirW))
        {
            setWorldDirection(mData.dirW);
        }
    }

    void DirectionalLight::setWorldDirection(const float3& dir)
    {
        if (!(length(dir) > 0.f)) // NaNs propagate
        {
            logWarning("Can't set light direction to zero length vector. Ignoring call.");
            return;
        }
        mData.dirW = normalize(dir);
    }

    void DirectionalLight::updateFromAnimation(const float4x4& transform)
    {
        float3 fwd = -transform.getCol(2).xyz();
        setWorldDirection(fwd);
    }

    // DistantLight

    DistantLight::DistantLight(const std::string& name)
        : Light(name, LightType::Distant)
    {
        mData.dirW = float3(0.f, -1.f, 0.f);
        setAngle(0.5f * 0.53f * (float)M_PI / 180.f);   // Approximate sun half-angle
        update();
        mPrevData = mData;
    }

    void DistantLight::renderUI(Gui::Widgets& widget)
    {
        Light::renderUI(widget);

        if (widget.direction("Direction", mData.dirW))
        {
            setWorldDirection(mData.dirW);
        }

        if (widget.var("Half-angle", mAngle, 0.f, (float)M_PI_2))
        {
            setAngle(mAngle);
        }
        widget.tooltip("Half-angle subtended by the light, in radians.");
    }

    void DistantLight::setAngle(float angle)
    {
        mAngle = std::clamp(angle, 0.f, (float)M_PI_2);

        mData.cosSubtendedAngle = std::cos(mAngle);
    }

    void DistantLight::setWorldDirection(const float3& dir)
    {
        if (!(length(dir) > 0.f)) // NaNs propagate
        {
            logWarning("Can't set light direction to zero length vector. Ignoring call.");
            return;
        }
        mData.dirW = normalize(dir);
        update();
    }

    void DistantLight::update()
    {
        // Update transformation matrices
        // Assumes that mData.dirW is normalized
        const float3 up(0.f, 0.f, 1.f);
        float3 vec = cross(up, -mData.dirW);
        float sinTheta = length(vec);
        if (sinTheta > 0.f)
        {
            float cosTheta = dot(up, -mData.dirW);
            mData.transMat = math::matrixFromRotation(std::acos(cosTheta), vec);
        }
        else
        {
            mData.transMat = float4x4::identity();
        }
        mData.transMatIT = inverse(transpose(mData.transMat));
    }

    void DistantLight::updateFromAnimation(const float4x4& transform)
    {
        float3 fwd = -transform.getCol(2).xyz();
        setWorldDirection(fwd);
    }

    // AnalyticAreaLight

    AnalyticAreaLight::AnalyticAreaLight(const std::string& name, LightType type)
        : Light(name, type)
    {
        mData.tangent = float3(1, 0, 0);
        mData.bitangent = float3(0, 1, 0);
        mData.surfaceArea = 4.0f;

        mScaling = float3(1, 1, 1);
        update();
        mPrevData = mData;
    }

    float AnalyticAreaLight::getPower() const
    {
        return luminance(mData.intensity) * (float)M_PI * mData.surfaceArea;
    }

    void AnalyticAreaLight::update()
    {
        // Update matrix
        mData.transMat = mul(mTransformMatrix, math::matrixFromScaling(mScaling));
        mData.transMatIT = inverse(transpose(mData.transMat));
    }

    // RectLight

    void RectLight::update()
    {
        AnalyticAreaLight::update();

        float rx = length(transformVector(mData.transMat, float3(1.0f, 0.0f, 0.0f)));
        float ry = length(transformVector(mData.transMat, float3(0.0f, 1.0f, 0.0f)));
        mData.surfaceArea = 4.0f * rx * ry;
    }

    // DiscLight

    void DiscLight::update()
    {
        AnalyticAreaLight::update();

        float rx = length(transformVector(mData.transMat, float3(1.0f, 0.0f, 0.0f)));
        float ry = length(transformVector(mData.transMat, float3(0.0f, 1.0f, 0.0f)));

        mData.surfaceArea = (float)M_PI * rx * ry;
    }

    // SphereLight

    void SphereLight::update()
    {
        AnalyticAreaLight::update();

        float rx = length(transformVector(mData.transMat, float3(1.0f, 0.0f, 0.0f)));
        float ry = length(transformVector(mData.transMat, float3(0.0f, 1.0f, 0.0f)));
        float rz = length(transformVector(mData.transMat, float3(0.0f, 0.0f, 1.0f)));

        mData.surfaceArea = 4.0f * (float)M_PI * std::pow(std::pow(rx * ry, 1.6f) + std::pow(ry * rz, 1.6f) + std::pow(rx * rz, 1.6f) / 3.0f, 1.0f / 1.6f);
    }


    FALCOR_SCRIPT_BINDING(Light)
    {
        using namespace pybind11::literals;

        FALCOR_SCRIPT_BINDING_DEPENDENCY(Animatable)

        pybind11::class_<Light, Animatable, ref<Light>> light(m, "Light");
        light.def_property("name", &Light::getName, &Light::setName);
        light.def_property("active", &Light::isActive, &Light::setActive);
        light.def_property("animated", &Light::isAnimated, &Light::setIsAnimated);
        light.def_property("intensity", &Light::getIntensity, &Light::setIntensity);

        pybind11::class_<PointLight, Light, ref<PointLight>> pointLight(m, "PointLight");
        pointLight.def(pybind11::init(&PointLight::create), "name"_a = "");
        pointLight.def_property("position", &PointLight::getWorldPosition, &PointLight::setWorldPosition);
        pointLight.def_property("direction", &PointLight::getWorldDirection, &PointLight::setWorldDirection);
        pointLight.def_property("openingAngle", &PointLight::getOpeningAngle, &PointLight::setOpeningAngle);
        pointLight.def_property("penumbraAngle", &PointLight::getPenumbraAngle, &PointLight::setPenumbraAngle);

                // Power attribute bindings
        POINTLIGHT_LOG_DEBUG("DEBUG: Registering Python power property binding");
        try
        {
            pointLight.def_property("power",
                [](const PointLight& light) -> float {
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python script accessing power property");
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python get power = {}", light.getPower());
                    return light.getPower();
                },
                [](PointLight& light, float power) {
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python script accessing power property");
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python set power = {}", power);
                    light.setPower(power);
                });

            // Power mode status binding (read-only)
            pointLight.def_property_readonly("isPowerManuallySet",
                [](const PointLight& light) -> bool {
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python script accessing isPowerManuallySet property");
                    bool result = light.isPowerManuallySet();
                    POINTLIGHT_LOG_DEBUG("DEBUG: Python isPowerManuallySet = {}", result);
                    return result;
                });
        }
        catch (const std::exception& e)
        {
            logError("ERROR: Python binding registration failed for power property: {}", e.what());
        }

        pybind11::class_<DirectionalLight, Light, ref<DirectionalLight>> directionalLight(m, "DirectionalLight");
        directionalLight.def(pybind11::init(&DirectionalLight::create), "name"_a = "");
        directionalLight.def_property("direction", &DirectionalLight::getWorldDirection, &DirectionalLight::setWorldDirection);

        pybind11::class_<DistantLight, Light, ref<DistantLight>> distantLight(m, "DistantLight");
        distantLight.def(pybind11::init(&DistantLight::create), "name"_a = "");
        distantLight.def_property("direction", &DistantLight::getWorldDirection, &DistantLight::setWorldDirection);
        distantLight.def_property("angle", &DistantLight::getAngle, &DistantLight::setAngle);

        pybind11::class_<AnalyticAreaLight, Light, ref<AnalyticAreaLight>> analyticLight(m, "AnalyticAreaLight");

        pybind11::class_<RectLight, AnalyticAreaLight, ref<RectLight>> rectLight(m, "RectLight");
        rectLight.def(pybind11::init(&RectLight::create), "name"_a = "");

        pybind11::class_<DiscLight, AnalyticAreaLight, ref<DiscLight>> discLight(m, "DiscLight");
        discLight.def(pybind11::init(&DiscLight::create), "name"_a = "");

        pybind11::class_<SphereLight, AnalyticAreaLight, ref<SphereLight>> sphereLight(m, "SphereLight");
        sphereLight.def(pybind11::init(&SphereLight::create), "name"_a = "");

        // LEDLight bindings
        pybind11::enum_<LEDLight::LEDShape>(m, "LEDShape")
            .value("Sphere", LEDLight::LEDShape::Sphere)
            .value("Ellipsoid", LEDLight::LEDShape::Ellipsoid)
            .value("Rectangle", LEDLight::LEDShape::Rectangle);

        pybind11::class_<LEDLight, Light, ref<LEDLight>> ledLight(m, "LEDLight");
        ledLight.def_static("create", &LEDLight::create, "name"_a = "");

        // Basic properties
        ledLight.def_property("position", &LEDLight::getWorldPosition, &LEDLight::setWorldPosition);
        ledLight.def_property("direction", &LEDLight::getWorldDirection, &LEDLight::setWorldDirection);
        ledLight.def_property("openingAngle", &LEDLight::getOpeningAngle, &LEDLight::setOpeningAngle);

        // LED specific properties
        ledLight.def_property("shape", &LEDLight::getLEDShape, &LEDLight::setLEDShape);
        ledLight.def_property("lambertExponent", &LEDLight::getLambertExponent, &LEDLight::setLambertExponent);
        ledLight.def_property("totalPower", &LEDLight::getTotalPower, &LEDLight::setTotalPower);
        ledLight.def_property("scaling", &LEDLight::getScaling, &LEDLight::setScaling);
        ledLight.def_property("transform", &LEDLight::getTransformMatrix, &LEDLight::setTransformMatrix);

        // Status query properties (read-only)
        ledLight.def_property_readonly("hasCustomSpectrum", &LEDLight::hasCustomSpectrum);
        ledLight.def_property_readonly("hasCustomLightField", &LEDLight::hasCustomLightField);

        // Data loading methods
        ledLight.def("loadSpectrumFromFile", &LEDLight::loadSpectrumFromFile, "filePath"_a);
        ledLight.def("loadLightFieldFromFile", &LEDLight::loadLightFieldFromFile, "filePath"_a);
        ledLight.def("clearCustomData", &LEDLight::clearCustomData);

        // Advanced data loading methods with vector input
        ledLight.def("loadSpectrumData", &LEDLight::loadSpectrumData, "spectrumData"_a);
        ledLight.def("loadLightFieldData", &LEDLight::loadLightFieldData, "lightFieldData"_a);

        // LED_EmissiveShape enum binding
        pybind11::enum_<LED_EmissiveShape>(m, "LED_EmissiveShape")
            .value("Sphere", LED_EmissiveShape::Sphere)
            .value("Rectangle", LED_EmissiveShape::Rectangle)
            .value("Ellipsoid", LED_EmissiveShape::Ellipsoid);

        // LED_Emissive class binding
        pybind11::class_<LED_Emissive, Object, ref<LED_Emissive>>(m, "LED_Emissive")
            .def_static("create", &LED_Emissive::create, "name"_a = "LED_Emissive")

            // Basic properties
            .def_property("shape", &LED_Emissive::getShape, &LED_Emissive::setShape)
            .def_property("position", &LED_Emissive::getPosition, &LED_Emissive::setPosition)
            .def_property("scaling", [](const LED_Emissive& led) -> float3 { return float3(1.0f); }, &LED_Emissive::setScaling)
            .def_property("direction", [](const LED_Emissive& led) -> float3 { return float3(0, 0, -1); }, &LED_Emissive::setDirection)
            .def_property("totalPower", &LED_Emissive::getTotalPower, &LED_Emissive::setTotalPower)
            .def_property("color", [](const LED_Emissive& led) -> float3 { return float3(1.0f); }, &LED_Emissive::setColor)

            // Light field distribution control
            .def_property("lambertExponent", &LED_Emissive::getLambertExponent, &LED_Emissive::setLambertExponent)
            .def_property("openingAngle", &LED_Emissive::getOpeningAngle, &LED_Emissive::setOpeningAngle)
            .def("loadLightFieldData", &LED_Emissive::loadLightFieldData, "data"_a)
            .def("loadLightFieldFromFile", &LED_Emissive::loadLightFieldFromFile, "filePath"_a)
            .def("clearLightFieldData", &LED_Emissive::clearLightFieldData)

            // Scene integration
            .def("addToSceneBuilder", &LED_Emissive::addToSceneBuilder, "sceneBuilder"_a)
            .def("removeFromScene", &LED_Emissive::removeFromScene)

            // Status query properties (read-only)
            .def_property_readonly("hasCustomLightField", &LED_Emissive::hasCustomLightField);
   }
}
