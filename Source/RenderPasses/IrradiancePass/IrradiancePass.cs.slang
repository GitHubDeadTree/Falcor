/***************************************************************************
 # Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
 * Compute shader for calculating irradiance from initial ray direction and intensity.
 */

import Scene.Shading;

cbuffer CB
{
    bool gReverseRayDirection;  // 是否反转光线方向
    float gIntensityScale;      // 强度缩放系数
}

// 输入纹理
Texture2D<float4> gInputRayInfo;       // 初始光线方向(xyz)和强度(w)

// 输出纹理
RWTexture2D<float4> gOutputIrradiance; // 辐照度输出

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;

    // 读取输入的光线方向和强度
    float4 rayInfo = gInputRayInfo[pixelPos];
    float3 rayDir = rayInfo.xyz;
    float intensity = rayInfo.w;

    // 如果需要，反转光线方向
    // 注：光线方向通常是从相机指向场景点，但辐照度计算需要的是光线到达表面的方向
    if (gReverseRayDirection)
    {
        rayDir = -rayDir;
    }

    // 计算辐照度：光线强度在法线方向上的投影
    // 在这个简化模型中，我们假设表面法线与反转后的光线方向相同
    // 完整的模型应考虑实际表面法线
    float3 normal = normalize(rayDir);

    // 辐照度 = 光线强度 * cos(入射角)
    // 由于我们假设法线与光线方向平行，cos(入射角) = 1
    float3 irradiance = float3(intensity, intensity, intensity) * gIntensityScale;

    // 存储辐照度和光线方向
    gOutputIrradiance[pixelPos] = float4(irradiance, 1.0f);
}
