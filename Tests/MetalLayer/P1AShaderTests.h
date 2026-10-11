#pragma once
#include "LayerRenderOperation.h"
void P1AShaderTests();
uint32_t TVPTestP1APixel(uint32_t dst, uint32_t src, const TVPLayerOperation& op);
// Raw software tTVPGLGammaAdjustTempData layout: B[256], G[256], R[256].
uint32_t TVPTestP1AGammaPixel(uint32_t pixel, const TVPLayerOperation& op,
                           const uint8_t* gamma768);
uint32_t TVPTestP1AConstAlphaSDPixel(uint32_t input0, uint32_t input1,
                                 const TVPLayerOperation& op);
