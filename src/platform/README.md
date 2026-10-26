# `/platform` Directory Overview

The files inside `/platform` are responsible for handling platform-specific abstractions such as HiDPI scaling, native window bindings, and rendering surface setup.

---

## Files

### `platform_utils.h`

**Purpose:**  
Declares a **cross-platform interface** for interacting with platform-specific logic like window size, DPI scaling, and BGFX platform initialization.

**Functions:**

- `GetDrawableSize(SDL_Window* window, int* out_w, int* out_h)`  
  - Returns the actual framebuffer size in physical pixels (e.g., Retina-aware).
- `float GetFramebufferScale(SDL_Window* window)`  
  - Computes the scale factor between logical and drawable resolution.
- `float GetDPIScale(SDL_Window* window)`  
  - Deprecated forwarder to `GetFramebufferScale`.
- `float GetContentScale(SDL_Window* window)` / `void EnableHighDpi()`  
  - The user's display scaling, and opting into DPI awareness (Windows).
- `SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window)`  
  - Populates `bgfx::Init` with the correct `platformData` fields depending on the OS and windowing system.
- `InitSDLForImGui(SDL_Window* window)`  
  - Calls the appropriate `ImGui_ImplSDL2_*` initializer depending on platform (Metal vs. OpenGL/DirectX/etc.).

---

### `platform_utils.cpp`

**Platform(s):** all  
**Purpose:**  
Implements the portable functions from `platform_utils.h` (SDL / std only): window metrics, fullscreen, text input, cursor.

**Highlights:**

- Uses `SDL_GetWindowSizeInPixels` for window metrics, for any graphics backend.

---

### `os/windows.cpp`, `os/linux.cpp`

**Platform:** Windows, Linux  
**Purpose:**  
Implements the OS-specific functions (`GetContentScale`, `EnableHighDpi`, `SetupBGFXPlatformData`, `InitSDLForImGui`, `StyleTitleBar`).

**Highlights:**

- Extracts native handles for DirectX or X11 via `SDL_SysWMinfo`.
- Uses `ImGui_ImplSDL2_InitForOther()` as the ImGui backend initializer.

---

### `os/macos.mm`

**Platform:** macOS  
**Purpose:**  
Implements the OS-specific functions using Cocoa APIs to interact with `NSWindow`, `CAMetalLayer`, and Metal.

**Highlights:**

- Attaches `CAMetalLayer` to `NSView` to prepare rendering surface (internal `CreateMetalLayer`).
- Retrieves HiDPI scale factor from `backingScaleFactor`.
- Sets `bgfx::Init`'s Metal layer handle (`nwh`) using native Cocoa pointers.
- Calls `ImGui_ImplSDL2_InitForMetal()` to initialize ImGui on macOS.

---

## Deprecated / Replaced Files

These older files were merged into `platform_utils.*`:

| Old File | Covered By |
| ---------- | ---------------- |
| `window_macos.h/.mm` | `platform_utils.h/.cpp` (`GetDrawableSize`, `GetDPIScale`), `os/macos.mm` (`CreateMetalLayer`) |
| `bgfx_macos.h/.mm` | `os/macos.mm` (`SetupBGFXPlatformData`) |
| `imgui_macos.h/.mm` | `os/macos.mm` (`InitSDLForImGui`) |
| `platform_utils.mm` | `platform_utils.cpp` + `os/macos.mm` |
