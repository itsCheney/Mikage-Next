#pragma once
#include "LayerRenderOperation.h"
void P1BShaderTests();
// Real initialized PS tables: SoftLight, ColorDodge, ColorBurn, each source*256+dest.
uint32_t TVPTestP1BPixel(uint32_t d,uint32_t s,const TVPLayerOperation& op,
                      const uint8_t* psTables);
