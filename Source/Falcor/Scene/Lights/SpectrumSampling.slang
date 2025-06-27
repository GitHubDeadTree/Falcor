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
#pragma once

/** Task 2: GPU-side spectrum sampling for LED lights.
 * This module provides efficient spectrum sampling functions for use in shaders.
 * Note: LightData should be imported by the including file.
 */

// Global spectrum CDF buffer (to be bound externally)
StructuredBuffer<float> gSpectrumCDFData;

/** Sample wavelength from LED spectrum using importance sampling.
 * @param[in] light LED light data containing spectrum information.
 * @param[in] u Random sample in [0,1).
 * @return Sampled wavelength in nm, or default wavelength if no custom spectrum.
 */
float sampleLEDWavelength(LightData light, float u)
{
    if (light.hasCustomSpectrum == 0 || light.spectrumCDFSize == 0) {
        // Default uniform sampling over visible spectrum
        return lerp(380.0f, 780.0f, u);
    }

    uint cdfOffset = light.spectrumCDFOffset;
    uint cdfSize = light.spectrumCDFSize;

    // Linear search through CDF (efficient for small arrays)
    uint sampleIndex = 0;
    uint maxSamples = min(cdfSize, 128u); // Limit to 128 for unrolling

    [unroll(128)]
    for (uint i = 0; i < 128u; ++i) {
        if (i >= maxSamples) break;
        float cdfValue = gSpectrumCDFData[cdfOffset + i];
        if (u <= cdfValue) {
            sampleIndex = i;
            break;
        }
    }

    // Calculate corresponding wavelength
    float wavelengthStep = (light.spectrumMaxWavelength - light.spectrumMinWavelength) / (cdfSize - 1);
    float wavelength = light.spectrumMinWavelength + sampleIndex * wavelengthStep;

    // Linear interpolation within interval
    if (sampleIndex > 0 && sampleIndex < cdfSize) {
        float cdf0 = gSpectrumCDFData[cdfOffset + sampleIndex - 1];
        float cdf1 = gSpectrumCDFData[cdfOffset + sampleIndex];

        if (abs(cdf1 - cdf0) > 1e-6f) {
            float t = (u - cdf0) / (cdf1 - cdf0);
            float wavelength0 = light.spectrumMinWavelength + (sampleIndex - 1) * wavelengthStep;
            float wavelength1 = light.spectrumMinWavelength + sampleIndex * wavelengthStep;
            wavelength = wavelength0 + t * (wavelength1 - wavelength0);
        }
    }

    // Clamp to valid range
    wavelength = clamp(wavelength, light.spectrumMinWavelength, light.spectrumMaxWavelength);

    return wavelength;
}

/** Evaluate probability density function for spectrum sampling.
 * @param[in] light LED light data containing spectrum information.
 * @param[in] wavelength Wavelength to evaluate PDF at (nm).
 * @return PDF value, or uniform PDF if no custom spectrum.
 */
float evaluateSpectrumPDF(LightData light, float wavelength)
{
    if (light.hasCustomSpectrum == 0) {
        // Uniform distribution over visible spectrum
        return 1.0f / (780.0f - 380.0f);
    }

    // For custom spectrum, return uniform PDF over spectrum range
    // In future, this could be improved to return actual intensity-based PDF
    return 1.0f / (light.spectrumMaxWavelength - light.spectrumMinWavelength);
}

/** Check if wavelength is within LED spectrum range.
 * @param[in] light LED light data containing spectrum information.
 * @param[in] wavelength Wavelength to check (nm).
 * @return True if wavelength is within spectrum range.
 */
bool isWavelengthInSpectrumRange(LightData light, float wavelength)
{
    if (light.hasCustomSpectrum == 0) {
        return wavelength >= 380.0f && wavelength <= 780.0f;
    }

    return wavelength >= light.spectrumMinWavelength && wavelength <= light.spectrumMaxWavelength;
}

/** Sample multiple wavelengths for spectral rendering.
 * @param[in] light LED light data containing spectrum information.
 * @param[in] u1 First random sample in [0,1).
 * @param[in] u2 Second random sample in [0,1).
 * @param[in] u3 Third random sample in [0,1).
 * @param[out] wavelength1 First sampled wavelength.
 * @param[out] wavelength2 Second sampled wavelength.
 * @param[out] wavelength3 Third sampled wavelength.
 */
void sampleLEDWavelengths3(LightData light, float u1, float u2, float u3,
                          out float wavelength1, out float wavelength2, out float wavelength3)
{
    wavelength1 = sampleLEDWavelength(light, u1);
    wavelength2 = sampleLEDWavelength(light, u2);
    wavelength3 = sampleLEDWavelength(light, u3);
}
