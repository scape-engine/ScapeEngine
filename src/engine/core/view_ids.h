#pragma once
#include <array>
#include <cstdint>

namespace ViewID {
// view IDs must be unique per frame
constexpr uint8_t REFLECTION_PASS = 0;  // for reflection texture
constexpr uint8_t UI_BACKGROUND = 1;    // back-buffer UI

// scene FBO (colour+depth)
constexpr uint8_t SCENE_TERRAIN = 2;
constexpr uint8_t SCENE_CLEAR = 3;  // clears the stencil of the whole scene FBO
constexpr uint8_t WATER_PASS = 4;
constexpr uint8_t DEBUG_PASS = 5;

// shadow map (separate FBO)
constexpr uint8_t SHADOW_PASS = 6;

// For individual glTF game objects
constexpr uint8_t SCENE_FORWARD = 7;
constexpr uint8_t SCENE_SKYBOX = 8;

// For preview thumbnails
constexpr uint8_t PREVIEW_PASS_BASE = 50;
constexpr uint8_t PREVIEW_PASS_MAX = 150;

// miscellaneous
constexpr uint8_t UI = 254;
constexpr uint8_t COUNT = 255;

/// every pass that is rendered into scene_framebuffer_
inline constexpr std::array<uint8_t, 5> kSceneViews = {
    SCENE_SKYBOX, SCENE_TERRAIN, WATER_PASS, DEBUG_PASS, SCENE_FORWARD};

}  // namespace ViewID