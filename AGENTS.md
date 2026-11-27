# AGENTS.md

Don't modify this file.

## Code & API
- C11 core; public headers C++ compatible. No unapproved API changes.
- C++ files: C-style C++ only. No exceptions, RTTI, STL, iostream, lambdas, virtual methods, inheritance, operator overloading, or new/delete (use the provided allocator or local variables). Avoid `auto`. All C rules apply.
- Use `bool`; prefer `uint32_t`/`uint64_t` unless negatives are needed.
- Style: `//` comments, Allman braces, 4 spaces, no tabs. Preserve existing conventions; don't reformat unrelated code.
- Avoid needless abstractions/macros. Factor duplicated blocks >50 lines.
- Line length: 160 chars. Separators: 127 chars (`// ----`) before each function implementation.
- Prefer designated initializers for struct initialization. Use compound literals with designated initializers when appropriate and when they improve readability.

## Code Quality
- Prefer the simplest correct implementation: minimize LoC, branches, helpers, and duplication without hurting readability, correctness, or maintainability.
- Keep concise code clear: no cryptic names, excessive compression, clever tricks, or obfuscation.
- Before adding code, reuse existing logic, remove redundancy, or simplify the design. Prefer deleting code over adding abstractions.

## Feature efficiency
* Reject low-ROI changes, even when technically beneficial.
* Prefer skipping a feature over significantly increasing code size for negligible gains.
* Every additional line of code must justify its maintenance and readability cost.
* Avoid speculative features, unnecessary abstractions, and premature generalization.
* Before implementation, report the expected benefit, estimated LoC, and feature efficiency.
* If the ROI is poor, recommend not implementing the change.

## Memory
- Libraries: no `malloc`/`free`; prefer caller-provided buffers. If a library needs more than the caller-provided buffer, return an error or a required-size value; don't allocate behind the caller's back.
- C++: allocate only via the provided allocator or local variables.
- Tools: `malloc`/`free` allowed outside critical hot loops.
- Up to 256 KiB: static buffer. Over 256 KiB: project memory interface.
- Make ownership explicit. Don't retain caller pointers unless required. Avoid hidden allocations and large temporary copies.

## State & Dependencies
- No mutable global state; use explicit contexts/parameters.
- Single-threaded: no mutexes, atomics, or lock-free structures.
- No unapproved third-party dependencies. Preserve platform/build requirements.
- Always prefer libraries listed in the [Preferred Libraries](#preferred-libraries) section over custom implementations.
- Reuse their APIs rather than reimplementing functionality they already provide.
- Before writing substantial utility code, check whether a preferred library already covers it.
- Use RNG SplitMix32; never `rand()`.

## Performance
- Favor compact layouts, cache locality, and linear buffer access.
- Avoid redundant passes over large data.
- Optimize only based on measured/profiled improvements; no speculative complexity.
- Never sacrifice correctness or maintainability for unmeasured optimization.
- Do not perform or interpret GPU profiling while the LLM is running. Request that the user collect and provide a standalone GPU profile instead.

## Comments
- Do not write descriptive comments in .c implementation files. The code must explain itself through clear naming and straightforward logic.
- Do not add comments that restate what the code does, explain obvious implementation details, or introduce sections of code.
- Comments are allowed only when necessary to explain non-obvious reasoning, tricky algorithms, constraints, or workarounds.
- Keep comments short and local. A comment explains at most one branch and sits on it: on the branch's `if` / `else` line or its first statement, at most 2 lines. A comment above a construct must never describe more than the line it sits on; a self-evident branch gets no comment.
- API documentation belongs in the .h header files. Document public functions, types, and parameters there when useful.
- Do not duplicate header documentation in the corresponding .c file.

## Build
- Config: `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build`
- Clean: `rm -rf build/`
- Test: `./build/unit_tests`

## Test
- Debug issues directly in the actual project. Do not create separate programs to reproduce or debug its implementation; small standalone programs are allowed only to independently validate or inspect the project's output.
- Use the existing code, build system, debugger, and runtime to identify and fix the root cause.
- Every new feature requires test. Adding new tests is allowed.
- Never modify/weaken tests to make them pass.
- Verify tests actually run before claiming success.
- Test behavior/results, not implementation details; don't enforce a specific implementation.
- Always validate changes using an AddressSanitizer (ASan) build. Treat any ASan report as a failure and fix all detected memory issues.
- For any change affecting shaders, GPU resources, pipeline state, or related host-side setup, run with GPU validation layers enabled. Treat all validation-layer messages as failures until fixed and verified. Metal validation can be enabled via the environment variable METAL_VALIDATION_ENABLED=1.
- No harness tests (tests that only exercise test scaffolding rather than product code).

## Versioning rules
When preparing a commit (CL), update the project version according to the following rules:

- Major version: Increment when the change introduces a breaking change.
- Minor version: Increment when the change introduces a new feature without breaking backward compatibility.
- Patch version: Increment for all other code changes, including bug fixes, performance optimizations, and refactoring.
- No version change: Do not update the version for non-code changes, such as documentation updates, comments, or formatting.

Always apply the highest applicable version increment. Never increment more than one version component for a single CL.

## Git
- Don't create a branch without permission
- Don't submit code without permission

## Completion
- Before reporting completion, verify: implementation finished, build succeeds, tests pass, rules followed, and no unrelated code changed.
- In case of failure (unit test, not reaching goal, etc...), write the experiment description in failed_experiments.md

## Preferred libraries

| Purpose | Library | Repository |
|---|---|---|
| JSON | sj.h | https://github.com/rxi/sj.h |
| Image loading | stb_image | https://github.com/nothings/stb 
| Image writing | stb_image_write | https://github.com/nothings/stb 
| Unit test | greatest | https://github.com/silentbicycle/greatest 
| Strings | String_View | https://github.com/tsoding/sv
| Command line parsing | flag | https://github.com/tsoding/flag.h
| Strings | String_View | https://github.com/tsoding/sv
| Image resize | stb_image_resize | https://github.com/nothings/stb_image_resize
| 2d rendering | onedraw | https://github.com/Geolm/onedraw
| HTTP client | naett | https://github.com/erkkah/naett
| Window management | sokol_app | https://github.com/floooh/sokol
| Memory management | vmem | https://github.com/Geolm/vmem
| Immediate UI | leanUI | https://github.com/Geolm/leanUI

