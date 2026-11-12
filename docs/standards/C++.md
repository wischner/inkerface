# C++ Coding Standards

> **Scope:** Modern C++ applications and libraries developed primarily on Linux with GCC/G++.
> This standard applies to human-written and AI-generated code alike.

This document defines how C++ code is organized, written, secured, tested, and maintained. Its purpose is not to mandate clever techniques; it is to make programs **correct, safe, understandable, and verifiable**. Safety and clarity take priority over brevity, novelty, or micro-optimizations.

The requirements are grouped by subject. Project-specific rules belong in separate documents and must not weaken the mandatory safety requirements here.

## How to interpret this standard

- **MUST / MUST NOT**: mandatory; deviations require the process described in [Exceptions and deviations](#26-exceptions-and-deviations).
- **SHOULD / SHOULD NOT**: default practice; a sound technical reason may justify an alternative.
- **MAY**: permitted option, not a requirement.
- **Prefer**: choose this option when its preconditions hold and it improves clarity or safety.

Requirements concerning security, undefined behavior, or data races are not made optional merely because a workaround appears to work. Some techniques depend on the project (for example, fuzzing a parser or testing concurrent code); apply them when relevant rather than adding artificial complexity.

---

# Part I — Foundations and Project Organization

## 1. Core principles

1. **Correctness before optimization.** Choose simple, auditable solutions. Optimize demonstrated bottlenecks and measure the result.
2. **Make invalid states difficult to represent.** Express invariants, units, nullability, ownership, and failure in types and interfaces.
3. **Ownership and lifetime are explicit.** Every resource has a clear owner, and non-owning access never outlives its referent.
4. **Trust ends at boundaries.** Validate external data before allowing it into trusted program state.
5. **Failure is designed, not improvised.** Errors must leave objects and resources in a defined state.
6. **Verification is part of development.** Builds, tests, diagnostics, and analysis are required evidence—not optional cleanup.
7. **No hidden behavior changes.** Code changes, including AI-authored changes, must be scoped, reviewed, tested, and documented.

## 2. Language standard and toolchain

- **C++20 is the default minimum** for new projects; C++23 MAY be used when the supported toolchain and standard library implement the required features. Existing C++17 projects MAY retain C++17 when compatibility is an explicit requirement.
- Every project MUST explicitly declare its language standard (for example, `-std=c++20`), supported compiler/standard-library versions, and platform assumptions. Do not rely on the compiler's default standard.
- Build and test with **G++ first** on Linux. Additional compilers and platforms MAY be included in CI.
- Prefer portable standard C++ over compiler-specific extensions. Document and isolate extensions where necessary.
- Do not use features newer than the declared project standard without an approved compatibility strategy.

Feature availability matters: `std::optional` and `std::variant` require C++17; `std::span`, `std::format`, ranges, and `std::jthread` require C++20; `std::expected` requires C++23. Library availability must also be verified for the actual toolchain.

## 3. Project layout and generated files

Keep the source tree clean. Do not place temporary files, editor backups, object files, or other untracked build output in source directories.

```text
project/
├── src/                # Application implementation files
├── include/            # Root project's public headers
│   └── <name>/         # Public headers for a library
├── lib/                # Library implementations
│   └── <name>/
│       └── src/
├── tests/              # Automated tests by category
├── docs/
│   └── notes/          # Architecture, API, build, format notes
├── build/              # All intermediate build artifacts
├── bin/                # Final binaries and generated user-facing outputs
├── Makefile            # Top-level build entry point
└── README.md
```

- Root-project public headers MUST live in `include/`; library public headers in `include/<libname>/`.
- Application sources belong in `src/`; library implementation sources in `lib/<name>/src/`.
- Intermediate artifacts belong under the **single root `build/` tree**; do not introduce nested independent `build/` directories.
- Final binaries and user-facing generated outputs belong in `bin/`.
- Only normal project metadata and configuration files (such as `LICENSE`, `.gitignore`, `.clang-format`, `.clang-tidy`, and `Makefile`) belong at the root.
- The top-level `Makefile` MUST expose normal targets such as `all`, `clean`, and `test`; subdirectory makefiles MAY be used where helpful.
- Generated directories SHOULD be ignored by version control. A project MUST NOT commit secrets or accidental generated artifacts.

## 4. Naming and formatting

Consistency is more valuable than personal preference.

- Use **lowercase `snake_case`** for variables, functions, methods, classes, structs, enums, enum values, namespaces, and filenames.
- Do not use Hungarian notation, `PascalCase`, or `camelCase` for project-defined identifiers. Names mandated by third-party APIs are exempt.
- Reserve `UPPER_SNAKE_CASE` for preprocessor macros; prefer typed `constexpr` constants over macros. Existing macros with mandated external names are exempt.
- Use **four spaces**, never tabs, for indentation.
- Use **K&R braces** for functions and apply a consistent brace style throughout the project.
- Keep lines readable in an **80-column terminal**; break long expressions where practical.
- Prefer descriptive names and explicit units; avoid unexplained abbreviations and magic numbers.
- Initialize variables at declaration wherever possible; use `const` wherever an object should not change.
- Maintain a repository `.clang-format` configuration and use automated formatting checks.
- Use clang-tidy naming checks where feasible; a formatter alone does not enforce identifier spelling.

```cpp
namespace audio_engine {

class game_state {
public:
    void update_score(int delta);

private:
    int player_score_ = 0;
};

}  // namespace audio_engine
```

## 5. Headers, implementation files, and dependencies

Modules SHOULD have clear interface/implementation boundaries. Do not create a pointless header for a translation-unit-local helper.

**Public headers:**

- MUST contain an accurate file header, an include guard or `#pragma once`, public declarations/types/constants, and documentation for public APIs.
- MUST include the headers required for their own declarations; do not rely on transitive includes.
- SHOULD minimize dependencies and avoid exposing implementation details.
- SHOULD keep ordinary non-template function implementations in `.cpp` files.
- MAY define templates, small `inline` functions, and `constexpr` functions in headers when necessary; avoid large implementation bodies in public headers.

**Implementation files:**

- MUST include their own header first (if one exists), exposing missing dependencies early.
- SHOULD keep internal helpers and state out of public interfaces.
- SHOULD use this include order: own header, other project headers, third-party headers, standard library headers.

Every `.cpp` and header file MUST begin with a meaningful description of its role and important design constraints. Use the project's **actual** license and copyright information; do not copy placeholders into production code.

```cpp
// Implements playback scheduling and state transitions for the audio engine.
// Scheduling state is confined to the owning engine instance.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 <copyright holder>
```

Replace the last two lines as appropriate for the project. Avoid circular module dependencies; prefer focused interfaces and forward declarations where safe.

---

# Part II — Design, Types, and Resource Safety

## 6. API design and encapsulation

- Favor small, focused functions and classes. Apply encapsulation, single responsibility, and composition over inheritance.
- Prefer values over indirection where practical; pass small inexpensive objects by value and larger read-only objects by `const&` when appropriate.
- Use `std::string_view` and `std::span` for non-owning input only when the lifetime requirements are clear. Do not store such views without an explicit lifetime guarantee.
- Prefer references when null is not a valid value; document nullable pointers and ownership transfer.
- Prefer `enum class` or dedicated types over unstructured integers, magic sentinel values, or several opaque Boolean arguments.
- Mark single-argument constructors `explicit` unless implicit conversion is an intentional part of the API.
- Use `[[nodiscard]]` for results whose accidental omission is a bug.
- Prefer `constexpr` (and `consteval` when compile-time evaluation is required) for suitable operations.
- Prefer standard algorithms and ranges when clearer than hand-written loops.
- Avoid unnecessary global mutable state; prefer explicit dependencies and narrowly scoped state. A singleton is not a substitute for an ownership design.
- Keep source files under roughly **500 lines** when practical, splitting along meaningful responsibilities rather than arbitrary limits.

Avoid calls such as `create_window(true, false, true)` when named option types express intent more clearly.

## 7. Class invariants and special member functions

An object must have a well-defined valid state. Constructors and factories MUST establish class invariants, and public operations MUST preserve them. Prefer APIs that prevent invalid combinations from being constructed.

- Keep mutable implementation state private unless exposure is explicitly justified.
- Avoid partially initialized objects and externally visible actions before construction is complete.
- Follow the **Rule of Zero** by default: types composed of well-behaved members generally do not need custom destructors, copy operations, or move operations.
- Where manual resource management is unavoidable, consider the **Rule of Five**; explicitly delete operations that do not make sense.
- Mark moves `noexcept` only when that guarantee is true.
- Polymorphic base classes intended for deletion through a base pointer MUST have an accessible virtual destructor (or an explicitly designed alternative ownership interface).
- Do not rely on derived-class virtual dispatch from constructors or destructors.
- Document important invariants and failure guarantees near the public interface.

## 8. Ownership, RAII, and cleanup

Every resource MUST have a clear owner and deterministic cleanup strategy.

- Prefer automatic storage and value semantics. Use RAII for memory, files, sockets, handles, mutexes, and other resources.
- Use `std::unique_ptr` for exclusive dynamic ownership; use `std::shared_ptr` only when shared ownership is genuinely necessary, and `std::weak_ptr` to avoid ownership cycles.
- Raw pointers and references are **non-owning** in project interfaces unless an external API imposes a documented exception.
- Do not use raw `new`/`delete` or `malloc`/`free` in ordinary application code. Isolate necessary low-level allocation (custom allocators, C interoperability) behind reviewed RAII wrappers.
- Prefer returning by value or owning smart pointer rather than returning ownership through obscure out-parameters.
- Destructors MUST NOT throw; cleanup must remain safe during exceptions and partial construction.
- Do not return pointers, references, spans, or views into local objects or temporary storage.
- Do not assume `std::move` extends lifetime or itself performs a move; inspect ownership and the state of moved-from objects.

## 9. Lifetime, bounds, and iterator safety

- MUST NOT dereference null, dangling, or invalid pointers, or use an object after its lifetime ends.
- MUST NOT dereference invalidated iterators or use views after their backing storage is destroyed, moved, or reallocated.
- Prefer `std::span<T>`/`std::span<const T>` over pointer-plus-length pairs in C++20 interfaces.
- Validate indices and lengths before access. Prefer checked operations such as `vector::at()` when indices are untrusted or not otherwise proven valid.
- Unchecked access MAY be used when bounds are established by local, reviewable invariants; performance alone does not justify unproven access.
- Avoid new raw C arrays and pointer arithmetic where safer containers or spans suffice.
- Asynchronous callbacks MUST NOT capture non-owning references unless the referenced lifetime is guaranteed beyond the callback's completion.

```cpp
int read_value(const std::vector<int>& values, std::size_t index) {
    return values.at(index);  // Checked access when index is not trusted.
}
```

## 10. Undefined behavior and type safety

Undefined behavior is prohibited even if a particular build appears to work.

- MUST initialize objects before reading them.
- MUST NOT perform out-of-bounds access, invalid shifts, signed overflow, invalid pointer arithmetic, or access through incompatible types.
- MUST NOT rely on unspecified evaluation order when correctness depends on sequencing.
- Ban C-style casts in project code; prefer explicit, appropriate C++ casts.
- Treat `reinterpret_cast` and `const_cast` as exceptional interoperability mechanisms requiring specific justification and review, not merely a comment claiming safety.
- Avoid `void*`, native-layout type punning, and C-style unions; use typed alternatives (`std::variant`, defined serialization, or permitted representation copying).
- Prefer fixed-width integer types for protocol and on-disk fields; do not assume the sizes of `int`, `long`, or pointers.
- Do not suppress sanitizer or static-analysis findings without understanding the root cause and documenting a reviewed exception.

## 11. Integer, floating-point, and units safety

- Check arithmetic used to compute allocations, sizes, offsets, element counts, and indices **before** overflow can occur.
- Avoid implicit signed/unsigned mixes and narrowing conversions; validate ranges before explicit narrowing.
- Use `std::size_t` for sizes and indices when suitable, but do not treat unsigned arithmetic as automatically safe: it can wrap.
- Prefer dedicated types or explicit unit names for milliseconds vs seconds, bytes vs elements, and other incompatible quantities.
- Check shifts for valid operands and shift counts.
- Validate floating-point input for NaN, infinity, and unsupported ranges when relevant.
- Do not compare floating-point results for exact equality when the problem requires tolerance-based comparison.

```cpp
if (count > std::numeric_limits<std::size_t>::max()
                / sizeof(std::uint32_t)) {
    throw std::length_error("allocation size overflow");
}
const std::size_t bytes = count * sizeof(std::uint32_t);
```

This example prevents arithmetic overflow; allocation limits and container maximum sizes still require separate checks.

---

# Part III — Failure Handling and Security Boundaries

## 12. Error handling and contracts

An API MUST document its failure model and preserve its stated object invariants.

| Situation | Default approach |
| --- | --- |
| Programmer error or violated internal precondition | Assertion or explicit contract failure |
| Expected absence of a value | `std::optional` |
| Expected recoverable failure with an error value | `std::expected` (C++23) or a documented C++20 alternative |
| Exceptional operation failure | Exception |
| Resource cleanup | RAII |
| C ABI boundary | Explicit status/error convention; no escaping C++ exceptions |
| Unrecoverable invariant failure | Deliberate fail-fast or termination policy |

- Do not silently ignore failures or use exceptions for routine flow.
- Catch exceptions at boundaries where meaningful recovery, cleanup policy, or diagnostics are possible; do not catch-and-ignore.
- A destructor MUST NOT throw. Use `noexcept` only when the claimed guarantee is valid.
- Prefer the **strong exception guarantee** (failure leaves observable state unchanged) where practical; at minimum preserve invariants (basic guarantee).
- Assertions check programmer assumptions; they MUST NOT be the only validation of external input because they may be disabled in release builds.
- Do not leak sensitive implementation details in user-facing error messages.
- Document preconditions, side effects, error results, and whether partial work may remain after failure.

## 13. Trust boundaries and input validation

Treat network data, files, command-line arguments, environment variables, user input, and untrusted library output as untrusted.

- Validate **type, encoding, format, length, range, permitted values, and cross-field consistency** before using input.
- Prefer parsing into a validated domain type once, rather than scattering assumptions throughout the application.
- Reject malformed data explicitly. Normalization or sanitization MUST be appropriate to the destination; generic “sanitize everything” logic is not sufficient.
- Never pass attacker-controlled data as a `printf`-style or other interpreted format string.
- Prefer `std::string`, `std::string_view`, `std::format` where available, and typed formatting over unsafe C string operations.
- Restrict privileges and authority at each boundary; validate again when passing data into a different security context.

## 14. Resource exhaustion and work limits

Well-formed input can still be maliciously expensive. Every untrusted operation SHOULD have limits appropriate to its domain.

- Cap input length, record count, object count, recursion/nesting depth, decoded/decompressed size, and in-memory allocations.
- Check size arithmetic before allocating or iterating.
- Bound CPU work and avoid unexpected quadratic or exponential behavior on attacker-controlled data.
- Use timeouts for potentially blocking operations and cancellation for long-running operations where suitable.
- Handle allocation failure and partial processing without corrupting program state.
- Define limits in terms of actual product requirements; a universal constant is not a substitute for threat analysis.

## 15. Filesystem and process security

- MUST NOT interpolate untrusted input into shell commands. Avoid `system()` and `popen()` for operations involving external input; use argument-vector process APIs where possible.
- Never assume that path normalization alone prevents directory traversal.
- Restrict filesystem access to approved roots or capabilities. Account for absolute paths, `..`, symbolic links, and mount changes.
- Where race resistance matters, prefer directory-relative, handle-based operations (for example, suitable Linux `openat2()` resolution constraints), not check-then-open path logic.
- Use safe temporary-file creation and appropriate file permissions; do not construct predictable temporary filenames.
- Avoid time-of-check/time-of-use assumptions: filesystem entries can change between checking and opening.
- Use least privilege for files, processes, network permissions, and service accounts.
- Treat filenames, extensions, and apparent file types as untrusted metadata.

## 16. Binary formats, serialization, and parsing

- MUST NOT reinterpret untrusted byte buffers directly as native C++ objects or assume a native struct's padding/alignment matches a format.
- Define wire and disk formats in terms of precise field widths, byte order, versioning, and bounds.
- Validate every field and every offset before reading or allocating.
- Reject truncated, inconsistent, unsupported, or malformed data.
- Use explicit encode/decode functions; permitted representation-level operations such as `std::memcpy` still require object-lifetime and type rules to be respected.
- Never serialize raw pointers as persistent or portable object references.
- Fuzz parsers for untrusted inputs where practical, and keep regression cases for discovered failures.

## 17. Secrets, cryptography, and logging

- MUST NOT hardcode or commit credentials, private keys, or production secrets.
- MUST NOT log passwords, access tokens, private keys, or other sensitive content.
- Avoid secrets in process arguments and exception messages; minimize their copies and lifetimes.
- Do not assume `std::string::clear()` securely erases secret bytes from memory; use an appropriate secret-handling strategy when required.
- Use vetted cryptographic libraries and documented protocols; do not invent cryptographic primitives.
- Use cryptographically secure randomness for secrets and security decisions; `std::rand()` and `std::mt19937` are not suitable.
- Make logs useful for diagnosis without exposing private data, internal paths, or attack-sensitive details to unauthorized readers.
- Apply suitable log retention, access controls, and structured fields for the application's threat model.

## 18. Dependencies and supply-chain security

- Minimize external dependencies and prefer maintained, appropriately licensed libraries.
- Review new direct dependencies for purpose, maintenance, security, licensing, and transitive impact.
- Record versions and pin or lock dependencies where practical to support repeatable builds.
- Track relevant security advisories and patch vulnerable components promptly, based on exposure and severity.
- Do not fetch and execute unverified build scripts or copy unknown source code without review.
- Record release toolchain versions and enough build metadata to investigate or reproduce delivered binaries.
- Consider automated dependency scans and an SBOM for projects that distribute software or have material supply-chain risk.

---

# Part IV — Concurrency and Runtime Behavior

## 19. Concurrency and thread lifecycle

Prefer single-threaded designs when concurrency adds no demonstrated benefit. When multiple threads are required:

- MUST prevent data races and document synchronization for shared mutable objects.
- Prefer immutable data or message passing over shared writable state.
- Use `std::lock_guard`, `std::unique_lock`, and `std::scoped_lock` to manage locks through RAII. Manual `lock()`/`unlock()` is exceptional and requires review.
- Do not hold a lock across unknown callbacks or blocking I/O unless explicitly justified.
- Define a lock order where multiple locks may be nested; use `std::scoped_lock` for coordinated acquisition where suitable.
- Use condition variables with predicates; account for spurious wakeups and missed-notification hazards.
- Prefer `std::jthread` for suitable C++20 worker threads. Threads MUST have documented ownership, cancellation, and shutdown behavior.
- Do not detach a thread without a rigorously established lifetime and shutdown strategy.
- Use sequentially consistent atomics by default; weaker memory ordering requires documented proof of correctness.
- Atomic variables do not make compound operations automatically thread-safe.
- Document thread-safety guarantees on public APIs.
- Run **ThreadSanitizer** on applicable concurrent tests in its own build configuration; do not combine it with AddressSanitizer.

---

# Part V — Documentation and Maintainability

## 20. Function and API documentation

Every function or method declared in a public header MUST have a comment immediately above its declaration. Describe purpose, parameters, return value, side effects, limits, preconditions, ownership/lifetime, thread-safety, and failure modes **as relevant**. Avoid repeating what an obvious signature already says without adding useful information.

```cpp
// Look up an application ID by application name.
//
// Parameters:
//     name - Application name (usually space-padded).
//
// Returns:
//     Matching application ID, or a negative value if not found.
//
// Notes:
//     Commonly used for desktop/accessory integration.
int appl_find(const std::string& name);
```

Document non-obvious private helpers, invariants, and algorithms too. Comments MUST explain actual behavior and design rationale, not aspirational features or stale assumptions.

## 21. Project documentation and architecture

- Keep `README.md` current with build instructions, dependencies, basic usage, and supported platforms.
- Maintain design and operational notes under `docs/notes/`; useful examples include `ARCHITECTURE.md`, `BUILD.md`, `API.md`, and `DATA-FORMATS.md`.
- Write Markdown filenames in `UPPER-CASE-WITH-MINUSES.md` form, except conventional names such as `README.md`.
- Update documentation in the same change that alters behavior, interfaces, build commands, data formats, or operational assumptions.
- Document major design choices, trust boundaries, important invariants, and intentional tradeoffs.
- Avoid circular architecture and unnecessary abstractions; documentation should make the codebase easier to navigate, not replace readable code.

---

# Part VI — Build Verification and Release Quality

## 22. Build profiles, warnings, and hardening

The project MUST have working development, sanitizer-test, and release configurations. CI SHOULD run additional profiles appropriate to the codebase. Keep the normal debugger launch configuration working (for example, VS Code `F5` when VS Code is used).

### Compiler diagnostics

Use G++ with the declared C++ standard and at least the following warning baseline for project-owned source code:

```makefile
WARNINGS = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
           -Wconversion -Wsign-conversion -Wnull-dereference
```

- Treat warnings as errors in CI for first-party code (`-Werror`) once the warning baseline is established.
- Apply third-party warning suppressions narrowly; do not disable diagnostics globally to silence dependencies.
- Review warnings caused by conversions rather than weakening the entire policy.

### Debug and memory-safety testing

A typical GCC ASan/UBSan test profile is:

```makefile
CXXFLAGS += -std=c++20 -O1 -g3 -fno-omit-frame-pointer \
            $(WARNINGS) -Werror -fsanitize=address,undefined
LDFLAGS  += -fsanitize=address,undefined
```

Use `-O0` when needed for interactive debugging; sanitizer builds and ordinary debug builds MAY be separate. Sanitizer instrumentation MUST be present in the relevant linking step.

### Concurrency testing

A separate TSan profile for applicable concurrent code is:

```makefile
CXXFLAGS += -std=c++20 -O1 -g -fno-omit-frame-pointer \
            -pthread -fsanitize=thread
LDFLAGS  += -pthread -fsanitize=thread
```

**Do not combine TSan with ASan** in the same binary.

### Release hardening

Use optimization, warnings, and hardening suitable for the supported GCC, libc, linker, and target architecture. Where supported, GCC's `-fhardened` MAY be used after verifying the flags it enables (`g++ --help=hardened`). Otherwise evaluate explicit options including:

```text
-O2 -DNDEBUG
-D_FORTIFY_SOURCE=3
-fstack-protector-strong
-fstack-clash-protection
-fPIE                    # for relevant executable objects
-pie                    # executable link
-Wl,-z,relro,-z,now     # full RELRO, where supported
```

These options are **platform- and toolchain-dependent**, and some require optimization or specific libc support. Avoid blindly duplicating `-fhardened` options. Do not apply executable-only PIE flags indiscriminately to shared-library targets. The build MUST verify actual toolchain compatibility and test the optimized release artifact. Do not ship sanitizers as a substitute for production hardening.

## 23. Testing, regression tests, and fuzzing

- Automated tests belong under `tests/`, organized into useful categories (`unit/`, `integration/`, `regression/`, `fuzz/`, and so on).
- New behavior MUST receive appropriate tests; bug fixes SHOULD include a regression test reproducing the original failure.
- Test normal paths, errors, empty inputs, boundaries, invalid input, truncated input, and cleanup paths.
- Test construction/destruction, repeated initialization/shutdown, and failure recovery where relevant.
- Apply realistic resource limits in tests and exercise limit violations.
- Concurrency code SHOULD be stress-tested, especially cancellation and shutdown paths.
- Tests SHOULD be deterministic, isolated, and independent of execution order; never use production secrets or services for ordinary tests.
- Run ASan/UBSan tests regularly and TSan tests for applicable concurrent code.
- Fuzz parsers, decoders, protocol handlers, and other untrusted-input code where practical. Preserve failure-inducing inputs as regression cases.
- No failing or quarantined test may be hidden without a tracked explanation and owner.

## 24. Static analysis, formatting, and CI gates

CI is the enforcement mechanism for rules that can be checked mechanically.

- Run `.clang-format` verification and `clang-tidy` with relevant C++ Core Guidelines, bug-prone, performance, and readability checks. `cppcheck` MAY supplement these tools.
- A standard formatting check is `clang-format --dry-run --Werror` on applicable project source files.
- Build first-party code using the configured warning policy; unresolved first-party warnings MUST fail CI.
- Run unit/integration tests and applicable sanitizer suites. Sanitizer findings MUST fail the job.
- Build and test the **release configuration**, not just a debug executable.
- Static-analysis critical findings MUST be resolved or explicitly reviewed and documented.
- Changes to public APIs, file formats, dependencies, or behavior MUST include corresponding documentation updates.
- Record test commands and outcomes; never claim a check passed unless it was run successfully.

At minimum, the acceptance gate is: **formatting, compilation, tests, applicable sanitizer checks, static analysis, and a successful release build**. Projects MAY add coverage targets, fuzzing budgets, dependency scanning, and multi-toolchain testing based on risk.

---

# Part VII — Change Control and AI Assistance

## 25. AI-generated and assisted code

All generated or AI-assisted code MUST satisfy the same standards and review expectations as human-written code. The agent or contributor MUST:

- Inspect relevant source, declarations, build rules, and documentation before changing behavior.
- Never invent APIs, dependencies, build options, or file formats; verify them against the repository and supported toolchain.
- Preserve existing behavior unless a change is explicitly requested and explained.
- Keep modifications focused and avoid unrelated refactors, new global state, or gratuitous abstraction.
- Avoid new dependencies, subprocesses, network calls, or filesystem effects without a task-based justification.
- Never bypass input validation, silence warnings, disable tests, or suppress sanitizer findings merely to make a build pass.
- Fix root causes rather than conceal symptoms.
- Update tests and documentation when behavior changes.
- Report exactly what was built, tested, and reviewed, along with any checks that could not be run.
- Disclose unresolved issues and security-sensitive tradeoffs. Do not claim results that have not been verified.

Security-sensitive changes (authentication, parsing untrusted data, file access, concurrency, resource ownership, cryptography) SHOULD receive focused human review even when automated checks pass.

## 26. Exceptions and deviations

There are legitimate reasons for a narrow exception to a coding convention—for example, a custom allocator or required C ABI—but exceptions MUST be explicit.

- Identify the affected rule and scope of the deviation.
- Explain the technical reason and why the safer/default alternative is unsuitable.
- Keep exceptions localized; do not weaken project-wide compiler or sanitizer settings for one case.
- Obtain review for deviations involving unsafe casts, manual resource management, concurrency, input validation, or security boundaries.
- Explain localized diagnostic suppressions beside the affected code and ensure the suppression is as narrow as possible.
- No exception authorizes known undefined behavior, a known data race, silent security bypass, or a knowingly unmitigated critical vulnerability without an explicit project-level risk decision and containment plan.

---

## Adoption checklist

For each project, establish the following before calling the standard operational:

- [ ] Declared C++ standard, supported GCC/G++ and library versions.
- [ ] Approved source tree, root `Makefile`, separate build output, and working debugger setup.
- [ ] Checked-in `.clang-format` and `.clang-tidy` configurations.
- [ ] Documented API contracts, ownership conventions, and error model.
- [ ] Working debug, ASan/UBSan, release, and (when applicable) TSan targets.
- [ ] Automated tests covering successful and failing paths.
- [ ] CI checks for warnings, formatting, static analysis, tests, and release builds.
- [ ] Documented input trust boundaries, limits, filesystem privileges, and secret handling where relevant.
- [ ] Current `README.md` and architectural/build notes.
- [ ] A review process for exceptions and AI-generated changes.

**Final principle:** Write C++ whose safety properties are easy to explain, whose failures are predictable, and whose claims of correctness can be checked.
