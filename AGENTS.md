# AGENTS.md

Repository guidance for coding agents. Keep this file focused on working rules and durable
constraints; put detailed design and investigation records in the relevant `docs/` document.
Read the linked documents when working on their subsystem.

## Engineering judgment

- The code architecture is not finalized. Adapt module boundaries, ownership and interfaces to
  requirements as needed; existing structure describes the current implementation, not an
  immutable design. Keep affected documentation consistent with architectural changes.
- Investigate implementation, configuration and the full diagnostic before concluding. Separate
  evidence from hypotheses; compiler behavior alone does not prove a C++ language rule. Check the
  applicable standard version and implementation support, using references or focused reproducers.
- Prefer suitable standard-library facilities; check preconditions, boundaries and return types.
  For example, positive integer mip counts use `std::bit_width`, not floating-point `log2`.
- Fix causes while preserving contracts. Do not remove `constexpr`, disable compiler jobs or weaken
  checks just to silence diagnostics; identify algorithm, language, library or configuration issues.
- Check defaults before adding overrides. Preserve justified local constraints, but do not copy
  local resource limits into CI. Install dependencies needed by the work actually performed.
- Validate the claimed scope: distinguish linting, configuration, compilation, CPU tests and GPU
  execution, and name unverified platforms. Local success does not establish remote CI success.
- Preserve unrelated user edits. Check diffs/history before attributing changes, and correct
  unsupported claims factually. Follow through on findings within scope; state audit boundaries
  and distinguish fixed issues from unresolved ones.

## Project and gameplay

- C++23, raw Vulkan 1.4 through vulkan-hpp/vk::raii, VMA-Hpp, GLFW and Slang.
  No third-party rendering abstraction or volk. Target desktop Linux first and Windows.
- Gameplay constraints and goals: [core systems](docs/content.md). Keep one traversable scale
  across countryside, cities and combat; city building is shared by economy and defense, and
  each soldier owns equipment. 局内/局外 are gameplay states, not separate maps or cameras.
- Current local ownership and update order: [code boundaries](docs/code-layout.md).
  GameWorld has no window, UI, audio or GPU dependency. Do not add speculative networking classes;
  the authoritative server/client split remains a future direction.
- Map and camera behavior: [continent](docs/continent.md), [world map](docs/world-map.md).
  Preserve shared player/collision between views and the single Gameplay input context.

## Build and dependencies

Use [BUILD.md](BUILD.md) for commands and prerequisites, [CI](docs/ci.md) for remote jobs,
and [environment notes](docs/development-environment.md) for local troubleshooting.
Start with `make`, `make run`, and `ctest --test-dir build/linux-x86_64/Debug --output-on-failure`.

- Build trees are `build/<target-platform>/<build_type>/`, each with its own generators, shaders
  and dependencies. `conanfile.py::layout()` pins this for single- and multi-config generators.
  Do not share a CMake cache across toolchains.
- Specify Conan build type explicitly. Preserve `presets_prefix` and `build_folder_vars`;
  after changing preset generation, reinstall every build type in CMakeUserPresets.json to avoid
  duplicate/stale names. Toolchain profiles and manual build commands are documented in BUILD.md.
- `JOBS` defaults to 8 in Make to protect this 15 GB desktop during dependency builds. It limits
  both Conan and CMake; override when appropriate. CI uses tool defaults, not this local cap.
- Use `make run`: it sources `generators/conanrun.sh` (including `VK_LAYER_PATH`) and runs from
  the build tree, where relative `shaders/` and copied `runtime/assets/` and `runtime/saves/` live. `LC1_ASSET_ROOT` overrides
  the asset root. Release still needs the correct working directory.
- Windows runs through Wine in `build/wine-prefix`, not `~/.wine`. MinGW omits validation layers;
  if the target environment has none, explicitly use `LC1_NO_VALIDATION=1 make run
  PLATFORM=windows-x86_64`. Do not silently disable validation in the run target.
- Libraries come from Conan, except pinned URL+SHA256 VMA-Hpp FetchContent in
  `third-party/CMakeLists.txt`. It bundles `vk_mem_alloc.h`; do not add a second Conan VMA.
  Slang is an external build tool; a complete installation is required (see quirks below).
- Project dependency defaults belong in `conanfile.py::default_options`; profiles describe
  toolchains/environment and may override them. Do not inherit unrelated `default` host options.
  Name static libraries individually; `*:shared=False` can override required shared dependencies.
- Linux defaults are Wayland-only: GLFW `with_wayland=True`, GLFW/XKB `with_x11=False`, and
  validation layers `with_wsi_xcb/xlib=False`. Wayland and XKB must remain shared because GLFW
  dlopens them. X11 support requires enabling those four options and installing system dev packages.
- Validation layers are a Debug-only Conan dependency; `with_validation_layers=False` controls
  installation, not the application's Debug validation behavior. CPU-only CI does not need them.
- Preserve MinGW's prefixed C/C++/`rc` tools and CMake link fixes: `stdc++exp` for `<print>`,
  executable `-static` for compiler runtime portability, and KTX 4.4.2's omitted
  `astcenc-avx2-static` companion archive from the same Conan package. Link flags belong in CMake;
  profile `*_FLAGS_INIT` changes do not update an existing cache. TinyGLTF filenames require UTF-8.

## Coding conventions

- Use English in code, comments, diagnostics, UI strings, fixtures and generation scripts.
  Design documents in `docs/` remain Chinese.
- Follow the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
  Types and enum values: `PascalCase`; functions, variables and constants: `snake_case`;
  private members: trailing `_`; macros only: `ALL_CAPS`. No type/scope/storage prefixes.
  Preserve Vulkan API spelling (`vk::`, `VK_`, `sType`, `eColorAttachmentOptimal`); do not alias it away.
- Headers are `.hpp` with `#pragma once`; sources are `.cpp`. Before finishing C++ changes, run
  `clang-format --style=file -i <changed .hpp/.cpp files>` using the repository's `.clang-format`.
- Prefer `const`/`constexpr`; read-only parameters use values or `const&`. Pass views such as
  `std::string_view`/`std::span`, not new pointer+length interfaces. No C-style casts;
  `reinterpret_cast` needs a comment identifying the C API requirement.
- Represent non-owning data members with pointers, not references; document required non-null
  borrows and lifetime constraints. Reference parameters and return values remain appropriate.
- Use RAII, constructors and destructors, not `init()`/`destroy()`, owning raw pointers or
  `new`/`delete`. Raw handles are documented borrows. `Window` is the explicit exception: it owns
  its GLFW C handle and destroys it once. Its first member, `glfw_lifetime_`, also terminates GLFW
  if construction fails; keep that guard.
- Build `vk::` structs with designated initializers in declaration order. Keep
  `VULKAN_HPP_NO_STRUCT_CONSTRUCTORS`; retain setters for checked array/count pairs, and do not
  define `VULKAN_HPP_NO_SETTERS`.
- Use `fail()`/`lc1::Error` for project diagnostics; add no subclasses or parallel error-code/out-param
  scheme. `fail()` captures its call site automatically; use `Error::diagnostic()` in logs and
  `what()` for UI/messages. When adding context to an existing Error, preserve its `location()`.
  Let `vk::SystemError` carry Vulkan diagnostics; wrapping every call is unnecessary.
  `main` catches both `lc1::Error` and `std::exception`. Preserve the user's intentional Debug
  `throw;` in both handlers after logging; Release returns `EXIT_FAILURE`. Do not remove these
  rethrows to suppress `terminate` diagnostics.
- World space is right-handed, Y up. glTF needs no axis conversion; Z-up tutorial/OBJ data does
  (the tutorial room needs −90° about X). Use negative viewport height with `y = height` for Vulkan
  clip Y. Keep back-face culling: dynamic front face is clockwise for negative model determinant,
  counter-clockwise otherwise. Bake import correction once in `Model`, never again per instance.

## Architecture and ownership

- Targets: `lc1` (`app/`) → `lc1_game` (`include/lc1/game/`, `src/game/`) → `lc1_engine`.
  Engine never depends on game. Public APIs use `include/lc1/` and `"lc1/..."`; app/game must not
  include engine implementation files. See [code boundaries](docs/code-layout.md).
- `include/lc1/vk/` mirrors `src/vk/`: `core` ← `resources` ← `presentation` and `render`.
  Presentation/render may also use core but never depend on each other. Core has no Window,
  scene or game dependency. See [Vulkan layers](docs/vulkan-layers.md).
- Keep session responsibilities and lifetime order in [code boundaries](docs/code-layout.md).
  Apply player commands before physics and update the camera afterward. Use one finite,
  non-negative simulation delta capped at 0.1 seconds. Update the fixed roster in keyid order,
  followers after leaders. Menus gate controls while simulation continues.
  Keep cursor capture before reading mouse deltas and ImGui backend lifetime in Application.
- `Window`/`input.hpp` include no Vulkan headers; input also includes no GLFW headers. `Surface`
  bridges GLFW and Vulkan. Application selects the physical device and checks surface support;
  Device receives the selected device, queue family and requirements, and retains no surface.
  Swapchain borrows Surface and receives an explicit extent. Window sizes use
  `FrameExtent`, converted to Vulkan types by Application.
- Preserve construction order and reverse destruction order:
  `VulkanLoader → Window → Instance → Surface → Device → Renderer → GameSession → FrameLoop`;
  Swapchain is created lazily for usable extents. Loader must die last and Window after all Vulkan
  objects. `Application` and `GameSession` are immovable because members borrow other members.
- Device's allocator dies before its logical device; Instance's messenger before its instance;
  Swapchain's image views/semaphores before its handle. Recreate must explicitly clear images
  before the handle and check zero extent before destroying anything. Surface outlives Swapchain.
  `SwapchainImage::image` is borrowed: wrapping it in `vk::raii::Image` would double-free it.
- `Application::run` waits for GPU completion on normal and exceptional loop exits while resources
  still live. RAII destruction does not wait for pending GPU work. Never operate on null/default
  raii objects: their operation dispatchers may be null.
- `FrameLoop<FrameResources>` owns per-slot resources, built by an application-supplied factory.
  It waits for the slot fence before passing resources to the synchronous recording callback;
  it does not interpret them. Renderer takes resources, camera and draws, not a frame index.
  Draw i uses that slot's object/material/frame UBOs; capacity overflow fails explicitly.
  Descriptors are initialized before use, and `GpuBuffer::upload` performs the required VMA flush.
- `DrawItem`, `scene::Object` and `RenderTarget` borrow resources. Resources precede their users
  in member order. Material owners keep textures alive through submitted GPU uses; Device outlives
  GPU resources.
- `ResourceManager` is scene-scoped, exclusive and render-thread-only. Typed caches own
  `unique_ptr<Resource>`; repeated loads return stable cached pointers, failures leave no null entry.
  `load<T>` requires a Resource-derived, move-constructible T with `load_from_file(Device const&,
  path const&) -> T`. Meshes still have a separate cache. GpuTexture uploads CPU Image data;
  it is not a file-loading Resource, and ResourceManager must not depend on Renderer.
- Resource IDs are normalized relative to the supplied asset root; reject absolute IDs and lexical
  escapes. Symlink aliases are not deduplicated. Individual unloading invalidates that resource's
  borrows; there is no reference counting. Stop using affected borrows and wait for their GPU uses
  to complete before unloading a resource or destroying the manager.
- Character's `scene::Transform` is its sole world pose: feet origin, upright/unit scale, body yaw
  about +Y with zero facing −Z, look relative to body. `move`/`walk` resolve horizontal collision
  without changing facing. Controllers consume resolved input and borrow world/character;
  Character owns no camera/window/router or render resources; WorldView derives draws from its pose.
  See [character and import correction](docs/character.md).

## Vulkan dispatch and synchronization

- One system loader: `VulkanLoader` keeps a long-lived `vk::raii::Context` and supplies its
  `vkGetInstanceProcAddr` to `glfwInitVulkanLoader` before GLFW initialization. No Conan
  `vulkan-loader`, volk, bare `vk*` calls or second/default dispatcher table.
- Keep `VK_NO_PROTOTYPES` and `VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL` at its default 1.
  Record/submit through `vk::raii::CommandBuffer`/`Queue`, not plain `vk::` handles. Dereference
  raii handles for raw arguments and hpp handle arrays (`*handle`), including ArrayProxy setters.
- Include `common.hpp` before VMA so it sees `VK_NO_PROTOTYPES`. With
  `vma::raii::Allocator{instance, device, info}`, leave `info.instance`, `info.device` and
  `info.pVulkanFunctions` null; the wrapper fills them from raii dispatchers.
- Application performs device selection using the explicit requirements and feature checks from
  `src/vk/core/device.cpp`, including BDA,
  maintenance5 and ray-query/acceleration-structure/deferred-host-operations extensions. Check
  required names/features and graphics+compute+present support; exclude CPU devices. Keep VMA
  feature flags consistent. No generic capability-negotiation framework. `LC1_DEVICE=<substring>`
  selects among GPUs; startup logs name/type/driver.
- Keep `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`: OUT_OF_DATE is frame-loop control flow.
  Catch `vk::SurfaceLostKHRError` before `vk::SystemError` and return `FrameResult::SurfaceLost`.

The following frame-loop invariants must survive refactoring:

1. Reset a slot's fence only on the path that reaches queue submission; otherwise its next wait
   can block forever.
2. Check acquire's result before reading its image index. SUCCESS and SUBOPTIMAL yield an image;
   SUBOPTIMAL also signals the semaphore, so render/present it and recreate afterward. Do not
   abandon it as OUT_OF_DATE (`VUID-vkAcquireNextImageKHR-semaphore-01286`).
3. Reset only the current frame's command buffer, never a pool containing another pending frame
   (`VUID-vkResetCommandPool-commandPool-00040`).
4. The acquire→render barrier's source stage is `COLOR_ATTACHMENT_OUTPUT`, matching the semaphore
   wait stage; `NONE` leaves the layout transition unordered (`SYNC-HAZARD-WRITE-AFTER-READ`).
5. Each actual swapchain image owns its present semaphore. Never size a separate array from the
   requested minimum image count; actual counts may be larger and change on recreation.

## Rendering contracts

- Project passes and GPU tests use `VK_EXT_shader_object`, not `VkPipeline` objects; ImGui's
  official Vulkan backend is the exception. `ShaderObject` owns executable stages in resources;
  `GraphicsShaders` binds pass stages and clears unused stages, including fragment for depth-only
  draws. `set_graphics_state` restores fixed-function state per pass; viewport/scissor use the
  with-count commands. Preserve the main fragment shader's static SampleId use for full sample
  shading; shader objects have no dynamic `sampleShadingEnable`. See [shader objects](docs/shader-objects.md).
- All project rendering passes use native descriptor heaps and push-data indices; no descriptor
  sets, set layouts or pipeline layouts. `DescriptorSet` has been removed. ImGui's official Vulkan
  backend is the sole allowed exception and retains its internally managed descriptor pool/sets.
  Per-frame heaps isolate descriptor updates from in-flight frames. Cache immutable buffer lookups;
  update explicitly owned image slots only after the owning frame fence.
- Normal lighting uses metallic-roughness GGX/Smith/Schlick with hemisphere ambient fill, but
  no IBL. Current lighting behavior is documented in [lighting](docs/lighting.md). Material
  factors are linear; base/emissive maps are sRGB, MR/normal are linear (MR G=roughness, B=metal).
  Initialize every texture descriptor. AO textures remain unsupported and must be rejected.
- Resolve MSAA in linear RGBA16F HDR before exposure/Reinhard tone mapping. Cap at 4× and check
  actual HDR/depth format support. sRGB attachments encode automatically; UNORM display outputs
  request `OutputEncoding::Srgb`; default output is linear. See [lighting](docs/lighting.md).
- Normal maps use UV0 MikkTSpace tangents and +Y bitangents. Generate valid tangents before upload;
  maps baked to an existing basis need matching tangents before nonuniform import correction.
  Preserve inverse-transpose normals, reflected winding/handedness and mirrored UV seams.
  Shadows, temporal history and filtering use the unperturbed surface normal.
- Meshes own BLASes; each frame slot owns TLAS/build inputs/scratch, updated only after its fence
  and reused when unchanged. Copy GLM transforms into Vulkan's row-major 3×4 layout correctly.
  Off-camera casters still participate. Ray-query shadows honor `casts_shadow`; text is drawn
  by the text pass and is never a draw item, so it casts nothing.
- Shadow rays run in a separate single-sample pass, not once per MSAA sample. Local lights use
  unbounded inverse-square attenuation; do not add range/cutoff. Sphere lights use a receiver-facing
  disk approximation (radius zero is point light), normally one ray/frame and up to 32 history frames.
- Temporal history belongs to Renderer-owned `RayQueryShadows` and spans submissions; resizing
  waits for all GPU work, not just a slot fence. Reject moving/disoccluded receivers. Never feed
  spatially filtered visibility back into temporal history. Object motion vectors are not implemented.
- Push each pass's resource indices explicitly. The renderer uses ray-query shadows; the old
  shadow-map preview diagnostic has been removed. See [ray-query shadows](docs/ray-query-shadows.md).

## Input contracts

- Window owns raw `InputState`; game-owned `Binding` rows in `src/game/input.cpp` map `Key`
  (including mouse buttons) to game-owned `Action`. Engine `BasicInputRouter` resolves only the top
  context; game `InputRouter` is its typed alias. Game code asks for actions.
  `Ui` intentionally binds nothing and gates gameplay. Window interprets no keys; ESC is app policy.
- `poll_events()` calls `begin_frame()` before pumping GLFW; `wait_events()` is not a frame boundary.
  Drop `GLFW_REPEAT` for game actions. Focus loss releases all held keys; capture changes, focus
  regain and pointer re-entry reset the mouse-delta baseline.
- Recompute action state from scratch each update; keep update idempotent. Mask `released` with
  `!held` so releasing one of multiple bindings does not end a held action; do not similarly mask
  `pressed`. Keep the GLFW↔Key mapping's completeness/uniqueness assertions.
- Apply a view toggle, then cursor capture, then read mouse deltas. Gate gameplay by control mode,
  focus and context independently of cursor capture. Cursor positions are GLFW logical screen
  coordinates, not framebuffer pixels; picking needs conversion at the framebuffer boundary.

## Validation and environment

- CPU CTest covers world data, collision/traversal, character/model correction, tangents and view
  controls without a window or Vulkan device. GPU rendering requires separate validation smoke
  tests. Build/run the relevant explicit GPU targets from the build tree with the Conan run env:
  `lc1_pbr_tests`, `lc1_temporal_shadow_tests`, `lc1_shadow_path_tests`, and descriptor-heap tests
  documented in [descriptor heap testing](docs/descriptor-heap-test.md). These are outside CPU CTest.
- Debug enables validation and synchronization validation. The callback prints the VUID and aborts
  on ERROR; preserve line-buffered stdout so startup logs survive abort. Require clean validation
  output for rendering checks. Never combine GPU_ASSISTED with SYNCHRONIZATION_VALIDATION.
  `LC1_NO_VALIDATION=1` is an explicit diagnostic/performance opt-out, not validation evidence.
- Read [environment notes](docs/development-environment.md) before changing Slang setup,
  the empty slangdconfig.json root marker, XKB run environment or compositor workarounds.

## Documentation maintenance

- Start at [the documentation index](docs/README.md); read the relevant subsystem before editing.
- Keep one primary document per contract; link to it instead of copying implementation summaries.
- Put dated investigations and superseded plans in [history](docs/history/README.md).
  Keep active external workarounds in [external defects](docs/external-defects.md).
- Update current behavior with code changes; distinguish implemented behavior, future plans and
  historical observations. Do not remove durable constraints merely because code disagrees.
