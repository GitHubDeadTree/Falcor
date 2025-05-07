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
#include "SpectrumUtils.h"
#include "Utils/Color/ColorUtils.h"

#include <xyzcurves/ciexyzCurves1931_1nm.h>
#include <illuminants/D65_5nm.h>

namespace Falcor
{
// Initialize static data.
// clang-format off
const SampledSpectrum<float3> SpectrumUtils::sCIE_XYZ_1931_1nm(360.0f, 830.0f, 471, reinterpret_cast<const float3*>(xyz1931_1nm));  // 1 nm between samples.
const SampledSpectrum<float> SpectrumUtils::sD65_5nm(300.0f, 830.0f, 107, reinterpret_cast<const float*>(D65_1nm));                 // 5 nm between samples.
// clang-format on

float3 SpectrumUtils::wavelengthToXYZ_CIE1931(float lambda)
{
    return sCIE_XYZ_1931_1nm.eval(lambda);
}

float SpectrumUtils::wavelengthToD65(float lambda)
{
    return sD65_5nm.eval(lambda);
}

float3 SpectrumUtils::wavelengthToRGB_Rec709(const float lambda)
{
    float3 XYZ = wavelengthToXYZ_CIE1931(lambda);
    return XYZtoRGB_Rec709(XYZ);
}

// CIE 1931 standard observer spectral locus points
// Wavelength range: 380-700nm, step 5nm
static const std::vector<float2> spectralLocus = {
    {0.1741f, 0.0050f}, // 380nm
    {0.1740f, 0.0050f}, // 385nm
    {0.1738f, 0.0049f}, // 390nm
    {0.1736f, 0.0049f}, // 395nm
    {0.1733f, 0.0048f}, // 400nm
    {0.1730f, 0.0048f}, // 405nm
    {0.1726f, 0.0048f}, // 410nm
    {0.1721f, 0.0048f}, // 415nm
    {0.1714f, 0.0051f}, // 420nm
    {0.1703f, 0.0058f}, // 425nm
    {0.1689f, 0.0069f}, // 430nm
    {0.1669f, 0.0086f}, // 435nm
    {0.1644f, 0.0109f}, // 440nm
    {0.1611f, 0.0138f}, // 445nm
    {0.1566f, 0.0177f}, // 450nm
    {0.1510f, 0.0227f}, // 455nm
    {0.1440f, 0.0297f}, // 460nm
    {0.1355f, 0.0399f}, // 465nm
    {0.1241f, 0.0578f}, // 470nm
    {0.1096f, 0.0868f}, // 475nm
    {0.0913f, 0.1327f}, // 480nm
    {0.0687f, 0.2007f}, // 485nm
    {0.0454f, 0.2950f}, // 490nm
    {0.0235f, 0.4127f}, // 495nm
    {0.0082f, 0.5384f}, // 500nm
    {0.0039f, 0.6548f}, // 505nm
    {0.0139f, 0.7502f}, // 510nm
    {0.0389f, 0.8120f}, // 515nm
    {0.0743f, 0.8338f}, // 520nm
    {0.1142f, 0.8262f}, // 525nm
    {0.1547f, 0.8059f}, // 530nm
    {0.1929f, 0.7816f}, // 535nm
    {0.2296f, 0.7543f}, // 540nm
    {0.2658f, 0.7243f}, // 545nm
    {0.3016f, 0.6923f}, // 550nm
    {0.3373f, 0.6589f}, // 555nm
    {0.3731f, 0.6245f}, // 560nm
    {0.4087f, 0.5896f}, // 565nm
    {0.4441f, 0.5547f}, // 570nm
    {0.4788f, 0.5202f}, // 575nm
    {0.5125f, 0.4866f}, // 580nm
    {0.5448f, 0.4544f}, // 585nm
    {0.5752f, 0.4242f}, // 590nm
    {0.6029f, 0.3965f}, // 595nm
    {0.6270f, 0.3725f}, // 600nm
    {0.6482f, 0.3514f}, // 605nm
    {0.6658f, 0.3340f}, // 610nm
    {0.6801f, 0.3197f}, // 615nm
    {0.6915f, 0.3083f}, // 620nm
    {0.7006f, 0.2993f}, // 625nm
    {0.7079f, 0.2920f}, // 630nm
    {0.7140f, 0.2859f}, // 635nm
    {0.7190f, 0.2809f}, // 640nm
    {0.7230f, 0.2770f}, // 645nm
    {0.7260f, 0.2740f}, // 650nm
    {0.7283f, 0.2717f}, // 655nm
    {0.7300f, 0.2700f}, // 660nm
    {0.7311f, 0.2689f}, // 665nm
    {0.7320f, 0.2680f}, // 670nm
    {0.7327f, 0.2673f}, // 675nm
    {0.7334f, 0.2666f}, // 680nm
    {0.7340f, 0.2660f}, // 685nm
    {0.7344f, 0.2656f}, // 690nm
    {0.7346f, 0.2654f}, // 695nm
    {0.7347f, 0.2653f}  // 700nm
};

// D65 white point
static const float2 whitePoint = {0.3127f, 0.3290f};

float SpectrumUtils::RGBtoDominantWavelength(const float3& rgb, float& purity)
{
    // Default output
    purity = 0.0f;

    // Normalize RGB values
    float r = std::max(0.0f, rgb.x);
    float g = std::max(0.0f, rgb.y);
    float b = std::max(0.0f, rgb.z);

    // If color is too dark, return 0 (cannot determine wavelength)
    if (r < 0.01f && g < 0.01f && b < 0.01f)
        return 0.0f;

    // Convert to XYZ color space (using sRGB to XYZ matrix)
    float X = 0.4124f * r + 0.3576f * g + 0.1805f * b;
    float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float Z = 0.0193f * r + 0.1192f * g + 0.9505f * b;

    float sum = X + Y + Z;

    // Prevent division by zero and convert to CIE xy chromaticity coordinates
    if (sum < 1e-6f)
        return 0.0f;

    float x = X / sum;
    float y = Y / sum;

    // Calculate vector from white point to current color
    float2 colorVec = {x - whitePoint.x, y - whitePoint.y};
    float colorVecLen = std::sqrt(colorVec.x * colorVec.x + colorVec.y * colorVec.y);

    // If color is too close to white point, cannot determine dominant wavelength
    if (colorVecLen < 1e-6f)
        return 0.0f;

    // Normalize vector
    colorVec.x /= colorVecLen;
    colorVec.y /= colorVecLen;

    // Find intersection with spectral locus
    float minWavelength = 380.0f;
    float wavelengthStep = 5.0f;
    float bestWavelength = 0.0f;
    float bestDotProduct = -1.0f;
    bool isComplement = false;

    for (size_t i = 0; i < spectralLocus.size() - 1; i++) {
        // Calculate vector from white point to spectral locus point
        float2 spectralVec = {
            spectralLocus[i].x - whitePoint.x,
            spectralLocus[i].y - whitePoint.y
        };

        float spectralVecLen = std::sqrt(spectralVec.x * spectralVec.x + spectralVec.y * spectralVec.y);
        if (spectralVecLen < 1e-6f) continue;

        // Normalize spectral vector
        spectralVec.x /= spectralVecLen;
        spectralVec.y /= spectralVecLen;

        // Calculate dot product between vectors
        float dotProduct = colorVec.x * spectralVec.x + colorVec.y * spectralVec.y;

        // If dot product is greater than previous best, update result
        if (std::abs(dotProduct) > std::abs(bestDotProduct)) {
            bestDotProduct = dotProduct;
            bestWavelength = minWavelength + i * wavelengthStep;
            isComplement = (dotProduct < 0);
        }
    }

    // Calculate purity
    // Find closest point on spectral locus
    int index = (int)((bestWavelength - minWavelength) / wavelengthStep);
    index = std::min(std::max(0, index), (int)spectralLocus.size() - 1);

    float2 spectralPoint = spectralLocus[index];

    // Calculate distance from white point to spectral point
    float dws = std::sqrt(
        (spectralPoint.x - whitePoint.x) * (spectralPoint.x - whitePoint.x) +
        (spectralPoint.y - whitePoint.y) * (spectralPoint.y - whitePoint.y)
    );

    // Calculate distance from white point to chromaticity point
    float dc = std::sqrt(
        (x - whitePoint.x) * (x - whitePoint.x) +
        (y - whitePoint.y) * (y - whitePoint.y)
    );

    // Calculate purity = distance to white point / distance to spectral locus
    purity = dc / dws;
    purity = std::min(std::max(0.0f, purity), 1.0f);

    // Return negative wavelength for complement colors
    return isComplement ? -bestWavelength : bestWavelength;
}

// Simplified version without purity
float SpectrumUtils::RGBtoDominantWavelength(const float3& rgb)
{
    float purity;
    return RGBtoDominantWavelength(rgb, purity);
}
} // namespace Falcor
