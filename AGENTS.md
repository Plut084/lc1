# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

A **Vulkan 1.4 renderer skeleton** exists (`include/`, `src/`, `app/`): window, swapchain, resize
handling, clean shutdown, an indexed mesh drawn through a graphics pipeline with a per-frame uniform
buffer (model/view/proj) bound through descriptor sets, and textures loaded from files — all with
validation + synchronization validation on and **zero output**. It is being built chapter by chapter
along the official tutorial (https://docs.vulkan.org/tutorial/latest/). The game itself — 养成 / 战斗 /
地图 — is not started.

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
- **地图 (map)** — a two-tier map: a world map with RPG-style character movement, plus a top-down
  minimap with Red Alert–style (类红警) RTS controls.

Note that 城池建设 (city building) deliberately appears in both 养成 and 战斗 — it is the same system
serving an out-of-game economic role and an in-game defensive/deployment role. Similarly, the
per-soldier equipment model ties the army system to the equipment system. When implementing, treat
these as shared systems with two modes rather than duplicated features.

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

```bash
conan install . -pr:h profiles/linux -pr:b default --build=missing -s build_type=Debug
cmake --preset conan-debug
cmake --build --preset conan-debug
make run-debug    # or all of the above in one step
```

Three parts of that are easy to get wrong:

- **`-pr:h profiles/linux` is required.** The default profile is missing options the graph needs, and
  omitting it fails with a confusing `xorg/system` error about missing X11 `-devel` packages.
- **`-s build_type=Debug` is required on every install.** The profile defaults to `Release`, and Conan
  only writes presets for build types it actually generated.
- **Run `make run-debug`, not `./build/Debug/lc1`.** It does two things the bare binary lacks:
  - It sources the conan run env. Debug builds take the validation layers from conan, and the loader
    finds them only via `VK_LAYER_PATH`, which that env exports. Without it, startup fails with a
    "layer not found" error that looks like the layer is missing when it is not.
  - It runs from `build/Debug/`. Assets such as `shaders/shader.spv` are opened by paths relative
    to the working directory, and the build puts them next to the binary.

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
  shows no `libvulkan`, and `LD_DEBUG=libs ./build/Debug/lc1` shows exactly one `find library=libvulkan`
  (two until 2026-09-24, when GLFW still dlopened `libvulkan.so.1` itself). A conan loader could only be
  a *second* one: `Context{}` dlopens by name regardless of what is linked, and `VK_NO_PROTOTYPES` means
  nothing would call into the linked copy. Holds on both platforms.
- **GLFW comes from conan, with `with_wayland=True` on Linux.** Its recipe defaults to
  `with_wayland=False`, an X11-only build.
- **Validation layers come from conan, Debug only.** They are a dev tool; Release does not need them,
  and building them from source is expensive. Version skew against the system loader is harmless.

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

Windows will need a sibling profile without the Wayland lines; `conanfile.py` should not need to change.

## Coding conventions

**The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) are the
standard here**, plus two project additions: `snake_case` variables, and no naming prefixes. The table
below is what those resolve to in `include/` and `src/`, which already follows it.

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
    negative viewport height (and `y` = height, or the viewport lands off-screen). A flip reverses
    every triangle's winding, which is why `Pipeline` uses `eCounterClockwise` as the front face.

## Architecture

Init order is load-bearing — device *selection* needs the surface, and each stage must be up before the
next is constructed:

```
VulkanLoader(dlopen) -> Window(glfwInit + GLFWwindow) -> Instance(loader) -> Device(window)
  -> Swapchain -> FrameLoop -> Renderer
```

Both GLFW steps are inside `Window`'s constructor, and the loader `dlopen` is inside `VulkanLoader`'s,
which is what makes the ordering enforceable rather than conventional: `Instance` takes the loader and
`Device` takes the window, so neither can be built without them, and there is no way to leave the scope
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
`Device` declares `surface_` before `handle_` so the *device* dies first, `Instance` declares
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

`FrameLoop` and `Swapchain` both hold `Device const&`. The raii handles they own call back through that
device to be destroyed, and a destroyed `Device` has already cleared its dispatcher — so outliving the
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

**`Swapchain` also holds `Window const&`, and that contract is a different shape** — the distinction is
worth keeping straight, because it is the one reference member here whose absence would *not* be a null
call. `~Swapchain` never touches the window; the reference is read only by `recreate()`, to ask how big
the framebuffer currently is. So the requirement is "Window must outlive every call to `recreate()`",
not "Window must outlive Swapchain". `main()` declaring `Window` first happens to satisfy both at once.
`Device` takes a `Window const&` too but stores nothing, because it needs the window only inside its
constructor, for `glfwCreateWindowSurface` — a construction-time contract, with no member and no
lifetime rule at all.

This is not cosmetic. `vk::raii` operation methods dereference their dispatcher unguarded —
`Device::waitIdle()` calls `getDispatcher()`, which does `m_dispatcher->getVkHeaderVersion()` on a
`unique_ptr` that a `{nullptr}`-constructed object leaves null. So on a default-constructed raii handle
those methods are a **null-pointer call** — the same class of failure as an unloaded function-pointer
table. Only `clear()` and the destructor check for null. Not having that state at all is cheaper than
guarding against it.

`Device::wait_idle()` stays explicit because `~vk::raii::Device` destroys the device **without** waiting
for it. A `vk::SystemError` escaping the scope unwinds it, which destroys `FrameLoop`, `Swapchain`,
`Device`, and `Instance` on the way out; `vkDestroyDevice` frees child objects implicitly, so validation
still reports no leak. A validation error that `abort()`s never unwinds at all.

Layout: **library** `lc1_engine` = public headers in `include/lc1/` + sources and private headers
(e.g. `vk/checks.hpp`) in `src/`; **app** `lc1` = `app/main.cpp`, linking `lc1_engine`. Include
public headers as `"lc1/..."` everywhere; `src/` is only on the library's private include path, so
the app cannot reach private headers.

| module | owns |
|---|---|
| `window.*` | GLFW init/terminate, the `GLFWwindow`, required surface extensions, the resize flag |
| `vk/loader.*` | the one `dlopen` of the Vulkan loader (`vk::raii::Context`), and GLFW's copy of its `vkGetInstanceProcAddr` |
| `vk/instance.*` | `vk::raii::Instance`, validation + sync validation, debug messenger |
| `vk/device.*` | surface, physical-device pick, logical device, the VMA allocator, the single graphics+present queue |
| `vk/swapchain.*` | swapchain, `SwapchainImage` vector, all recreation — raii except the borrowed images |
| `vk/frame_loop.*` | per-FRAME sync (fences, `image_available`, command buffers), WSI state machine |
| `vk/renderer.*` | the pipeline, its uniform buffers, descriptor pool/sets and sampler; records each frame |
| `vk/pipeline.*` | graphics pipeline, its pipeline layout and descriptor set layout |
| `vk/shader.*`, `vk/shader-stages.*` | shader modules, and the stage list a pipeline is built from |
| `vk/buffer.*` | a `VkBuffer` + its VMA allocation; vertex/index/uniform/staging differ only in flags |
| `vk/image.*` | a `VkImage` + its VMA allocation + a whole-image view; does not fill itself |
| `vk/texture.*` | an `Image` loaded from a file (stb) through a staging buffer, left `SHADER_READ_ONLY` |
| `vk/sampler.*` | a `vk::raii::Sampler`; one serves many textures |
| `vk/mesh.*`, `vk/vertex.*` | vertex + index buffers uploaded through staging; the vertex layout; `DrawItem` |
| `vk/one-time-submit.hpp` | record, submit and wait for one-off setup work (uploads) |
| `vk/common.*` | hpp configuration macros, `VK_CHECK`, `transition_image_layout`, `read_file`, string helpers |
| `camera.hpp` | `FpsCamera`: yaw/pitch camera producing view and projection matrices |
| `game-clock.*` | `Stopwatch`: frame delta, total time, fps |

**`window.*` is the only non-Vulkan module**, and it lives outside `vk/` for that
reason. It includes no Vulkan header — not even `common.hpp` — and `Device`/`Swapchain` take a
`Window const&` precisely so that they do not have to name `GLFWwindow` either. That is what makes
"the Vulkan layer does not know about GLFW" a fact about the include graph rather than a convention.
It gets `lc1::Error` from `include/lc1/error.hpp`, which exists as a separate header for exactly this case.

Fixed-size framebuffer sizes cross that boundary as `lc1::FrameExtent` rather than `vk::Extent2D`, for
the same reason: a `vk::Extent2D` in `window.hpp` would make it a Vulkan module in all but name and drag
the load-bearing include order in with it. The cost is one parallel 8-byte type and one conversion,
which `compute_extent` in `swapchain.cpp` performs.

The other five are on `vk::raii`; the C API is gone from the project. The raii classes still hand out raw
handles where a C-only caller needs one, and there is exactly one such caller left: `Instance::handle()`
for `glfwCreateWindowSurface`, which takes a `VkInstance` and nothing else — paired with
`Window::handle()` for the `GLFWwindow *` half of the same call. The remaining accessors are
`Device::queue_family()` (a `uint32_t`, not a handle), `Swapchain::extent()` and `Swapchain::images()`
(both consumed by renderer, which borrows rather than owns). The non-owning raii accessors are
`Instance::raii()`, and `Device::raii()` / `raii_physical()` / `raii_surface()` / `raii_queue()` — those
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

`VK_PHYSICAL_DEVICE_TYPE_CPU` devices are **excluded**, not deprioritized. llvmpipe is enumerated as a
real device on the dev machine and nothing reorders devices (`VK_LAYER_NV_optimus` is dormant without
`__NV_PRIME_RENDER_OFFLOAD=1`). Startup logs device name/type/driver; `LC1_DEVICE=<substring>` overrides
the pick on multi-GPU machines.

## Known environment quirks

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
