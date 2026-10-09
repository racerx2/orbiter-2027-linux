// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.

// not upstream: split from stress.cpp so only this file is built with -mavx -mfma (GCC/Clang)

#include "stress.h"
#include "math/cgnr.h"


typedef CGNR<AngLin6, AngLin6Ops<SIMD_Scalar>, BondMatrixS, BondMatrixOpsS<SIMD_Scalar>, SIMD_Scalar, AngLin6ErrorSq>       CGNR_SIMD;


size_t
StressSimdCacheSize(uint32_t N_nodes, uint32_t N_bonds)
{
    return CGNR_SIMD().required_cache_size(N_nodes, N_bonds);
}


int
StressSimdSolve(AngLin6* impulses, const BondMatrixS& B, const AngLin6* b, uint32_t N_nodes, uint32_t N_bonds, void* cache, AngLin6ErrorSq* error_sq, float tolerance, uint32_t maxIter, unsigned warmth)
{
    return CGNR_SIMD().solve(impulses, B, b, N_nodes, N_bonds, cache, error_sq, tolerance, maxIter, warmth);
}
