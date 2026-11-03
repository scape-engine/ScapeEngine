#ifndef SKY_LIGHT_H
#define SKY_LIGHT_H

class Skybox;
struct SkyLightComponent;

/**************************************************************************
 *  Sky Light
 * ----------
 * Captures the sky into a cubemap and lights the scene with it, like UE's
 * Sky Light:
 *  - diffuse: projected onto 9 spherical harmonics coefficients
 *  - specular: GGX prefiltered cubemap, one roughness level per mip
 **************************************************************************/

namespace SkyLight {

void Create();
void Destroy();

// Recapture the sky when needed (real-time, first use or a recapture
// request), then set the sky light uniforms for this frame
void Update(Skybox& skybox, SkyLightComponent& light);

// No sky light in the scene: no ambient lighting
void Disable();

// Lit materials sample the sky light textures (bgfx textures are per draw)
void BindTextures();

}  // namespace SkyLight

#endif  // SKY_LIGHT_H
