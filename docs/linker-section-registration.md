# Linker-section registration

Several boreas macros register entries into iterable linker sections rather
than running `__attribute__((constructor))` functions:

- `SYS_INIT()` → `.sys_init_entries`
- `LOG_MODULE_REGISTER()` → `.log_module_entries`
- `LOG_BACKEND_DEFINE()` → `.log_backends`
- `SHELL_CMD_REGISTER()` → `.shell_root_cmds`

Each subsystem ships an ESP-IDF `.lf` linker fragment that uses `ldgen` with
`KEEP`, `ALIGN`, and `SURROUND` to place these input sections inside
`.dram0.data` (so they're loaded from flash at boot) and emit
`_<name>_start` / `_<name>_end` boundary symbols. At boot, the corresponding
`*_init()` function walks the section and populates a runtime registry.

## Why sections and not constructors

ESP-IDF compiles components into static libraries. When linking the final
image, GNU LD's archive-member rule is narrow: an object file is pulled out
of an archive **only to resolve an unresolved symbol reference**. A TU whose
only exported contribution is a constructor + a static registration struct
has no externally-referenced symbol, so the entire `.o` gets dropped — the
constructor never runs and the registration silently disappears.

The linker-section approach removes the need for a constructor. The
registration struct is placed in a named section, and iteration at boot
walks the SURROUND-generated `_start/_end` symbols. `KEEP` prevents
`--gc-sections` from dropping the entries.

## The archive-pull constraint (important)

**Linker scripts do not pull archive members. Only unresolved-symbol
references do.**

This matches ESP-IDF's own documented constraint on `ESP_SYSTEM_INIT_FN`
(`esp_system/include/esp_private/startup_internal.h`):

> Initialization functions should be placed in a compilation unit where at
> least one other symbol is referenced in another compilation unit.

The same rule applies to every boreas macro listed above. Place callsites in:

- `main/` (or any equivalently whole-linked TU), **or**
- any component TU that exports some other symbol referenced from outside
  the TU.

A `.c` file whose only contribution is a `SYS_INIT()` (or similar) sitting in
a component library will be stripped by the linker with no warning. The
registration struct compiles fine, makes it into the `.a`, and then
disappears at final link.

## Why `.lf` fragments (and not raw `.ld` scripts)

Early iterations tried raw `.ld` scripts supplied via
`target_link_options(... INTERFACE "-T...")` with their own `SECTIONS`
blocks. This looked cleaner on paper but had two fatal problems:

1. **Symbols without initialized data**. A raw `SECTIONS { ... } > dram0_0_seg`
   assigns a VMA in DRAM but is *not* included in ESP-IDF's flash-to-DRAM
   load segment. At boot, the VMA holds whatever random bytes the SRAM
   powered up with — the section appears to exist but its contents are
   uninitialized garbage. Iteration then reads wild pointers and crashes.
2. **`INSERT AFTER .dram0.data` silently fails** across separate `-T`
   scripts — the target output section defined in ESP-IDF's own
   `sections.ld` isn't visible to a supplementary `-T` script at the time
   `INSERT` is resolved.

Using ldgen `.lf` fragments sidesteps both: the input sections are merged
into the existing `.dram0.data` output section (which is already part of
the load segment), and `SURROUND` emits the boundary symbols we need.

## Naming conventions

- Section names **start with a leading dot** (e.g. `.sys_init_entries`).
  Both the `__attribute__((section(".name")))` in the macro and the
  `[sections:name] entries: .name+` in the `.lf` must refer to the same
  literal name.
- Boundary symbols are `_<name>_start` / `_<name>_end` (single underscore
  prefix, `_start` / `_end` suffix). This is what ldgen's `SURROUND(name)`
  emits — not the GNU-LD orphan-section `__start_<name>` / `__stop_<name>`
  pair, which would only fire for orphan sections.
- Use `ALIGN(4, pre, post)` in the `.lf` mapping so the boundary symbols sit
  on 4-byte boundaries. Without this, the *start* symbol can land at an
  unaligned byte and `(end - start) / sizeof(struct)` yields a non-integer
  count.

## Host-test (linux target) fallback

The unit-test binary is built against ESP-IDF's linux preview target, which
runs on the developer host (macOS or Linux). This target doesn't expose the
ESP-specific DRAM layout that ldgen uses to materialize `SURROUND` symbols,
and on macOS hosts the Mach-O section syntax differs from ELF. Each macro
uses a `#if defined(CONFIG_IDF_TARGET_LINUX)` guard that falls back to the
legacy constructor pattern on this target. That's safe because the host
test executable is whole-linked — archive-stripping isn't a concern there.

Both paths populate the same runtime registry, so dispatch and iteration
code is target-agnostic.

## Pattern for new linker-section registrations

1. Declare the descriptor struct in a public header.
2. Emit instances into a dotted named section via
   `__attribute__((section(".my_section"), used))`. Guard with
   `#if defined(CONFIG_IDF_TARGET_LINUX)` to fall back to a constructor that directly
   registers with the runtime API.
3. Add the section + mapping to your component's `.lf`:

   ```
   [sections:my_section]
   entries:
       .my_section+

   [scheme:my_section_placement]
   entries:
       my_section -> dram0_data

   [mapping:my_section_placement]
   archive: *
   entries:
       * (my_section_placement);
           my_section -> dram0_data KEEP() ALIGN(4, pre, post) SURROUND(my_section)
   ```

4. Register via `LDFRAGMENTS "mycomp.lf"` in your component's
   `idf_component_register(...)`.
5. In the subsystem's init entry point, walk `_my_section_start` to
   `_my_section_end` on ESP and call the runtime-register function for
   each entry. Do not walk the section on `CONFIG_IDF_TARGET_LINUX` — constructors have
   already populated the runtime state there.
6. Document the archive-pull constraint in the macro's docblock.

## Two gotchas to avoid

### Anchoring an archived TU

If you need a TU's registration entry pulled from a component archive but
the TU has no other externally-referenced symbol, add an exported anchor
(e.g. `const int foo_anchor = 1;`) and reference its **address** from a TU
that is already reached — ideally inside a function that's actually called,
with a volatile load so the optimizer can't elide it:

```c
extern const int foo_anchor;
const int * volatile p = &foo_anchor;
(void)*p;
```

A file-scope `__attribute__((used))` pointer is **not sufficient**:
`--gc-sections` strips unreferenced exported rodata even with `used`. Only
code inside a reached function, or a reference from a KEEP'd section,
survives both passes.

### CONFIG\_\* must be visible when the macro is defined

If your macro is guarded by `#if defined(CONFIG_FOO)`, include `sdkconfig.h`
at the top of the header that defines the macro. Otherwise the preprocessor
may define the empty fallback branch (CONFIG not yet visible), and a later
`#if defined(CONFIG_FOO)` in a `.c` file can still see CONFIG (pulled in by
a different include) and compile surrounding code that *calls* the macro —
but the macro invocation expands to nothing. Silent no-op at runtime.

## Verifying it works

Build for a real target (e.g. ESP32-S3) and inspect the ELF:

```
xtensa-esp32s3-elf-nm build/<proj>.elf | grep -E "_<name>_start|_<name>_end"
```

- Both symbols should appear in a DRAM address range (e.g. `0x3fc9xxxx` on
  ESP32-S3), not IRAM. DRAM placement is what lets the bootloader copy the
  initialized contents from flash.
- `end - start` should equal `N * sizeof(entry_struct)` for `N`
  registrations. If it's not an integer multiple, the boundary symbols
  aren't aligned — check that the `.lf` mapping has `ALIGN(4, pre, post)`.
- If the delta is zero but you expect entries, a TU with a registration
  callsite was dropped by archive stripping — add an external symbol
  reference to anchor it, or move the callsite into `main/`.
