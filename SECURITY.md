# Security Policy

## Supported Versions

Boreas is pre-1.0. Security fixes are applied to the latest `0.1.x` release and
to `main`.

| Version | Supported |
|---------|-----------|
| 0.1.x   | ✓         |
| < 0.1   | ✗         |

## Reporting a Vulnerability

**Please do not open a public issue for security vulnerabilities.**

Report privately using GitHub's
[Report a vulnerability](https://github.com/intercreate/boreas/security/advisories/new)
button (the **Security** tab → **Advisories**), or email **seth@intercreate.io**.

Please include:

- the affected version or commit,
- a description of the issue and its impact, and
- steps to reproduce, if possible.

You can expect an initial acknowledgement within a few business days. We will
keep you informed of progress and coordinate disclosure once a fix is available.

## Scope

Boreas is a Zephyr-compatible API layer over ESP-IDF / FreeRTOS. In-scope
reports include memory-safety defects (buffer overruns, use-after-free),
synchronization errors that can lead to corruption, and similar issues in the
components under `components/`. Vulnerabilities in ESP-IDF or FreeRTOS
themselves should be reported to their respective upstream projects.
