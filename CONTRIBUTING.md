# Contributing to Boreas

Thanks for your interest in contributing to Boreas! This document covers how to
get set up, run tests, and submit changes.

## Getting Started

### Prerequisites

- [ESP-IDF v5.4+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
  installed and available on your `PATH`
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

- Follow the existing code style (K&R braces, 4-space indent, `snake_case`).
- Public APIs use Zephyr naming conventions (`k_*`, `sys_*`, `device_*`).
- Keep headers minimal -- only expose what downstream code needs.
- Use Kconfig for compile-time feature gates, not `#ifdef` with ad-hoc defines.
- Add a brief doc comment for any new public function or macro.

## Pull Request Guidelines

- Keep PRs focused -- one logical change per PR.
- Write a clear description of *what* changed and *why*.
- Reference any related issues (e.g., `Fixes #12`).
- Ensure CI passes (linux build + test).
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
