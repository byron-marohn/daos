#define D_LOGFAC DD_FAC(tests)

#include <utest_common.h>

#define NUM_ENTRIES 512
#define NUM_ITER 100000000

struct entry {
	d_list_t	link;
	uint64_t	ts;
	uint64_t	payload;
};

struct value {
	umem_off_t	ptr2ptr;
	uint64_t	value;
};

#define HASH_BITS 14
#define HASH_SIZE (1 << hash_bits)
extern int hash_mask;

#define POOL_NAME "/mnt/daos/test/hash_bench"

static inline uint32_t
hash(uint64_t key)
{
	return (uint32_t)(key >> 13) ^ ((uint32_t)key & 65535);
}


void lookup_ht(struct umem_instance *umm, uint64_t key, struct d_hash_table *ht,
	       uint64_t *value);

void lookup_simple(struct umem_instance *umm, uint64_t key,
		   struct entry *simple_hash, uint64_t *value);

void lookup_deref(struct umem_instance *umm, umem_off_t ptr, uint64_t *value);
