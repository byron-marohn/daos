#include "bench.h"

void
lookup_ht(struct umem_instance *umm, uint64_t key, struct d_hash_table *ht,
	  uint64_t *value)
{
	d_list_t	*rlink;
	struct entry	*entry;

	rlink = d_hash_rec_find(ht, &key, sizeof(key));

	entry = d_list_entry(rlink, struct entry, link);

	*value = entry->payload;
}

void
lookup_simple(struct umem_instance *umm, uint64_t key,
	      struct entry *simple_hash, uint64_t *value)
{
	struct entry	*entry;
	unsigned int	idx;

	idx = hash(key) & hash_mask;
	entry = &simple_hash[idx];

	if (entry->ts == key) {
		*value = entry->payload;
		return;
	}

	d_list_for_each_entry(entry, &simple_hash[idx].link, link) {
		if (entry->ts == key) {
			*value = entry->payload;
			return;
		}
	}
	D_ASSERT(0);
}

void
lookup_deref(struct umem_instance *umm, umem_off_t ptr, uint64_t *value)
{
	umem_off_t	offset;

	offset = *(umem_off_t *)umem_off2ptr(umm, ptr);

	*value = *(uint64_t *)umem_off2ptr(umm, offset);
}
