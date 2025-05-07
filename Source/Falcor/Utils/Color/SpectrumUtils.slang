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

/**
 * Utilities for converting tristimulus values to/from spectral data.
 */
struct SpectrumUtils
{
    /**
     * Evaluates the 1931 CIE XYZ color matching curves.
     * This uses a multi-lobe piecewise Gaussian fit described in:
     * Wyman et al., "Simple Analytic Approximations to the CIE XYZ Color
     * Matching Functions", JCGT, vol 2(2), 2013.
     * @param[in] lambda Wavelength in nm.
     * @return XYZ tristimulus values.
     */
    static float3 wavelengthToXYZ(float lambda)
    {
        float x = 0.362f * G(lambda, 442.0f, 0.0624f, 0.0374f) + 1.056f * G(lambda, 599.8f, 0.0264f, 0.0323f) -
                  0.065f * G(lambda, 501.1f, 0.0490f, 0.0382f);
        float y = 0.821f * G(lambda, 568.8f, 0.0213f, 0.0247f) + 0.286f * G(lambda, 530.9f, 0.0613f, 0.0322f);
        float z = 1.217f * G(lambda, 437.0f, 0.0845f, 0.0278f) + 0.681f * G(lambda, 459.0f, 0.0385f, 0.0725f);
        return float3(x, y, z);
    }

    // CIE 1931 standard observer spectral locus points
    // Wavelength range: 380-700nm, step 5nm
    static const float2 spectralLocus[] = {
        float2(0.1741f, 0.0050f), // 380nm
        float2(0.1740f, 0.0050f), // 385nm
        float2(0.1738f, 0.0049f), // 390nm
        float2(0.1736f, 0.0049f), // 395nm
        float2(0.1733f, 0.0048f), // 400nm
        float2(0.1730f, 0.0048f), // 405nm
        float2(0.1726f, 0.0048f), // 410nm
        float2(0.1721f, 0.0048f), // 415nm
        float2(0.1714f, 0.0051f), // 420nm
        float2(0.1703f, 0.0058f), // 425nm
        float2(0.1689f, 0.0069f), // 430nm
        float2(0.1669f, 0.0086f), // 435nm
        float2(0.1644f, 0.0109f), // 440nm
        float2(0.1611f, 0.0138f), // 445nm
        float2(0.1566f, 0.0177f), // 450nm
        float2(0.1510f, 0.0227f), // 455nm
        float2(0.1440f, 0.0297f), // 460nm
        float2(0.1355f, 0.0399f), // 465nm
        float2(0.1241f, 0.0578f), // 470nm
        float2(0.1096f, 0.0868f), // 475nm
        float2(0.0913f, 0.1327f), // 480nm
        float2(0.0687f, 0.2007f), // 485nm
        float2(0.0454f, 0.2950f), // 490nm
        float2(0.0235f, 0.4127f), // 495nm
        float2(0.0082f, 0.5384f), // 500nm
        float2(0.0039f, 0.6548f), // 505nm
        float2(0.0139f, 0.7502f), // 510nm
        float2(0.0389f, 0.8120f), // 515nm
        float2(0.0743f, 0.8338f), // 520nm
        float2(0.1142f, 0.8262f), // 525nm
        float2(0.1547f, 0.8059f), // 530nm
        float2(0.1929f, 0.7816f), // 535nm
        float2(0.2296f, 0.7543f), // 540nm
        float2(0.2658f, 0.7243f), // 545nm
        float2(0.3016f, 0.6923f), // 550nm
        float2(0.3373f, 0.6589f), // 555nm
        float2(0.3731f, 0.6245f), // 560nm
        float2(0.4087f, 0.5896f), // 565nm
        float2(0.4441f, 0.5547f), // 570nm
        float2(0.4788f, 0.5202f), // 575nm
        float2(0.5125f, 0.4866f), // 580nm
        float2(0.5448f, 0.4544f), // 585nm
        float2(0.5752f, 0.4242f), // 590nm
        float2(0.6029f, 0.3965f), // 595nm
        float2(0.6270f, 0.3725f), // 600nm
        float2(0.6482f, 0.3514f), // 605nm
        float2(0.6658f, 0.3340f), // 610nm
        float2(0.6801f, 0.3197f), // 615nm
        float2(0.6915f, 0.3083f), // 620nm
        float2(0.7006f, 0.2993f), // 625nm
        float2(0.7079f, 0.2920f), // 630nm
        float2(0.7140f, 0.2859f), // 635nm
        float2(0.7190f, 0.2809f), // 640nm
        float2(0.7230f, 0.2770f), // 645nm
        float2(0.7260f, 0.2740f), // 650nm
        float2(0.7283f, 0.2717f), // 655nm
        float2(0.7300f, 0.2700f), // 660nm
        float2(0.7311f, 0.2689f), // 665nm
        float2(0.7320f, 0.2680f), // 670nm
        float2(0.7327f, 0.2673f), // 675nm
        float2(0.7334f, 0.2666f), // 680nm
        float2(0.7340f, 0.2660f), // 685nm
        float2(0.7344f, 0.2656f), // 690nm
        float2(0.7346f, 0.2654f), // 695nm
        float2(0.7347f, 0.2653f)  // 700nm
    };

    // D65 white point
    static const float2 whitePoint = float2(0.3127f, 0.3290f);

    /**
     * Converts RGB color to dominant wavelength and purity.
     * Based on CIE 1931 chromaticity diagram for physical accuracy.
     * @param[in] rgb RGB color value, range [0,1].
     * @param[out] purity Returns the color's purity value (0-1).
     * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
     */
    static float RGBtoDominantWavelength(float3 rgb, out float purity)
    {
        // Default output
        purity = 0.0f;

        // Normalize RGB values
        rgb = max(0.0f, rgb);

        // If color is too dark, return 0 (cannot determine wavelength)
        if (all(rgb < 0.01f))
            return 0.0f;

        // Convert to XYZ color space (using sRGB to XYZ matrix)
        float X = 0.4124f * rgb.r + 0.3576f * rgb.g + 0.1805f * rgb.b;
        float Y = 0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b;
        float Z = 0.0193f * rgb.r + 0.1192f * rgb.g + 0.9505f * rgb.b;

        float sum = X + Y + Z;

        // Prevent division by zero and convert to CIE xy chromaticity coordinates
        if (sum < 1e-6f)
            return 0.0f;

        float x = X / sum;
        float y = Y / sum;

        // Calculate vector from white point to current color
        float2 colorVec = float2(x - whitePoint.x, y - whitePoint.y);
        float colorVecLen = length(colorVec);

        // If color is too close to white point, cannot determine dominant wavelength
        if (colorVecLen < 1e-6f)
            return 0.0f;

        // Normalize vector
        colorVec /= colorVecLen;

        // Find intersection with spectral locus
        float minWavelength = 380.0f;
        float wavelengthStep = 5.0f;
        float bestWavelength = 0.0f;
        float bestDotProduct = -1.0f;
        bool isComplement = false;

        for (int i = 0; i < 65; i++) {
            // Calculate vector from white point to spectral locus point
            float2 spectralVec = float2(
                spectralLocus[i].x - whitePoint.x,
                spectralLocus[i].y - whitePoint.y
            );

            float spectralVecLen = length(spectralVec);
            if (spectralVecLen < 1e-6f) continue;

            // Normalize spectral vector
            spectralVec /= spectralVecLen;

            // Calculate dot product between vectors
            float dotProduct = dot(colorVec, spectralVec);

            // If dot product is greater than previous best, update result
            if (abs(dotProduct) > abs(bestDotProduct)) {
                bestDotProduct = dotProduct;
                bestWavelength = minWavelength + i * wavelengthStep;
                isComplement = (dotProduct < 0);
            }
        }

        // Calculate purity
        // Find closest point on spectral locus
        int index = int((bestWavelength - minWavelength) / wavelengthStep);
        index = clamp(index, 0, 65);

        float2 spectralPoint = spectralLocus[index];

        // Calculate distance from white point to spectral point
        float dws = length(spectralPoint - whitePoint);

        // Calculate distance from white point to chromaticity point
        float dc = length(float2(x, y) - whitePoint);

        // Calculate purity = distance to white point / distance to spectral locus
        purity = dc / dws;
        purity = clamp(purity, 0.0f, 1.0f);

        // Return negative wavelength for complement colors
        return isComplement ? -bestWavelength : bestWavelength;
    }

    /**
     * Simplified version that converts RGB color to dominant wavelength without returning purity.
     * @param[in] rgb RGB color value, range [0,1].
     * @return Estimated dominant wavelength (nm), negative value for non-spectral colors (complement), or 0 if unable to estimate.
     */
    static float RGBtoDominantWavelength(float3 rgb)
    {
        float purity;
        return RGBtoDominantWavelength(rgb, purity);
    }

    /**
     * Gets the effective wavelength value, handling complement colors.
     * @param[in] dominantWavelength Potentially negative dominant wavelength
     * @return Effective wavelength value, converting complement colors to actual wavelength
     */
    static float getEffectiveWavelength(float dominantWavelength)
    {
        // For negative values (complement colors), use absolute value as effective wavelength
        if (dominantWavelength < 0.0f)
            return abs(dominantWavelength);
        else
            return dominantWavelength;
    }

    /**
     * Calculates light intensity from RGB color.
     * @param[in] rgb RGB color value.
     * @return Luminance value representing light intensity.
     */
    static float calculateLightIntensity(float3 rgb)
    {
        // Use standard luminance calculation (Rec. 709 coefficients)
        return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    }

    // Internal

    static float G(float x, float lambda, float invSigma1, float invSigma2)
    {
        float t = (x - lambda) * (x < lambda ? invSigma1 : invSigma2);
        return exp(-0.5f * t * t);
    }
};
