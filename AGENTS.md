# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

A **Vulkan 1.4 renderer skeleton** exists (`include/`, `src/`, `app/`): window, swapchain, resize
handling, clean shutdown, indexed meshes drawn through a graphics pipeline with a per-frame camera UBO
and per-draw model UBOs bound through descriptor sets, and textures loaded from files — all with
validation + synchronization validation on and **zero output**. It is being built chapter by chapter
along the official tutorial (https://docs.vulkan.org/tutorial/latest/). A first continent walking
prototype now exists: tiled terrain, a full-scale blockout city, one ground-level player controller,
and static collision. The world now has named regions (all five terrain types), six location types
and a validated, walkable road network; see `docs/world-map.md`. Progression, combat, NPC pathfinding and persistent city simulation are not
implemented yet.

`docs/` holds the design draft and stays in Chinese; keep new design docs in Chinese too.

## The game (from `docs/content.md`)

Genre: RPG / strategy sandbox / real-time strategy (RPG，策略沙盒，即时战略).
Goal: a story-driven main line (故事主线) with immersion (沉浸感).

Core loop: collect resources → build army → attack cities → upgrade army → attack stronger cities.

Three module groups, each combining in-game (局内) and out-of-game (局外) systems — this split is a
load-bearing design axis, not just vocabulary:

- **养成 (progression)** — protagonist (level, skills); army (recruitment, training, with **each
  individual soldier owning their own equipment**); equipment upgrades; 城池建设 as the out-of-game
  economic layer (resource production).
- **战斗 (combat)** — 城池建设 again but as the in-game layer (defenses, troop deployment); combat
  consumes troops, time, and equipment.
- **地图 (map)** — one consistent camera perspective for countryside, cities, and combat, with
  WASD movement. Cities occupy large, traversable spaces with walls, gates, streets and buildings,
  at the same spatial scale as the character and countryside. This replaces the earlier world-map /
  top-down RTS-map split (user decision, 2026-09-29). The current requested view is fixed oblique,
  like Don't Starve / Red Alert: `update_player2` follows the player using a perspective projection
  (user decision, 2026-09-30), a visible cursor and wheel zoom. The vertical FOV is fixed at 45 degrees;
  zoom moves the camera along its viewing direction. This is the default view inside and outside cities.
  M toggles between `update_player2` and first-person `update_player`, sharing player position and
  collision. First-person captures the cursor; returning releases it and recenters while preserving
  zoom and the Y lock state.
  Y toggles following the player; while unlocked, edge pan moves the camera independently and
  Space recenters (holding temporarily follows without changing the lock).
  The loading strategy is not yet decided; a consistent perspective does not require loading the entire world
  at once. See `docs/continent.md` for the proposed spatial design.

Note that 城池建设 (city building) deliberately appears in both 养成 and 战斗 — it is the same system
serving an out-of-game economic role and an in-game defensive/deployment role. Similarly, the
per-soldier equipment model ties the army system to the equipment system. When implementing, treat
these as shared systems with two modes rather than duplicated features.
The in-game / out-of-game distinction describes gameplay state, not a camera or map-scale change.
The prototype uses one `Gameplay` input context; the previous `WorldMap` / `Minimap` contexts
have been removed. M now toggles camera views within Gameplay, not input contexts.
Input routing and the UI context remain useful independently.

## Rendering stack (decided 2026-09-20)

**Raw Vulkan 1.4 core, written against the vulkan-hpp binding. No third-party rendering abstraction**
— no bgfx, SDL_GPU, Diligent, wgpu, or The-Forge. The line is *binding vs abstraction*, not C vs C++:

- **In scope:** `vulkan-hpp` (the `vk::` types and the `vk::raii` wrappers) and `VMA` via VMA-Hpp (allocates
  memory). Both are generated from the same `vk.xml`: same call sequence, no hidden state, no runtime
  behaviour of their own. `volk` was in this list until 2026-09-21 and was **removed** — see "One
  loader" below; it had been reduced to one `dlopen` that `vk::raii::Context` now does itself.
- **Out of scope:** anything that makes you call something that isn't a Vulkan entry point.

This replaced an earlier, stricter wording — *"if a library makes you call something that isn't `vk*`,
it is the wrong layer"* — which vulkan-hpp violates by the letter while satisfying the intent. What that
rule was protecting against is a library choosing your synchronization, your memory, or your lifetimes.
vulkan-hpp chooses none of those. `vk::raii` does decide *when* objects die, but it does so by putting
lifetime in the type and in the scope that holds it, not by hiding a device or a queue behind an opaque
handle.

**The one real cost:** VUIDs are named after C functions (`VUID-vkAcquireNextImageKHR-semaphore-01286`),
while an hpp call site reads `swapchain.acquireNextImage(...)`. The method name still contains the
function name body, so grep mostly works, but a VUID no longer maps verbatim onto a source line. Expect
to translate when chasing a validation message.

GLFW for windowing. Target is desktop, Linux first, **Windows plausible** — which is why every
dependency comes from conan. Explicitly **not** targeting macOS (caps at Vulkan 1.2) or consoles (none
use Vulkan) without revisiting this.

## Build

**The build tree is `build/<platform>/<build_type>/`** — `build/linux-x86_64/Debug/`,
`build/windows-x86_64/Release/`. `<platform>` is the *target* os and arch, so a MinGW cross build
running on Linux lands in `build/windows-x86_64/`, beside nothing it shares a CMake cache with. Each
tree is self-contained: its own conan generators, its own VMA-Hpp FetchContent download, its own
compiled shaders.

`conanfile.py`'s `layout()` pins that shape instead of calling `cmake_layout()`, which adds the
build-type level only for single-config generators — with Visual Studio it would collapse to
`build/<platform>/` with the build type living in the CMake cache. Pinning it makes the shape a
property of the project rather than of the generator.

```bash
make                                            # Linux native Debug: install + configure + build
make run                                        # the above, then run it
make build PLATFORM=windows-x86_64 BUILD_TYPE=Release
make deps  PLATFORM=windows-x86_64              # conan install only
```

`make` drives three steps, which by hand are:

```bash
conan install . -pr:h profiles/linux -pr:b default --build=missing -s build_type=Debug
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug
```

| `PLATFORM` | host profile | target | runnable here |
|---|---|---|---|
| `linux-x86_64` | `profiles/linux` | Linux, clang | yes |
| `windows-x86_64` | `profiles/mingw64` | Windows, MinGW-w64 cross | yes, via wine |

Anything else errors out at `make` parse time. The build profile is always `default` — that is the
machine doing the compiling, which stays Linux even when the target is Windows — and conan sets
`CMAKE_SYSTEM_NAME=Windows` on its own when the two disagree.

**`JOBS` caps parallelism, and defaults to 8 rather than `nproc`.** The first `--build=missing` for a
new platform compiles every dependency from source, and that is the only step where this matters; it
goes to conan as `tools.build:jobs` (which conan also bakes into the generated build preset) and to
`cmake --build` as `-j`. The cap is about RAM, not cores — 32 concurrent C++ translation units on this
15 GB machine is what makes the whole desktop stutter, while the dependencies themselves are small once
the validation layers are out of the graph. `make JOBS=$(nproc)` when nothing else is running.

**Cross-compiling to Windows has two wrinkles that are not about profiles.**

The first is at link time. GCC keeps the non-inline half of `<print>` in a separate `libstdc++exp`,
and that is where `vprint_unicode`'s terminal helpers live. Linux never notices — `libstdc++.so.6`
exports `std::__open_terminal` and `std::__write_to_terminal` already — but MinGW's `libstdc++.dll.a`
does not, so every one of the 23 targets compiles and the *link* dies with
`undefined reference to 'std::__open_terminal(_iobuf*)'`, which reads like libstdc++ is missing a
standard function. `CMakeLists.txt` links `stdc++exp` for `MINGW` only; it is on `lc1_engine` because
`std::println` is used by the library, not just the app.

The second is that a MinGW link pulls the compiler's own runtime in as **DLLs** —
`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`. Those exist on the machine that
compiled the exe and nowhere it is meant to run, so `CMakeLists.txt` passes `-static` to `lc1` for
`MINGW`, and `objdump -p` on the result lists only `KERNEL32`/`USER32`/`GDI32`/`SHELL32`/`msvcrt`.
Nothing else in the link is affected: conan already builds every dependency static
(`*:shared=False`), and the Vulkan loader is `dlopen`ed rather than linked. The flag is in CMake
rather than in `profiles/mingw64`'s `tools.build:exelinkflags` deliberately —
`CMAKE_EXE_LINKER_FLAGS_INIT` is read only on a tree's **first** configure, so a profile-only change
would silently do nothing to an existing tree, and the exe would look fine until it was copied
somewhere else.

**`make run PLATFORM=windows-x86_64` runs the exe under wine**, in a prefix at `build/wine-prefix`
(inside the gitignored tree so `make clean` takes it along, and not `~/.wine`, so a build artifact
does not share a `C:` drive with everything else the machine uses wine for). Verified end to end: the
exe loads the mesh and texture, enumerates the real GPU through winevulkan, creates a swapchain and
renders — which is what makes the cross build testable without leaving the machine.

One thing it needs is not obvious: a **Debug exe asks for `VK_LAYER_KHRONOS_validation` and stops if
it is missing**, and here it is missing — `profiles/mingw64` deliberately does not build the layers,
and a fresh wine prefix has none. On a real Windows box the layers come from the Vulkan SDK, which is
also why not shipping them is normal. Locally,
`LC1_NO_VALIDATION=1 make run PLATFORM=windows-x86_64` is the switch `main.cpp` already has for this.
The Makefile prints that hint when a run stops but does not set it for you: silently disabling the
safety net on every wine run is how it stops being one.

Four parts of this are easy to get wrong:

- **`-pr:h profiles/linux` is required.** The default profile is missing options the graph needs, and
  omitting it fails with a confusing `xorg/system` error about missing X11 `-devel` packages.
- **`-s build_type=Debug` is required on every install.** The profiles default to `Release`, and Conan
  only writes presets for build types it actually generated.
- **The preset name carries the platform: `linux-x86_64-debug`, not `conan-debug`.** One
  `CMakeUserPresets.json` at the repo root includes every platform's generated presets at once, and
  CMake refuses to read a file whose includes define the same name twice
  (`CMake Error: Duplicate preset`). The default prefix is `conan` for every install, so `conanfile.py`
  sets `CMakeToolchain.presets_prefix` to the platform — it is also why `generate()` instantiates the
  generators by hand instead of using the `generators = ...` attribute.
- **Run `make run`, not `./build/linux-x86_64/Debug/lc1`.** It does two things the bare binary lacks:
  - It sources the conan run env. Debug builds take the validation layers from conan, and the loader
    finds them only via `VK_LAYER_PATH`, which that env exports. Without it, startup fails with a
    "layer not found" error that looks like the layer is missing when it is not.
  - It runs from the build tree. `shaders/shader.spv` is opened by a path relative to the working
    directory, and the build puts it next to the binary alongside the copied `assets/` directory. Resource IDs are relative to that
    asset root; `LC1_ASSET_ROOT` can override its location.

  Release builds have no layer to find, but still need the working directory.

## Dependency rules — all from conan

Every dependency is conan-managed, on every platform, so there is one build story — with one exception:
**VMA-Hpp is not on ConanCenter**, so `third-party/CMakeLists.txt` fetches a pinned release tarball
(URL + SHA256) with FetchContent. It bundles its own `vk_mem_alloc.h`; do not also add conan's
`vulkan-memory-allocator`, or two VMA versions compete for the same include. Specifics that look
redundant and are not:

- **`lc1/vk/memory.hpp` includes `common.hpp` before VMA.** VMA must see `VK_NO_PROTOTYPES`; otherwise
  `VMA_IMPLEMENTATION` (in `src/vk/memory.cpp`) turns on `VMA_STATIC_VULKAN_FUNCTIONS` and references
  `vk*` symbols that nothing links. The allocator is built with `vma::raii::Allocator{instance, device,
  info}`, which requires `info.instance`, `info.device` and `info.pVulkanFunctions` to be null — it fills
  them from the raii dispatchers itself.

- **Do NOT add `vulkan-loader`.** The loader is the system's, reached by exactly one `dlopen`:
  `lc1::VulkanLoader`'s `vk::raii::Context` (`libvulkan.so` → `libvulkan.so.1` on Linux,
  `vulkan-1.dll` on Windows), which hands its `vkGetInstanceProcAddr` to GLFW through
  `glfwInitVulkanLoader`, so GLFW never loads one of its own. Measured, not assumed: `ldd` on the binary
  shows no `libvulkan`, and `LD_DEBUG=libs ./build/linux-x86_64/Debug/lc1` shows exactly one `find library=libvulkan`
  (two until 2026-09-24, when GLFW still dlopened `libvulkan.so.1` itself). A conan loader could only be
  a *second* one: `Context{}` dlopens by name regardless of what is linked, and `VK_NO_PROTOTYPES` means
  nothing would call into the linked copy. Holds on both platforms.
- **GLFW comes from conan, with `with_wayland=True` on Linux.** Its recipe defaults to
  `with_wayland=False`, an X11-only build.
- **Validation layers come from conan, Debug only.** They are a dev tool; Release does not need them,
  and building them from source is expensive. Version skew against the system loader is harmless.
  `profiles/mingw64` turns them off even for Debug, through the `with_validation_layers` option in
  `conanfile.py`: a cross-compiled binary cannot be run on the machine building it, so the layers would
  be a from-source build of one of the largest CMake projects there is — 30+ minutes with all cores
  saturated — for something nothing here executes. This is the single biggest lever on how long a
  first-time `--build=missing` for a new platform takes.

## The profile is load-bearing

`profiles/linux` is not boilerplate. **A profile-level option outranks anything a recipe — or a
consumer conanfile's `configure()` — sets for its own dependencies.** Putting these in `conanfile.py`
silently does nothing and the graph fails to resolve. Conan says as much: *"It is recommended to
define options values in profiles, not in recipes."*

The profile currently encodes **native Wayland only, no X11**:

| option | why |
|---|---|
| `wayland/*:shared`, `xkbcommon/*:shared` | glfw always dlopens them; static ones fail `validate()` |
| `glfw/*:with_x11=False`, `xkbcommon/*:with_x11=False` | drops `xorg/system`, which demands ~15 X11 `-devel` RPMs |
| `vulkan-validationlayers/*:with_wsi_xcb/xlib=False` | same — the layers pull `xorg/system` too |

**Trade-off:** the resulting binary will not run on an X11 session. To restore X11 support, delete the
four `with_x11`/`with_wsi_*` lines and install the X11 dev packages (manually, or via
`-c tools.system.package_manager:mode=install`). conan cannot vendor X11 headers — they *are* the
system integration layer — so this is the one place "all dependencies from conan" has a real limit.

`profiles/mingw64` is the Windows sibling the earlier version of this section said would be needed. It
drops the four Wayland/X11 lines outright — Windows glfw uses Win32 — and replaces clang with a
target-triple-prefixed MinGW-w64 gcc, including `rc`, which CMake passes `.rc` files through. It does
not `include(default)`: the default profile describes the Linux machine compiling, this one describes
the Windows binaries it produces, and conan's cross-compilation handling is driven by exactly that
disagreement. The one thing it adds that is not about the compiler is
`lc1/*:with_validation_layers=False` — see the dependency-rules section.

## Coding conventions

**The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) are the
standard here**, plus two project additions: `snake_case` variables, and no naming prefixes. The table
below is what those resolve to in `include/` and `src/`, which already follows it.

**Use the repository's [`.clang-format`](.clang-format) for code formatting.** Run
`clang-format --style=file -i <changed .hpp/.cpp files>` on new or modified C++ files before
finishing a change. The checked-in configuration is authoritative; do not substitute a built-in
style or manually impose different formatting.

| element | style | example |
|---|---|---|
| class / struct / enum / exception | `PascalCase` | `FrameLoop`, `SwapchainImage`, `FrameResult`, `Error` |
| enum *enumerator* | `PascalCase` | `FrameResult::RecreateRequested` |
| function / method, incl. private | `snake_case` | `draw_frame`, `wait_idle`, `result_string` |
| variable / parameter / constant | `snake_case` | `image_index`, `frames_in_flight` |
| private data member | `snake_case` + trailing `_` | `handle_`, `frame_index_` |
| macro | `ALL_CAPS` — macros *only* (NL.9) | `VK_CHECK` |

**No prefix that encodes type, scope, or storage.** `pInstance` is the obvious one, but `g_`, `s_`,
`m_`, and `k` are the same mistake wearing a different hat — they restate something the compiler
already knows and that the declaration puts in front of you anyway. The trailing `_` on private
members stays: it distinguishes member from parameter *without* a prefix, which is the whole point.
Scope is not lost by dropping `g_` — file-local state lives in an anonymous namespace.

**Vulkan's own spelling is exempt, and this is the one carve-out worth stating.** `vk::`, `VK_`,
`VkImage`, `sType`, `pNext`, and hpp's `eColorAttachmentOptimal` enumerators are the *API's* names, not
ours. They are not renamed, shortened, or aliased: they are what the spec, the VUIDs, and every
StackOverflow answer are written in, and the whole point of this section is a grep that lands.
That is the same reasoning the four invariants above rely on — match by argument shape, not spelling.

**Headers are `.hpp`, sources `.cpp`.** This departs from SF.1's `.h` default on purpose: `.h` is
what the C headers this project includes use (`vulkan_core.h`, `GLFW/glfw3.h`), so `.hpp` marks
every file of ours as C++-only at a glance — the same split vulkan-hpp itself draws between
`vulkan.h` and `vulkan.hpp`. Every header is `#pragma once`.

Beyond naming, the Guidelines that this codebase actually leans on, so a change that breaks one is a
change to argue for rather than slip in:

- **RAII and no manual resource management** (R.11, R.20, C.30). No `new`/`delete`, no owning raw
  pointers or handles, no `vkCreate*`/`vkDestroy*` pairs — see the architecture section for why
  `vk::raii` plus member declaration order is doing that job here.
- **A raw handle is non-owning, and says so** (R.3). `SwapchainImage::image` is the case, it is
  commented as an invariant, and a second one needs the same treatment, not a `unique_ptr` wrapper
  invented on the spot.
- **`Window`'s `GLFWwindow *` is the one *owning* raw handle**, and it is a carve-out with a reason
  rather than an exception: GLFW's C API has no raii form, so `Window` *is* the wrapper — `~Window` is
  the single destroy site, and the member comment says exactly that. GLFW is also the only resource
  here that is not created through `vk::raii`, which is why it needs the one hand-written wrapper and
  everything else does not.
- **`vk::` structs are built with designated initializers.** `VULKAN_HPP_NO_STRUCT_CONSTRUCTORS` in
  `common.hpp` is what makes them aggregates — C++ designated initializers need an aggregate, and hpp's
  generated constructors would stop them being one. Why that matters: one expression means no half-filled object for a later statement to observe, every
  field is named where it is set, and `sType`/`pNext` come from hpp's default member initializers so
  omitting them is safe rather than a bug. Two rules that follow:
  - **Fields must be in declaration order.** C++ requires it (C does not), so `.pEngineName` before
    `.pApplicationName` will not compile. Skipping fields is fine.
  - **Setters stay, and are not a competing style.** They are the *checked* way to fill an array
    member: `setPEnabledLayerNames` assigns `enabledLayerCount` and `ppEnabledLayerNames` together from
    one `ArrayProxy`, while designated initialization can only write those two fields separately with
    nothing to keep them in agreement. Do not reach for `VULKAN_HPP_NO_SETTERS`.

- **`const` and `constexpr` by default** (Con.1, ES.20). A parameter that is not mutated is
  `const&` or by value, never `&`. Compile-time constants are `constexpr` in the type that owns the
  concept, e.g. `FrameLoop::frames_in_flight`.
- **No C-style casts** (ES.49). `static_cast` for value conversions; `reinterpret_cast` only where
  the C API forces it, with a comment saying which call forces it.
- **Pass views, not pointer+length** (I.13, R.14, F.24). `std::string_view` for read-only string
  parameters, like `Window`'s `title`; no array-and-count pairs in new interfaces.
- **No exception escapes `main`** — see "Exceptions" below. Nothing new derives from `lc1::Error`,
  and no `std::error_code`-and-out-param pairs get introduced alongside it.
- **World space is right-handed with Y up** (`FpsCamera::world_up`), the OpenGL convention and
  glm's default. glTF is Y-up too, so glTF assets need no conversion. The tutorial's chapters up to
  glTF use Z-up (`lookAt` with up = (0,0,1)); translate them rather than copying the up vector. Its
  `viking_room.obj` is Z-up and needs −90° about X.
  - Vulkan's clip-space Y points down, where glm's points up. `Renderer::record` flips it with a
    negative viewport height (and `y` = height, or the viewport lands off-screen). The base front face
    is `eCounterClockwise`. `eFrontFace` is dynamic: before each draw Renderer selects clockwise
    when the model's 3x3 determinant is negative, otherwise counter-clockwise. Back-face culling
    stays enabled; mirrored transforms need no caller-side cull-mode changes.

### Descriptor set layout convention

**Assign higher set numbers to bindings that change more frequently during drawing.** Frequency
means descriptor-set binding changes (including dynamic-offset changes), not writes to buffer contents.
For the per-object rendering path, where an object can have multiple material submeshes, the target
layout is:

| set | data | expected binding changes |
|---|---|---|
| 0 | camera / global | once per frame or view |
| 1 | object | per draw; dynamic offsets may be used in a later implementation |
| 2 | material | as needed per submesh |

The renderer and shader implement this layout. The first version uses a separate model UBO and
descriptor set per draw per frame slot; dynamic UBO offsets are a possible later optimization. Keep low-numbered set layouts shared across pipelines where possible. Vulkan's
ordinary pipeline-layout compatibility for set N requires identically defined set layouts from 0
through N and identical push-constant ranges; matching only set N is insufficient. Putting layout
variation at higher set numbers helps preserve compatible lower-set bindings.

Treat frequency ordering as a design default, not a reason to renumber sets whenever draw sorting
changes. Stable layouts and cross-pipeline compatibility take priority over matching every draw
order's measured binding frequency.

## Architecture

Init order is load-bearing — device *selection* needs the surface, and each stage must be up before the
next is constructed:

```
VulkanLoader(dlopen) -> Window(glfwInit + GLFWwindow) -> Instance(loader)
  -> Surface(instance, window) -> Device(instance, surface)
  -> Renderer -> FrameLoop<FrameResources>
  -> Swapchain (created lazily when the framebuffer has a usable extent)
```

Both GLFW steps are inside `Window`'s constructor, and the loader `dlopen` is inside `VulkanLoader`'s,
which is what makes the ordering enforceable rather than conventional: `Instance` takes the loader and
`Surface` takes the instance and window, while `Device` borrows the surface during selection, and there is no way to leave the scope
without terminating GLFW. The one order the types do *not* enforce is `VulkanLoader` before `Window`:
GLFW reads the loader it is handed only in `glfwInit`, so the other order silently makes GLFW dlopen
`libvulkan.so.1` itself.

**`VulkanLoader` is declared first in `main()`'s scope and `Window` second, so they are destroyed
last**, in reverse. `VulkanLoader` must be the very last: `~vk::raii::Context` `dlclose`s the library
every function pointer points into, GLFW's included. `Window` must follow every Vulkan object. On
Wayland that ordering is load-bearing rather than tidy: `glfwCreateWindowSurface` builds the
`VkSurfaceKHR` from GLFW's `wl_surface`, so a `glfwTerminate` that ran before the surface died would
pull the native surface out from under a live Vulkan object.

### One loader, reached once

Every module is on `vk::raii`. The whole chain descends from a single `vkGetInstanceProcAddr`:
the `vk::raii::Context` in `VulkanLoader` dlopens the loader and resolves it, and from there each
object builds its own dispatcher — Context → Instance → PhysicalDevice → Device → Queue. GLFW is
handed the same pointer, so `glfwCreateWindowSurface` resolves through the same loader too.

The `Context` is a long-lived member, not a local, because its destructor `dlclose`s. Until 2026-09-24
it was a local in `Instance`'s constructor and died at the end of it; the library stayed mapped only
because GLFW's own `dlopen` still held a reference. Removing that `dlopen` would have turned the local
into a use-after-unmap.
`vk::raii` handles also inherit it through their parents, so `createSwapchainKHR` on the raii device
and `pipelineBarrier2` on a raii command buffer both resolve through the same table.

Two consequences worth knowing:

- **A bare `vk*` call does not compile.** `VK_NO_PROTOTYPES` in `common.hpp` keeps vulkan_core.h from
  declaring the entry points, so the mistake is caught in the editor rather than at runtime. This is a
  deliberate improvement over the volk arrangement, which is worth understanding because the failure
  modes differ sharply. Measured on 2026-09-21 by adding a bare `vkDestroyInstance(VK_NULL_HANDLE,
  nullptr)` and building:
  - **with volk:** compiles, links, then **null-pointer call at runtime** — volk declared the `vk*`
    names as global function-pointer variables and nothing called `volkLoadDevice`, so they were all
    null. This is the trap the "go through the raii object" rule was written against, and removing
    volk is what removes it.
  - **with `#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1` instead of `VK_NO_PROTOTYPES`:** compiles,
    then fails to link — `undefined reference to 'vkDestroyInstance'`.
  - **with `VK_NO_PROTOTYPES` (what this project uses):** does not compile, and the message even points
    at the right thing — *did you mean
    `vk::raii::detail::InstanceDispatcher::vkDestroyInstance`?*
- **A non-raii `vk::CommandBuffer` or `vk::Queue` cannot record or submit.** Those classes default their
  dispatcher argument to `VULKAN_HPP_DEFAULT_DISPATCHER`, which this project never defines, so a plain
  `vk::` handle would be a link error at best. This is why `Renderer::record` takes a
  `vk::raii::CommandBuffer` and why hpp's non-raii handles are not an option here.

`VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL` is left at its **default of 1**, which is what gives
`vk::raii::Context` its no-argument constructor — the one that dlopens. Setting it to 0 would leave only
the constructor taking a `PFN_vkGetInstanceProcAddr`, which is the hook volk used to fill; that is the
whole reason volk was a dependency, and with volk gone there is nothing left to fill it. Do not
introduce `VULKAN_HPP_DEFAULT_DISPATCHER` either — that is a second function-pointer table for a project
that has one loader.

`VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS` is defined to make `VK_ERROR_OUT_OF_DATE_KHR` a return
value rather than an exception, for the same reason `VK_CHECK` is not used on acquire or present: the
frame loop handles it as control flow. `VK_ERROR_SURFACE_LOST_KHR` is *not* covered by it and still
throws — see the exception section below.

**The include order is no longer load-bearing.** `common.hpp` used to include `<volk.h>` *before*
`<vulkan/vulkan_raii.hpp>` because `volk.h` `#error`s if `vulkan.h` came first. That constraint left with
volk: `window.hpp` and `window.cpp` include neither `common.hpp` nor any Vulkan header at all, which is the
point of them. Note also that `vulkan.hpp` does not pull in the `vk::raii` namespace — that is a
separate header.

### Exceptions

Two kinds of exception reach `main()`, and it catches both, so none escapes into `std::terminate`
(which prints "terminate called…", may skip every destructor, and dumps core):

- **`lc1::Error`**, thrown by `fail()`: our own diagnostics, such as the missing-layer message or
  `read_file`'s "failed to open <absolute path>".
- **`std::exception`**, the last-resort catch. It covers `vk::SystemError`, which `vk::raii` throws
  on any failed call. Its `what()` already names the call and the `VkResult`, so modules do **not**
  need to wrap Vulkan calls to translate it. Some older constructors (`Instance`, `Device`,
  `Swapchain`, `FrameLoop`, `Renderer`) still catch and re-`fail()` with a prefix. That is optional
  context, not a rule to copy into new code.

**`draw_frame`'s `vk::SurfaceLostKHRError` catch is control flow, not error handling — keep it,
and keep it first.** `FrameResult::SurfaceLost` is real control flow for the WSI state machine,
but SURFACE_LOST is not in hpp's success-code list for acquire or present, so it arrives as a
thrown exception rather than a returned result. It is caught by its own type *before* the
`vk::SystemError` catch, which would otherwise swallow it: `SurfaceLostKHRError` derives from
`SystemError`, so the reverse order silently turns "the surface is gone, report it" into "the frame
failed".

### Construction and teardown both belong to the type

`Window`, `Instance`, `Device`, `Swapchain`, and `FrameLoop` have neither an `init()` nor a
`destroy()`.
Construction happens in the constructor, and a constructor that fails throws, so **an object that
exists is one that works** — there is no valid-but-uninitialized state for a later call to trip over.
Destruction is the destructor, running at the closing brace of the block in `main()` that declares them,
in reverse declaration order. Member declaration order inside those classes is therefore load-bearing:
`Device` declares its allocator after its logical device so the allocator dies first. `Surface`
is declared before `Device` in `main()` and outlives the swapchain. `Instance` declares
`messenger_` after `handle_` so the *messenger* dies first, and `Swapchain` declares `images_` after
`handle_` so the image views and semaphores — which reference the swapchain's images — die before the
swapchain itself.

**`Window` is the one place where member declaration order is doing cleanup rather than destruction
order**, and it is worth reading as the same technique pointed at a different problem. Its private
`glfw_lifetime_` member exists only for its destructor, which calls `glfwTerminate()`. It is declared
first so it is destroyed last — after `~Window`'s own body has run `glfwDestroyWindow` — but the reason
it is a *member* and not a statement in `~Window` is the failure path: a constructor that throws does
not run the destructor, while members already constructed are destroyed during unwinding. So a failed
`glfwCreateWindow` still terminates GLFW. It works unconditionally because `glfwTerminate()` returns
immediately when GLFW is not initialized, which is exactly the state after a failed `glfwInit` — so the
guard needs no "is it armed" flag.

`FrameLoop` holds a non-owning, non-null `Device const*` initialized from its constructor reference;
`Swapchain` holds `Device const&`. The raii handles they own call back through that device to be destroyed, and a destroyed `Device` has already cleared its dispatcher — so outliving the
device is a null call, not a stale value. `main()`'s declaration order is what guarantees it.

`Swapchain::recreate` has to repeat that last ordering **by hand** (`images_.clear()` then
`handle_.clear()`), because a rebuild is not a destruction and the member declaration order only helps
on the way out of scope. It also has to rebuild in the order the C version did: query the surface
capabilities and bail out on a zero extent *before* destroying anything, so a minimized window leaves
the existing swapchain intact rather than leaving the object holding nothing.

**`SwapchainImage::image` is deliberately raw, and that is an invariant, not an oversight.** The image
belongs to the swapchain, which frees it; a `vk::raii::Image` would free it a second time. hpp has no
way to spell "borrowed", so the type stays `vk::Image` and the comment in the struct is the invariant.

**`Swapchain` holds `Device const&`.** The raii handles it owns call back through that device to be
destroyed, and a destroyed `Device` has already cleared its dispatcher — so a `Swapchain` that outlives
its `Device` is a null call, not a stale value. `main()`'s declaration order is what guarantees it, and
it is the same contract the raw handle copies used to carry, only now enforced sharply.

**`Surface` owns presentation integration; `Device` only borrows it during construction.**
`Surface` creates the Vulkan surface through GLFW and immediately adopts it into `vk::raii`.
`Instance` and `Window` must outlive `Surface`. `Swapchain` holds a non-owning `Surface const*`
for creation and recreation; Surface must outlive the swapchain. `Device` stores neither Surface
nor Window and includes no GLFW header. Its constructor only needs the surface to select a queue
family that supports presentation. Framebuffer size is queried by `main()` and passed to Swapchain
as an explicit extent; Swapchain does not depend on Window.

This is not cosmetic. `vk::raii` operation methods dereference their dispatcher unguarded —
`Device::waitIdle()` calls `getDispatcher()`, which does `m_dispatcher->getVkHeaderVersion()` on a
`unique_ptr` that a `{nullptr}`-constructed object leaves null. So on a default-constructed raii handle
those methods are a **null-pointer call** — the same class of failure as an unloaded function-pointer
table. Only `clear()` and the destructor check for null. Not having that state at all is cheaper than
guarding against it.

`Device::wait_idle()` stays explicit because `~vk::raii::Device` destroys the device **without** waiting
for it. `main()` waits on both normal and exceptional exits from the drawing loop, while the frame
resources, materials and meshes are still alive. RAII destruction alone does not make pending GPU
access safe. A validation error that `abort()`s never unwinds at all.

**Frame scheduling owns per-slot drawing resources through a template parameter.**
`FrameLoop<FrameResources>` constructs one resource set per slot using a factory supplied by `main()`.
The factory calls `Renderer::make_frame_resources(object_capacity)` and is not retained. After waiting
for its slot's fence, the loop passes that slot's resources by mutable reference to the synchronous
recording callback. It does not interpret the resource contents or depend on `Renderer`.
The template definitions live in `frame-loop-impl.hpp`, included by `frame_loop.hpp`.

`Renderer::record` accepts one `FrameResources &`, the camera, and a span of `DrawItem`s, with no
frame count or slot index. Each resource set owns a camera UBO/set, a descriptor pool, a fixed-capacity
array of model UBOs/sets, and depth/MSAA attachments. Draw i uses resource i for that frame only;
there is no persistent object-to-slot mapping. Capacity is passed by the application (currently the
continent view's chunk draw count),
and exceeding it fails explicitly. `Buffer::upload` copies and performs the necessary VMA flush;
the caller's fence wait makes overwriting safe. Descriptors are written at resource creation.

`scene::Object` holds CPU transform data and non-owning mesh/material pointers; `draw_item()` extracts
its model matrix. `Transform` defaults to zero translation/rotation and unit scale. The prototype draws
the chunks produced by `ContinentView`, sharing one material. Frame capacity follows the generated
draw count. `Vertex::color` defaults to white for existing textured meshes; procedural world geometry
uses per-vertex colors and a white texture. The renderer owns the pipeline,
material pool and sampler. `RenderTarget` remains the shared non-owning output contract.
`main()` creates the renderer before the frame loop and waits for the device on normal and exceptional
loop exits before destroying any resources used by submitted draws.

`scene::Character` owns its `Object`, whose transform is the sole world pose. Its origin is at
the feet; ground characters remain upright and unscaled so collision dimensions agree with the
world pose. Body yaw is right-handed about +Y (zero faces -Z); look yaw/pitch are relative to the
body. `move` accepts horizontal world-space displacement in metres and resolves collision;
`walk` converts a direction and bounded frame time into displacement. Neither changes body facing.
The character borrows its mesh/material and owns no camera, window or input router. Player
controllers borrow the continent and accept resolved `PlayerInput`; the application gates input
using control mode, UI context and focus, independently of cursor capture.

Import correction belongs to `Model`. Both OBJ loading and procedural construction accept a
`scene::Transform` correction, baked once into CPU vertices before `gen_mesh` uploads them.
Normals use the inverse transpose and baked reflections reverse triangle winding. Instances share
the resulting GPU mesh; `Character` and `Object` never reapply the correction. This is the static
model path, not skeletal animation. Future skeleton/animation import must use consistent coordinate
conversion for joints and inverse bind matrices as well.

**Resource ownership is scene-scoped and exclusive.** `ResourceManager::load<T>` stores resources
in type-indexed, relative-ID-keyed maps of `std::unique_ptr<Resource>`. T must derive from Resource
and provide `load_from_file(Device const&, path const&) -> T`; the current storage construction also
requires T to be move-constructible. The pure virtual Resource destructor has an inline definition.
Each type bucket contains only T, which permits casting cached Resource references back to T.
Repeated loads return the cached resource; failed loads do not leave a null resource entry.
Mesh loading still uses a separate typed cache during this migration.
The application currently loads CPU Image data and constructs GpuTexture directly;
GpuTexture has no file-loading interface or Resource base class. Materials are currently created
and owned by the application through Renderer; ResourceManager no longer depends on Renderer.

Resource IDs are normalized paths relative to the constructor-supplied asset root. Absolute IDs and
paths escaping lexically above the root are rejected; symlink aliases are not deduplicated. CMake
copies assets into each build tree. The application chooses the root; objects and Renderer do not
resolve resource paths. Returned pointers remain stable as the caches grow. There is no individual
eviction or reference counting: stop using borrowed pointers and wait for GPU completion before
manager destruction. Device must outlive the manager. Material owners must keep their textures and
Renderer alive until those materials are destroyed. Cache access is render-thread-only.

Layout: **library** `lc1_engine` = public headers in `include/lc1/` + sources and private headers
(e.g. `vk/checks.hpp`) in `src/`; **app** `lc1` = `app/main.cpp`, linking `lc1_engine`. Include
public headers as `"lc1/..."` everywhere; `src/` is only on the library's private include path, so
the app cannot reach private headers.

| module | owns |
|---|---|
| `resource-manager.*` | scene-owned typed resource caches and legacy mesh cache; relative paths identify resources |
| `window.*` | GLFW init/terminate, the `GLFWwindow`, required surface extensions, the resize flag, every input callback and the `InputState` they fill, cursor capture |
| `input.hpp`, `input.cpp` | `Key`, `InputState`, `Action`, `InputContext`, `Binding`, `InputRouter`: device state, the data-driven bindings, and the active context's resolved actions |
| `vk/loader.*` | the one `dlopen` of the Vulkan loader (`vk::raii::Context`), and GLFW's copy of its `vkGetInstanceProcAddr` |
| `vk/instance.*` | `vk::raii::Instance`, validation + sync validation, debug messenger |
| `vk/surface.*` | the window presentation surface, created via GLFW and owned by `vk::raii` |
| `vk/device.*` | physical-device pick, logical device, the VMA allocator, the single graphics+present queue |
| `vk/swapchain.*` | swapchain, `SwapchainImage` vector, all recreation — raii except the borrowed images |
| `vk/frame_loop.hpp`, `vk/frame-loop-impl.hpp` | per-frame sync, templated per-slot resources, WSI state machine |
| `vk/renderer.*` | main material rendering and orchestration; normal record uses ray-query shadows, a separate entry point renders the optional legacy depth-map diagnostic |
| `vk/ray-query-shadows.*`, `vk/temporal-shadows.cpp` | ray-query visibility, TLAS updates, temporal history and spatial filtering |
| `vk/shadow-map-preview.*` | lazy directional depth-map preview resources and explicit ShadowMapRegion coverage; no normal lighting |
| `vk/render-data.hpp` | shared camera, light and object shader upload layouts |
| `vk/frame-resources.hpp` | camera and per-draw UBOs/sets, pool and attachments; owned by each frame slot |
| `vk/render-target.hpp` | the borrowed single-sampled output view and extent shared by recording and presentation |
| `vk/pipeline.*` | graphics pipeline, its pipeline layout and descriptor set layout |
| `vk/shader.*`, `vk/shader-stages.*` | shader modules, and the stage list a pipeline is built from |
| `vk/buffer.*` | a `VkBuffer` + its VMA allocation; vertex/index/uniform/staging differ only in flags |
| `vk/descriptor-set.*` | one set owning its Buffer objects, with UBO size checks, borrowed combined image samplers and layout binding validation; FrameResources owns the pool |
| `vk/gpu-image.*` | a `VkImage` + its VMA allocation + a whole-image view; does not fill itself |
| `vk/gpu-texture.*` | uploads a CPU `Image` into a `GpuImage` through a staging buffer, left `SHADER_READ_ONLY`; file loading stays with the caller |
| `vk/sampler.*` | a `vk::raii::Sampler`; one serves many textures |
| `vk/mesh.*`, `vk/vertex.*` | vertex + index buffers uploaded through staging; the vertex layout; `DrawItem` |
| `vk/one-time-submit.hpp` | record, submit and wait for one-off setup work (uploads) |
| `vk/common.*` | hpp configuration macros, `VK_CHECK`, `transition_image_layout`, `read_file`, string helpers |
| `scene/camera.hpp` | `scene::FpsCamera`: yaw/pitch view with perspective projection |
| `model.hpp` | CPU mesh data and model-level import correction, baked before upload |
| `scene/transform.hpp`, `scene/object.hpp` | transforms and drawable instances borrowing mesh/material resources |
| `scene/character.*` | character world pose, relative look, body configuration, eye position and ground collision movement |
| `scene/lights/*.hpp` | `scene` light types and the unified GPU upload record |
| `game/continent.*` | region IDs on terrain tiles, location footprints, road graph and spatial queries, map validation and swept ground movement |
| `game/continent-prototype.cpp` | sample five-region, six-location world and connecting roads, with shared render/collision blocks |
| `game/player-controller.*` | first-person and oblique-view player control, consuming resolved input and borrowing the world/character |
| `game/map-camera-controller.*` | oblique camera center, follow lock, zoom, temporary recenter and bounded edge pan |
| `game/continent-view.*` | static 64-metre chunk meshes and their shared material; all chunks currently resident |
| `game-clock.*` | `Stopwatch`: frame delta, total time, fps |

**Window and input keep platform integration out of the rendering interfaces.**
`window.*` includes no Vulkan header, and obtains `lc1::Error` from `include/lc1/error.hpp`.
`surface.cpp` is the Vulkan/GLFW bridge: it includes both APIs to call `glfwCreateWindowSurface`.
Neither Device nor Swapchain depends on Window or includes GLFW. Surface's public header forward
declares Window and exposes only its raii Vulkan surface accessor.

Fixed-size framebuffer sizes cross that boundary as `lc1::FrameExtent` rather than `vk::Extent2D`, for
the same reason: a `vk::Extent2D` in `window.hpp` would make it a Vulkan module in all but name and drag
the load-bearing include order in with it. The cost is one parallel 8-byte type and one conversion,
which `compute_extent` in `swapchain.cpp` performs.

### Input: device state, bindings, one active context

Three layers, and the split is what keeps rebinding cheap — game code asks `held(Action::MoveForward)`
and never "is W down":

- **`InputState`** — which switches are down, which went down or came up this frame, cursor
  position/delta/scroll. No meaning, no game rules. Window owns it because Window owns the callbacks.
- **`Binding`** — a row of data: in this context, this `Key` means this `Action`. The table lives in
  `app/main.cpp`; nothing else maps a key to a meaning, so a rebind is a row edit.
- **`InputRouter`** — resolves the active context's rows into `held`/`pressed`/`released` per action.

Five things are load-bearing:

- **`Window::poll_events()` is the input frame boundary.** It calls `InputState::begin_frame()` and
  *then* pumps GLFW, so an edge means "happened during this frame's polling" and can be neither
  consumed twice nor land in the wrong frame. The application does not call `begin_frame` itself on
  purpose: a boundary the caller has to remember is one that ends up in the wrong place. `wait_events()`
  is deliberately *not* a boundary — it runs only while there is nothing to draw, so whatever it pumps
  is discarded by the next `poll_events` instead of leaking into a later frame.
- **`GLFW_REPEAT` is dropped in `key_callback`.** Passing it on would make a held key look like a fresh
  press every few frames — right for a text field, wrong for every game action, where a held W would
  stutter. A text field wants the repeat rate and a context of its own, not a change in the callback.
- **Losing focus releases everything** (`glfwSetWindowFocusCallback` → `InputState::release_all()`),
  reporting each held key as released so a drag cannot stay half-open across an Alt-Tab. An unfocused
  window receives no key events, so "still held" is unknowable and "none" is the only safe answer.
  Cursor discontinuities — capture toggled, focus regained, pointer re-entered — reset the delta
  baseline for the same class of reason: the pointer travelled while nothing was tracking it, and that
  distance is not camera motion.
- **Exactly one context is active**, the top of `InputRouter`'s stack. Switching modes has to be able to
  turn a whole set of controls off, and "off" is easier to reason about than priority between
  overlapping layers. Action state is **recomputed from scratch** every frame rather than updated in
  place, which is what makes a switch safe — a key still held from the old mode has no binding in the new
  one and simply resolves to false — and what makes `update()` idempotent within a frame.
  `InputContext::Ui` binds nothing on purpose: pushing it *is* the gate for "a text field or ImGui owns
  the keyboard now", and it has to exist from the start, because retrofitting it means auditing every
  `held()`/`pressed()` call site.
- **`released` is masked by `!held`**, so it means "the action ended", not "some key bound to it came
  up". Without the mask, letting go of one of two keys bound to one action tells a consumer its drag
  finished while it is still running. `pressed` is deliberately *not* masked the other way: a second
  key's press while the action is already held is harmless, and suppressing it would require comparing
  against the previous frame — state that would make `update()` depend on how many times it was called.

**`Key` covers mouse buttons as well as keys** — `Key::MouseLeft`, not a parallel `MouseButton` enum. An
action does not care which device triggered it, and one vocabulary keeps `Binding` a flat row instead of
a tagged union. Unreal's `FKey` makes the same call.

**`input.hpp` includes neither a Vulkan header nor a GLFW one**, and `src/window.cpp` is the only place
that knows `GLFW_KEY_*` exists: it holds the `Key` ↔ GLFW table under two `static_assert`s that turn
"forgot one" and "listed one twice" into compile errors, so the table is provably a bijection. This is
the `FrameExtent` argument again — a public header stays free of a platform API it does not need.

**`InputState::cursor()` is in screen coordinates, not pixels** — GLFW's logical points. On a display
with a scale factor the two differ, and nothing in `input.hpp` converts, because the conversion needs the
framebuffer size: multiply by `glfwGetWindowContentScale` at the point where a cursor must become a pixel
or a world ray. Mouse *motion* (camera look) needs no conversion and is why the demo works without it;
picking will, and that is the one place to add it.

**`Window` interprets no key at all.** ESC-to-close moved to `main()` when input arrived: a hardcoded
ESC-to-close makes ESC unusable as the pause/back key every strategy game needs, and "this key closes
the window" is application policy, not a property of a window.

`Instance`, `Surface`, `Device`, `Swapchain`, `FrameLoop` and `Renderer` are on `vk::raii`; the C API is gone
from the project. The raii classes still hand out raw
handles where a C-only caller needs one, and there is exactly one such caller left: `Instance::handle()`
for `glfwCreateWindowSurface` in `surface.cpp`, which takes a `VkInstance` and nothing else — paired with
`Window::handle()` for the `GLFWwindow *` half of the same call. The remaining accessors are
`Device::queue_family()` (a `uint32_t`, not a handle), `Swapchain::extent()` and `Swapchain::images()`
(both consumed by renderer, which borrows rather than owns). The non-owning raii accessors are
`Instance::raii()`, `Surface::raii()`, and `Device::raii()` / `raii_physical()` / `raii_queue()` — those
exist because a `vk::raii::SwapchainKHR` is created *through* the raii device and the surface queries go
through the raii physical device, which is also why `Swapchain` and `FrameLoop` take the whole `Device`
rather than a handful of handles.

`*` on a raii handle yields the plain `vk::` handle inside it, and it is not optional style in two
places:
- where a raw handle is needed from a raii one, because raii handle → C handle would be two
  user-defined conversions, and those do not chain (`*target.view`, `*frame.image_available`);
- where hpp takes an array of handles (`ArrayProxy`, e.g. `setSetLayouts(*descriptor_set_layout_)`).
  Passing the raii object itself compiles the conversion check but then fails inside hpp: the proxy
  needs the *address* of a `vk::DescriptorSetLayout`, and a raii object's address is not one.

**`SwapchainImage` owns the present semaphore**, not the frame loop. `vkGetSwapchainImagesKHR` may
return more images than requested, and the count changes across recreation; a separate semaphore array
sized from `minImageCount` makes `renderFinished[imageIndex]` an out-of-bounds write. Tying the
semaphore to the image makes "array sized wrong" and "recreation forgot a resource" the same bug. It is
a `vk::raii::Semaphore` member now, which is what turns "recreation forgot a resource" from a bug that
has to be caught into one that cannot be written — `images_.clear()` frees its own semaphores.

## Validation discipline

`ctest --test-dir build/linux-x86_64/Debug --output-on-failure` runs CPU tests for continent data,
continuous collision, wall sliding, body clearance, boundaries, movement speed, city traversal,
character pose/controller behavior and static model correction (including reflected winding).
They create no window or Vulkan device. Runtime rendering still needs validation-layer smoke testing.

Validation **and synchronization validation** run by default in debug builds. This is the safety net for
the whole renderer; a clean run is a real assertion, not a formality. It has already paid for itself —
it caught both of the bugs below, which no amount of code review found.

- The debug callback prints `pMessageIdName` (that string is the VUID) and **aborts on ERROR** — you
  should see the first error, not hundreds of cascading ones.
- `LC1_NO_VALIDATION=1` disables the layer, for measuring without it.
- Never enable `GPU_ASSISTED` together with `SYNCHRONIZATION_VALIDATION`; they are incompatible.
- stdout is line-buffered deliberately: the callback aborts, and `abort()` does not flush
  block-buffered streams, which would swallow the startup logs exactly when they matter.

### Four invariants that are silent when broken — do not "tidy" these

The names below are the spec's and the VUIDs'. In the hpp code they are the same words without the `vk`
prefix and reached as methods — `raii_device.resetFences(...)`, `command_buffers_[frame].reset({})`.
Match them up by the argument shapes, not by the spelling.

1. **`vkResetFences` goes only on the path that reaches `vkQueueSubmit`.** Resetting earlier leaves the
   fence unsignaled with nothing submitted; the next wait on that slot blocks forever, no message.
2. **`VK_SUBOPTIMAL_KHR` from acquire means an image WAS acquired and the semaphore IS signaled** —
   render and present it, then recreate next frame. Treating it like `OUT_OF_DATE` abandons a signaled
   semaphore (`VUID-vkAcquireNextImageKHR-semaphore-01286`).
   Because of that, the result has to be checked **before** the image index is read. hpp returns both in
   a `ResultValue`, and on any non-success result the driver left the out-parameter unwritten, so
   `acquired.value` is an uninitialized read — check `acquired.result` first, always.
3. **Reset the frame's command *buffer*, never the whole pool.** `vkResetCommandPool` resets the other
   in-flight frame's buffer too, which is still pending:
   `VUID-vkResetCommandPool-commandPool-00040 ... is in use`. Waiting on `fences_[frame]` only proves
   *this* slot is free.
4. **The acquire→render barrier's `srcStageMask` must be `COLOR_ATTACHMENT_OUTPUT`**, matching the
   stage the submit waits on the acquire semaphore at. `VK_PIPELINE_STAGE_2_NONE` is the tempting sync2
   idiom and is legal against that barrier's own VUIDs — but it leaves the layout transition unordered
   against the acquire, and sync validation reports `SYNC-HAZARD-WRITE-AFTER-READ`. This is why "it
   passes validation" is not the same as "it is correct"; only sync validation catches the difference.

## Device selection

Selection checks every extension passed to the Device constructor, not just `VK_KHR_swapchain`.
`required_features()` builds the feature chain used both for selection and device creation:
`sampleRateShading`, `samplerAnisotropy`, `shaderDrawParameters`, `synchronization2`,
`dynamicRendering`, `extendedDynamicState`, `bufferDeviceAddress`, `accelerationStructure`,
and `rayQuery`. The device also requires `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
and `VK_KHR_deferred_host_operations`, and the graphics/present family must support compute
for acceleration structure builds. VMA enables its buffer-device-address allocator flag. When adding an enabled feature, also add its
support comparison in `supports_required_features`; unsupported candidates must be rejected before
ranking, so a usable lower-ranked GPU can still be selected. Surface presentation support is checked
against the caller's Surface, which Device does not own or retain.

`VK_PHYSICAL_DEVICE_TYPE_CPU` devices are **excluded**, not deprioritized. llvmpipe is enumerated as a
real device on the dev machine and nothing reorders devices (`VK_LAYER_NV_optimus` is dormant without
`__NV_PRIME_RENDER_OFFLOAD=1`). Startup logs device name/type/driver; `LC1_DEVICE=<substring>` overrides
the pick on multi-GPU machines.

## Known environment quirks

- **Fedora's `shader-slang` package ships no downstream-compiler shims, and only a *Release* build
  notices.** Debug passes `-O0`, which skips the optimizer; Release runs the full pipeline and dies in
  the shader step with
  `error[E00100]: failed to load downstream compiler 'spirv-opt'` and
  `note[E99996]: failed to load dynamic library 'slang-glslang-2026.18'` — naming a library nothing in
  this repo mentions. `/usr/bin/slangc` is installed, `libslang-compiler.so` is installed,
  `libslang-glslang-<version>.so` is not (the copr package's 2026.18.3 has the same gap, and the
  `slang-2.3.3` package is an unrelated older library). Any complete Slang install works — the
  official release tarball, or the one the neovim mason package puts in
  `~/.local/share/nvim/mason/packages/slang/bin/slangc` beside its `lib/`. Point the build at it with
  `make SLANGC=<path-to-slangc>`, which forwards to `-DSLANGC_EXECUTABLE`; that is a CMake cache
  variable, so it sticks to the build tree it was configured in and a fresh tree needs it again.
  This is not MinGW-specific: a *Linux* Release build fails the same way.
- **`XLOCALEDIR=/usr/share/X11/locale` is set in the profile's `[runenv]`.** The conan-built
  libxkbcommon has no compiled-in default, and the compose data lives in the system, not the package.
  Without it every run prints an `xkbcommon: couldn't find a Compose file` error plus a GLFW error
  (printed by the error callback in `src/window.cpp`, prefixed `[glfw] error`).
  Non-fatal, but compose/dead-key text entry silently does not work.
- `driverVersion` is a member of `VkPhysicalDeviceProperties`, not of `VkPhysicalDeviceDriverProperties`
  (which carries only `driverID`/`driverName`/`driverInfo`). Its encoding is vendor-defined — print as hex.
- **Hyprland (the dev machine's compositor) tiles new windows off-screen** when the workspace is full,
  reporting them as `visible: True` while they sit past the monitor's right edge. A screenshot that
  seems to be missing the window is probably this, not a rendering bug. Hyprland also has no minimize
  concept, so the zero-extent path in `Swapchain::recreate` is unreachable there — it is defensive
  code for compositors that do report `0x0`.
- Expect stderr noise from the loader about `/usr/libvulkan_dzn.so` returning -9 on some runs. That is
  the D3D-on-Vulkan translation layer failing to load. **Not our bug.**


## Ray-query shadows

Lighting uses fragment ray queries against mesh-owned BLASes and one TLAS per frame slot.
Draws default to opaque, two-sided shadow casters, including off-camera geometry; printed labels
opt out through `DrawItem::casts_shadow` while remaining visible. Both shadow paths honor this flag.
Instance transforms use Vulkan's row-major 3x4 layout; GLM matrices must be transposed when copied.
Reuse each slot's TLAS when its instances are unchanged; rebuild after the slot fence otherwise.
Build scratch and instance inputs also belong to that slot. The application calls only ordinary
`record`, accepting camera, lights and draws with no preview flag or shadow center.
The separate `Renderer::record_shadow_map_preview` diagnostic remains available to explicit callers
such as GPU tests, which supply `ShadowMapRegion`; the application has no preview toggle.
The legacy pass and per-slot map/descriptors are created lazily. It binds a separate set-0 layout
with only bindings 2/3; the normal layout retains bindings 0/1/4-10. Bind sets explicitly across
these incompatible pipeline layouts. Previewing runs no TLAS update, ray queries, temporal resolve
or main-pass depth/MSAA allocation. The device/mesh ray-query requirements still apply.
Normal rendering after a preview invalidates history before accumulating again.

MSAA is capped at 4x with full sample shading for material/BRDF evaluation. A separate
single-sampled raster pass traces shadows and stores 8-bit visibility for each of the ten lights
in an RGBA32Uint image, alongside world position/normal in RGBA32Sfloat. A fullscreen temporal
resolve reprojects history, rejects disocclusion/moving receivers and clamps against the current
receiver neighborhood. A 5x5 spatial bilateral pass filters only nonzero-radius sphere lights,
using surface identity, normals, tangent-plane distance and history-dependent visibility weights.
It reuses the raw visibility image for output; never feed spatial blur back into temporal history.
The MSAA pass fetches the spatial output by pixel coordinate, so ray-query
work is not multiplied by the sample count. The ray and lit entries use early depth testing.
Local lights use unbounded inverse-square attenuation; do not add a range/cutoff parameter.
SphereLight uses a receiver-facing disk approximation with pixel/light/frame-seeded random samples,
defaulting to one shadow ray per frame and up to 32 history frames; radius zero takes the point-light
path. History belongs to the Renderer-owned `RayQueryShadows` and is shared across consecutive
submissions: resizing it must wait for all GPU work, not just the current slot fence. Moving receivers reject history; this does
not yet implement object motion vectors. The GPU-only lc1_temporal_shadow_tests target checks
the actual temporal shader and is excluded from default CPU CTest. See `docs/ray-query-shadows.md`
for the approximation, edge behavior and performance limitations.

`lc1_shadow_path_tests` is another explicit GPU target (excluded from default CPU CTest). It checks
fresh preview/normal slots, resource isolation, switching in both directions, resize, mirrored draws,
and actual offscreen pixel readback with validation and synchronization validation enabled.

## PBR direct lighting and HDR

Normal rendering now uses metallic-roughness GGX/Smith/Schlick direct lighting. There is no fixed
ambient term or IBL yet. `scene::PbrParameters` contains linear factors; CPU `PbrMaterial` references
images, while `MaterialInfo` supplies already-uploaded GPU textures to `Renderer::make_material`.
GPU `Material` owns an immutable 64-byte UBO and set 2 through `DescriptorSet`. Set 2 bindings are
0 base color, 1 factors UBO, 2 metallic-roughness, 3 normal, 4 occlusion, 5 emissive. All texture
descriptors are initialized; missing base/MR/emissive maps use white. Normal maps are linear and use UV0 MikkTSpace tangents; only AO remains deferred and rejects
nonempty textures. Color/emissive are sRGB; MR is linear (G roughness, B metal).
The old texture-only factory explicitly produces a nonmetal with roughness 0.8. At most 64 materials
may be live; Renderer is immovable and must outlive materials, including their capacity counters.

Each frame slot owns an RGBA16F HDR resolve image, optional HDR MSAA color, and a lazily allocated
tone-map set. Sample count is capped at 4x and checked against the actual HDR/depth formats.
Resolve happens in linear HDR before exposure/Reinhard tone mapping. sRGB attachments encode
automatically; UNORM display output explicitly requests `OutputEncoding::Srgb`. The default writes
linear values. The depth-preview diagnostic keeps an independent output path. See `docs/lighting.md` and `docs/pbr.md`.

`app/main.cpp` adds a walkable PBR exhibit area on both sides of the spawn road. Its shared meshes,
procedural textures, materials, labels, lights and animated transforms live in `app/pbr-showcase.*`.
`Continent::make_prototype` optionally accepts spawn-relative blocks, used for both rendering and
static collision; keep the central road open. Run with `make run`; `make run-pbr` is a compatibility
alias for that same entry point. The separate demo executable has been removed at the user's request.
Existing camera controls remain: wheel zoom, M views, Y unlock, Space recenter. Gameplay bindings add
F4 cycling light types (including emission-only), F5 motion pause and PageUp/PageDown exposure.
`lc1_pbr_tests` remains an explicit GPU target outside CPU CTest. It covers numerical BRDF, textures,
HDR, output encoding, MSAA, resize, pool lifetime and the main scene's exhibit collision and rendering
under validation and synchronization validation, saving `artifacts/pbr-showcase.ppm` and
`artifacts/pbr-emission.ppm` relative to the build-tree working directory.

### Tangent-space normal mapping

`Vertex::tangent` stores object-space xyz and +/-1 bitangent handedness; zero means absent.
Conan's `mikktspace/cci.20200325` generates tangents through `generate_tangents`, reindexing per-corner
results to preserve mirrored UV seams. Invalid geometry/UVs fail without changing the input.
`Model::generate_tangents()` processes its current CPU geometry before GPU upload. For maps baked
against an existing basis, generate/provide tangents before nonuniform import correction; that
correction transforms tangents with its linear matrix, orthogonalizes them and flips w on reflection.

`MaterialData.flags.x` bit 0 enables normal mapping; absent maps and scale zero skip sampling.
Mapped materials require a mesh with valid tangents. Vertex shading transforms T with the model
matrix and N with its inverse transpose; fragment shading orthogonalizes T and reconstructs B
using tangent.w and the model determinant's sign. Normal textures use +Y bitangents. Ray-query bias,
history and filtering keep the unperturbed surface normal; direct light also respects that surface's
front hemisphere. No tangent generation or extra shadow rays run per frame.

`lc1_tangent_tests` belongs to CPU CTest. PBR GPU tests cover missing/flat maps, scale, mirrored UVs,
nonuniform/reflected instances and a world-normal reference. The main exhibit includes three matching
NORMAL OFF/x1/x2 panels and mapped transformed objects, using the same existing lights.

The MinGW KTX 4.4.2 Conan package installs `libastcenc-avx2-static.a` separately but omits it from
KTX::ktx's interface; CMake links that companion archive from the same Conan package on MINGW.
TinyGLTF filename APIs take UTF-8 strings, so convert filesystem paths explicitly on Windows too.

Normal-map diagnostics use a dedicated wave pattern. Regular transformed exhibits use subtle fine
normal detail, not the strong diagnostic map. Printed label draws set `casts_shadow = false` to avoid
duplicate glyph shadows. The flag is also available on `scene::Object`; caster membership changes
invalidate temporal history, and the diagnostic depth pass skips non-casters just like the TLAS builder.
