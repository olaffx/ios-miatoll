#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/* Number of live /memory banks (root #address-cells/#size-cells honoured,
 * 2/2 here). out[2*i] = base, out[2*i+1] = size; up to max banks. 0 if the
 * node is absent or still zeroed (ABL fills it at load, not in the image). */
int fdt_memory_banks(uintptr_t fdt, uint64_t *out, int max);
