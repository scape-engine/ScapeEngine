#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <SDL2/SDL_syswm.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>

#include "platform_utils.h"
#include "engine/core/logger.h" // ! remove engine dependency

#if defined(__APPLE__)

namespace Platform {

void GetDrawableSize(SDL_Window* window, int* out_w, int* out_h) {
    SDL_Metal_GetDrawableSize(window, out_w, out_h);
}

float GetDPIScale(SDL_Window* window) {
    int logicalW, logicalH;
    SDL_GetWindowSize(window, &logicalW, &logicalH);

    int drawableW, drawableH;
    SDL_Metal_GetDrawableSize(window, &drawableW, &drawableH);

    float scale = logicalW > 0 ? (float)drawableW / (float)logicalW : 1.0f;
    
    // Logger::getInstance().Log(LogLevel::Debug, "macOS DPI Scale: " + std::to_string(scale)); // DEBUG only
    return scale;
}

void* CreateMetalLayer(void* cocoaWindow) {
    NSWindow* nsWindow = (__bridge NSWindow*)cocoaWindow;
    NSView* contentView = [nsWindow contentView];
    [contentView setWantsLayer:YES];

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = YES;
    metalLayer.contentsScale = [nsWindow backingScaleFactor];

    [contentView setLayer:metalLayer];

    Logger::getInstance().Log(LogLevel::Debug, "Created CAMetalLayer");
    return (__bridge void*)metalLayer;
}

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window) {
  int dw, dh;
  GetDrawableSize(window, &dw, &dh);
  init.resolution.width = dw;
  init.resolution.height = dh;
  init.resolution.reset = BGFX_RESET_VSYNC;

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi)) {
    void* layer = CreateMetalLayer((__bridge void*)wmi.info.cocoa.window);
    init.platformData.nwh = layer;
    init.platformData.ndt = nullptr;
    init.platformData.context = nullptr;
  }
}

void InitSDLForImGui(SDL_Window* window) {
  ImGui_ImplSDL2_InitForMetal(window);
}

void ToggleBorderlessFullscreen(SDL_Window* w, bool enable)
{
    if (enable)
        Platform::RestoreMinSize(w);                                   // let Cocoa expand
    SDL_SetWindowFullscreen(w,
            enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

    if (!enable)
        Platform::LockMinSize(w, 1280, 720);                           // restore window clamp
    Platform::RefreshFramebufferSize(w);
}

bool IsBorderlessFullscreen(SDL_Window* win)
{
    return SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;            
}

void RefreshFramebufferSize(SDL_Window* win)
{
    int dw, dh;
    SDL_Metal_GetDrawableSize(win, &dw, &dh);                                  
    bgfx::reset(dw, dh, BGFX_RESET_VSYNC);                                     

    ImGuiIO& io = ImGui::GetIO();
    float dpi   = GetDPIScale(win);
    io.DisplaySize             = ImVec2(dw / dpi, dh / dpi);
    io.DisplayFramebufferScale = ImVec2(dpi, dpi);
    
    if (dw == 0 || dh == 0)           // window is minimised or off-screen
    return;          // skip reset
}

bool InFullscreenSpace(SDL_Window* w)
{
    return SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void LockMinSize(SDL_Window* w, int minW, int minH)
{
    SDL_SetWindowMinimumSize(w, minW, minH);
    SDL_SetWindowMaximumSize(w, 0, 0);        // 0 = can enlarge
}

void RestoreMinSize(SDL_Window* w)
{
    SDL_SetWindowMinimumSize(w, 0, 0);        // remove lower bound
}

void StyleTitleBar(SDL_Window* window, float r, float g, float b) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi))
        return;

    NSWindow* nsWindow = wmi.info.cocoa.window;
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.backgroundColor = [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
    nsWindow.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]; 
    if (@available(macOS 11.0, *))
        nsWindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
}

void DisableTextInput() {
    SDL_StopTextInput();
}

void EnableTextInput() {
    SDL_StartTextInput();
}

bool IsTextInputActive() {
    return SDL_IsTextInputActive() == SDL_TRUE;
}

// TODO: move to input/cursor
glm::vec2 GetCursorPosition() {
  int x, y;
  SDL_GetMouseState(&x, &y);
  return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void SetCursorPosition(SDL_Window* window, const glm::vec2& pos) {
  SDL_WarpMouseInWindow(window, static_cast<int>(pos.x), static_cast<int>(pos.y));
}

glm::vec2 GetGlobalCursorPosition() {
  int x, y;
  SDL_GetGlobalMouseState(&x, &y);
  return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void SetGlobalCursorPosition(const glm::vec2& pos) {
  SDL_WarpMouseGlobal(static_cast<int>(pos.x), static_cast<int>(pos.y));
}

}  // namespace Platform

#endif
