#pragma once

namespace coinop
{

extern const char* const kVertexShader;
extern const char* const kCellShader;

/// Prepended after the #version line for the effect build, which is the only
/// difference between the two bundles' shaders.
extern const char* const kEffectDefine;

} // namespace coinop
