# Mesa/Softpipe source map (pinned feasibility result)

Snapshot: Mesa `c515ec0fe77da9f42f390e442379f2d0629d908c` from
`sources.lock.toml`. This is source-level feasibility evidence, not a linked
dependency or conformance claim.

## GLES-to-Gallium path

| Layer | Pinned source/symbol | Role | Classification for UEFI |
| --- | --- | --- | --- |
| GLES dispatch/state | `src/mesa/main/*`, `_mesa_*` entry points | GLES validation and object state | Runtime; large hosted-runtime audit surface |
| Context bridge | `src/mesa/state_tracker/st_manager.c:st_api_create_context` → `st_create_context` | Creates Mesa state tracker over a `pipe_context` | Runtime |
| Draw bridge | `src/mesa/state_tracker/*` → `pipe_context::draw_vbo` | Converts GL state/draws to Gallium | Runtime |
| GLSL frontend | `src/compiler/glsl/*` → NIR | Parses/links GLSL ES | Runtime unless shaders are compiled offline |
| Shader lowering | `src/gallium/auxiliary/nir/nir_to_tgsi.c:nir_to_tgsi` | Lowers NIR for Softpipe | Runtime |
| Shader creation | `sp_state_shader.c:softpipe_create_vs_state` / `softpipe_create_fs_state` | Builds Softpipe shader state | Runtime |
| Screen/context | `sp_screen.c:softpipe_create_screen` → `sp_context.c:softpipe_create_context` | Installs and initializes the software driver | Runtime |
| Raster/execute | `sp_draw_arrays.c`, draw module, TGSI execute, tile cache | Executes primitives and fragments | Runtime |
| Display target | caller-supplied `struct sw_winsys` | Allocates/maps display resources | Runtime; replace with project-owned memory/GOP sink |

## Platform dependency classification

| Dependency | Runtime/build time | Required disposition |
| --- | --- | --- |
| Meson, Python generators, lex/yacc-style generated sources | Build time | Run on host; generated artifacts must be pinned/reproducible |
| allocation/reallocation/free and checked arithmetic | Runtime | EFI pool adapter or bounded arena; ownership accounting required |
| `sw_winsys` displaytarget map/unmap/display | Runtime | New GOP adapter; no framebuffer-layout assumptions |
| environment/debug option readers | Runtime in normal Mesa build | Compile out; immutable provider configuration only |
| time/timestamps | Runtime | Explicit UEFI monotonic/timer service |
| mutexes, condition variables, thread queues, TLS | Runtime in utility/compiler paths | Disable or replace; no hidden no-op shims |
| files, dlopen, driver discovery, shader cache | Runtime in normal build | Remove; static selection and in-memory shaders only |
| math/compiler helper symbols and floating-point setup | Runtime | Inventory per EFI object and supply only project-owned helpers |
| atomics | Runtime | Prove single-executor configuration or provide freestanding intrinsics |
| LLVM | Build/runtime depending configuration | Softpipe avoids LLVM, but GLSL/NIR remains substantial |

## Decision

**PIVOT for the current GLES subset.** Mesa's state tracker plus GLSL/NIR and
utility compatibility surface is much larger than the now-tested 0.1 provider.
The public protocol and private backend table remain deliberately compatible
with a future Mesa experiment, but Mesa is not linked into firmware. Reconsider
only when the subset needs enough shader/language behavior that the audited
compatibility layer is lower risk than extending the small renderer.
