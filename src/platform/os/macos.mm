#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <SDL2/SDL_syswm.h>
#include <backends/imgui_impl_sdl2.h>

#include "platform/platform_utils.h"

namespace Platform {

namespace {

// Creates and attaches a CAMetalLayer to the window's content view, for bgfx
// to render into
void* CreateMetalLayer(NSWindow* window) {
  NSView* content_view = [window contentView];
  [content_view setWantsLayer:YES];

  CAMetalLayer* metal_layer = [CAMetalLayer layer];
  metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metal_layer.framebufferOnly = YES;
  metal_layer.contentsScale = [window backingScaleFactor];
  metal_layer.contentsGravity = kCAGravityTopLeft;

  [content_view setLayer:metal_layer];
  return (__bridge void*)metal_layer;
}

}  // namespace

float GetContentScale(SDL_Window*) {
  return 1.0f;  // points already include the user's display scaling
}

void EnableHighDpi() {}  // Retina from SDL_WINDOW_ALLOW_HIGHDPI

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window) {
  int dw, dh;
  GetDrawableSize(window, &dw, &dh);
  init.resolution.width = dw;
  init.resolution.height = dh;
  init.resolution.reset = BGFX_RESET_VSYNC;

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi)) {
    init.platformData.nwh = CreateMetalLayer(wmi.info.cocoa.window);
    init.platformData.ndt = nullptr;
    init.platformData.context = nullptr;
  }
}

void InitSDLForImGui(SDL_Window* window) {
  ImGui_ImplSDL2_InitForMetal(window);
}

void StyleTitleBar(SDL_Window* window, float r, float g, float b) {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi))
    return;

  NSWindow* ns_window = wmi.info.cocoa.window;
  ns_window.titlebarAppearsTransparent = YES;
  ns_window.backgroundColor = [NSColor colorWithSRGBRed:r green:g blue:b
                                                  alpha:1.0];
  ns_window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  if (@available(macOS 11.0, *))
    ns_window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
}

}  // namespace Platform
