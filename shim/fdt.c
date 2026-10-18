/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

/* Walk to the /memory child and parse its reg property into base/size pairs.
 * The shipped DTB has this node zeroed; ABL patches it in the RAM copy it
 * hands to us, so a non-empty result here is the *live* layout. */
int fdt_memory_banks(uintptr_t fdt, uint64_t *out, int max)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_mem = 0, ac = 2, sc = 2, n = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_mem = depth == 2 && streq(name, "memory");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_mem)
				return n;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			const char *prop = (const char *)(strings + nameoff);
			if (in_mem && streq(prop, "reg") && ac && sc) {
				uint32_t bytes = (uint32_t)(ac + sc) * 4;
				for (uint32_t off = 0; off + bytes <= len && n < max; off += bytes) {
					uint64_t base = 0, size = 0;
					for (int i = 0; i < ac; i++)
						base = base << 32 | be32(val + off + (uint32_t)i * 4);
					for (int i = 0; i < sc; i++)
						size = size << 32 | be32(val + off + (uint32_t)(ac + i) * 4);
					out[2 * n] = base;
					out[2 * n + 1] = size;
					n++;
				}
			} else if (depth == 1 && streq(prop, "#address-cells")) {
				ac = len == 4 ? (int)be32(val) : 2;
			} else if (depth == 1 && streq(prop, "#size-cells")) {
				sc = len == 4 ? (int)be32(val) : 2;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return n;
		}
	}
}
