#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Intercreate
#
# Assert that K_ISR_SAFE helpers land in IRAM (.iram0.text), not in a
# flash-mapped text section. A K_ISR_SAFE (IRAM_ATTR) function that calls
# a flash-resident helper faults with a cache-access error when reached
# from an IRAM ISR during a flash-cache-disabled window (issue #53).
#
# The host (linux) build cannot observe this -- it is a target-only
# memory-placement property -- so this guard runs against a real target
# ELF. Section names (.iram0.text vs .flash.text/.text) are consistent
# across Xtensa and RISC-V targets, so the check is ISA-agnostic.
#
# The symbol set is DERIVED from source: every function carrying the
# K_ISR_SAFE attribute in components/*/src/*.c must be IRAM-resident, so a
# newly-added ISR-safe helper is covered automatically with no edit here.
# A symbol the compiler inlined into its IRAM caller has no out-of-line
# symbol and is reported as "skip" (safe). Override the derived set with
# IRAM_SYMBOLS="a b c" if you ever need to.
#
# Usage: tools/check_iram_symbols.sh <firmware.elf> [src-root]
#   OBJDUMP=<triple>-objdump   override toolchain autodetect
#   IRAM_SYMBOLS="sym1 sym2"   override the derived symbol set

set -euo pipefail

ELF="${1:?usage: check_iram_symbols.sh <firmware.elf> [src-root]}"
SRC_ROOT="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

# Derive the K_ISR_SAFE function set from source. A definition line
# carries the attribute and opens a parameter list; the function name is
# the identifier just before '('. Drop comment lines and the macro
# #define (the only other places the token appears).
derive_symbols() {
	grep -hE 'K_ISR_SAFE' "$SRC_ROOT"/components/*/src/*.c |
		grep -vE '^[[:space:]]*([*]|/[*]|//|#)' |
		grep -E '\(' |
		sed -E 's/.*[^A-Za-z0-9_]([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/\1/' |
		sort -u
}

SYMBOLS="${IRAM_SYMBOLS:-$(derive_symbols)}"
if [ -z "${SYMBOLS//[[:space:]]/}" ]; then
	echo "error: no K_ISR_SAFE symbols found under $SRC_ROOT/components" >&2
	exit 2
fi

# Pick the toolchain objdump if not supplied.
if [ -z "${OBJDUMP:-}" ]; then
	case "$(file -b "$ELF")" in
	*RISC-V*) OBJDUMP=riscv32-esp-elf-objdump ;;
	*Tensilica* | *Xtensa*) OBJDUMP="xtensa-${IDF_TARGET:-esp32s3}-elf-objdump" ;;
	*)
		echo "error: cannot determine toolchain for $ELF (set OBJDUMP=)" >&2
		exit 2
		;;
	esac
fi
command -v "$OBJDUMP" >/dev/null || {
	echo "error: $OBJDUMP not on PATH (run . \$IDF_PATH/export.sh)" >&2
	exit 2
}

# One symbol-table dump, reused per symbol.
SYMTAB="$("$OBJDUMP" -t "$ELF")"

fail=0
checked=0
while IFS= read -r sym; do
	[ -n "$sym" ] || continue
	checked=$((checked + 1))

	# A symbol-table entry's line names exactly one section.
	line="$(printf '%s\n' "$SYMTAB" | grep -E "[[:space:]]${sym}\$" || true)"

	if [ -z "$line" ]; then
		# Inlined into its IRAM caller (no out-of-line symbol) or not
		# linked into this image -- both safe. Report, don't fail.
		echo "skip  $sym (no out-of-line symbol -- inlined or not linked)"
	elif [[ "$line" == *".iram0.text"* ]]; then
		echo "ok    $sym -> .iram0.text"
	else
		sec="$(printf '%s\n' "$line" | awk '{print $(NF-1)}')"
		echo "FAIL  $sym -> ${sec} (expected .iram0.text; flash-resident would cache-fault from an IRAM ISR)"
		fail=1
	fi
done <<<"$SYMBOLS"

echo "---"
echo "checked $checked K_ISR_SAFE symbol(s) in $(basename "$ELF")"
exit "$fail"
