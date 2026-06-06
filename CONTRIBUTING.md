# Contributing to Boreas

Thanks for your interest in contributing to Boreas! This document covers how to
get set up, run tests, and submit changes.

## Getting Started

### Prerequisites

- [ESP-IDF v5.4 or v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
  installed and available on your `PATH` (both versions are supported and
  tested in CI)
- A C compiler toolchain (GCC or Clang) for linux-target builds
- Git

### Clone and Build

```bash
git clone https://github.com/intercreate/boreas.git
cd boreas/test
idf.py --preview set-target linux
idf.py build
./build/boreas_test.elf
```

All tests should pass with zero failures before you make any changes.

### Recommended: enable the pre-commit hook

The repo ships a pre-commit hook that runs `clang-format` on staged C sources
so formatting problems are caught before CI. Enable it once per clone:

```bash
git config core.hooksPath tools/hooks
```

The hook skips quietly if `clang-format` is not installed; CI enforces
formatting either way.

## Development Workflow

1. **Fork** the repository and create a feature branch from `main`.
2. **Make your changes** in the appropriate component under `components/`.
3. **Add or update tests** in `test/main/` for any new or changed behavior.
4. **Build and run tests** on the linux target (see below).
5. **Open a pull request** against `main`.

## Running Tests

### Linux Target (required for all PRs)

```bash
cd test
idf.py --preview set-target linux
idf.py build
./build/boreas_test.elf
```

### ESP32-S3 (if you have hardware)

```bash
cd test
idf.py set-target esp32s3
idf.py build flash monitor
```

### Build Checks

Boreas builds with `-Werror`. Your changes must compile without warnings on both
linux and ESP32-S3 targets.

## Coding Standards

Boreas follows [Zephyr's coding style](https://docs.zephyrproject.org/latest/contribute/style/index.html). The repo ships Zephyr's `.clang-format` at the root -- run `clang-format -i` on changed files before submitting.

The canonical clang-format version is **21.x** (CI pins `21.1.8`; major versions
disagree on a few constructs). If your distro ships an older clang-format:
`pipx install clang-format==21.1.8`.

- **Formatting**: tabs (8-wide), K&R braces, 100-column soft wrap, `snake_case` identifiers. Enforced by `clang-format`.
- **Public API naming**: Zephyr conventions (`k_*`, `sys_*`, `device_*`, `z_*` for upstream-mirrored internals). Project-private helpers with no Zephyr analogue use `boreas_*`.
- **Struct vs typedef**: Types the caller initializes, inspects, or stack-allocates use explicit `struct` (e.g. `struct k_sem`). Opaque handles and callback signatures may `typedef` with `_t`. Don't typedef-hide a struct whose fields callers touch.
- **Headers**: Public Zephyr-mirrored headers live under `include/zephyr/...` and match upstream paths. Keep headers minimal -- only expose what downstream code needs.
- **Kconfig**: Use `CONFIG_*` symbols (with help text, defaults, `depends on`/`select`) for feature gates, not `#ifdef` with ad-hoc defines.
- **Doxygen**: Add `@brief`/`@param`/`@return` for any new public function or macro. Behavioral divergences from upstream Zephyr go in an `@note` on the declaration.
- **License headers**: New files include an SPDX line and copyright matching project convention.

## Pull Request Guidelines

- Keep PRs focused -- one logical change per PR.
- Write a clear description of *what* changed and *why*.
- Reference any related issues (e.g., `Fixes #12`).
- Ensure CI passes: clang-format check, linux build + test, and esp32s3
  build, each on ESP-IDF v5.4 and v5.5.
- Be open to feedback; maintainers may request changes.

## Reporting Bugs

Open an issue using the **Bug Report** template. Include:

- Which target and ESP-IDF version you're using
- Minimal reproduction steps
- Expected vs. actual behavior
- Relevant logs or backtraces

## License

By contributing, you agree that your contributions will be licensed under the
[Apache License 2.0](LICENSE), the same license as the project.
