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
# Usage: tools/check_iram_symbols.sh <firmware.elf>
#   OBJDUMP=<triple>-objdump may be set to override toolchain autodetect.

set -euo pipefail

ELF="${1:?usage: check_iram_symbols.sh <firmware.elf>}"

# Symbols that MUST be IRAM-resident: each carries K_ISR_SAFE and sits on
# a call path reachable from an IRAM ISR while the flash cache may be off.
# Keep in sync with the K_ISR_SAFE definitions in components/*/src.
REQUIRED_IRAM=(
	# k_sem.c
	z_sem_pop_waiter
	k_sem_give
	k_sem_take
	# k_event.c
	z_event_match
	z_event_post_internal
	z_event_wait_internal
	# k_work.c
	k_work_submit_internal
	k_work_submit_to_queue
	# k_msgq.c
	k_msgq_put
)

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
for sym in "${REQUIRED_IRAM[@]}"; do
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
done

exit "$fail"
