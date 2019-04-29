#include "bench.h"

static bool
ht_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	struct entry	*entry = d_list_entry(rlink, struct entry, link);

	return entry->ts == *(const uint64_t *)key;
};

static uint32_t
ht_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	return hash(*(uint64_t *)key);
}

static d_hash_table_ops_t	table_ops = {
	.hop_key_cmp	=	ht_key_cmp,
	.hop_key_hash	=	ht_key_hash,
};

void
set_ptr(void *ptr, size_t size, const void *src_mem)
{
	memcpy(ptr, src_mem, size);
}

int hash_bits = HASH_BITS;
int hash_mask;

int main(int argc, char **argv)
{
	struct d_hash_table	*ht;
	struct entry		*entries;
	uint64_t		*keys;
	struct value		*values;
	umem_off_t		*values_off;
	struct utest_context	*utx;
	struct umem_instance	*umm;
	struct timespec		 t1, t2;
	static int		 next_entry;
	unsigned int		 idx;
	int			 rc;
	int			 i;
	int			 collisions = 0;
	uint64_t		 total_time = 0;
	struct entry		*simple_hash;

	rc = d_log_init();
	d_log_sync_mask();

	if (argc > 1) {
		hash_bits = atoi(argv[1]);
		if (hash_bits > 25)
			hash_bits = 25;
		if (hash_bits == 0)
			hash_bits = HASH_BITS;
	}

	hash_mask = HASH_SIZE - 1;

	rc = utest_pmem_create(POOL_NAME, 1000000000, sizeof(int), &utx);
	if (rc != 0) {
		printf("Failed to create pmem pool: rc = %d\n", rc);
		return -1;
	}

	umm = utest_utx2umm(utx);

	rc = d_hash_table_create(D_HASH_FT_NOLOCK, 14, NULL, &table_ops, &ht);
	if (rc != 0) {
		printf("Failed to create hash table, rc = %d\n", rc);
		return rc;
	}

	D_ALLOC_ARRAY(keys, NUM_ENTRIES);
	if (keys == NULL) {
		printf("out of memory\n");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	D_ALLOC_ARRAY(values, NUM_ENTRIES);
	if (values == NULL) {
		printf("out of memory\n");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	D_ALLOC_ARRAY(entries, NUM_ENTRIES);
	if (entries == NULL) {
		printf("out of memory\n");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	D_ALLOC_ARRAY(simple_hash, HASH_SIZE);
	if (simple_hash == NULL) {
		printf("out of memory\n");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	D_ALLOC_ARRAY(values_off, NUM_ENTRIES);
	if (values_off == NULL) {
		printf("out of memory\n");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	for (i = 0; i < NUM_ENTRIES; i++) {
		rc = utest_alloc(utx, &values_off[i],
				 sizeof(uint64_t), set_ptr, &next_entry);
		next_entry++;
		if (rc != 0) {
			printf("out of memory\n");
			rc = -DER_NOMEM;
			goto cleanup;
		}
	}
	srand(time(NULL));

	for (i = 0; i < NUM_ENTRIES; i++) {
		struct entry *new_entry;

		keys[i] = entries[i].ts = crt_hlc_get();
		values[i].value = *(uint64_t *)utest_off2ptr(utx,
							     values_off[i]);
		entries[i].payload = values[i].value;
		rc = utest_alloc(utx, &values[i].ptr2ptr,
				 sizeof(umem_off_t), set_ptr, &values_off[i]);
		if (rc != 0) {
			printf("out of memory\n");
			rc = -DER_NOMEM;
			goto cleanup;
		}

		rc = d_hash_rec_insert(ht, &entries[i].ts,
				       sizeof(entries[i].ts),
				       &(entries[i].link), true);
		if (rc != 0) {
			printf("key is not unique: rc = %d\n", rc);
			rc = -DER_MISC;
			goto cleanup;
		}

		idx = hash(keys[i]) & hash_mask;
		if (simple_hash[idx].ts != 0) {
			D_ALLOC_PTR(new_entry);
			if (new_entry == NULL) {
				rc = -DER_NOMEM;
				goto cleanup;
			}
			collisions++;
			*new_entry = entries[i];
			d_list_add(&new_entry->link, &simple_hash[idx].link);
		} else {
			simple_hash[idx] = entries[i];
			D_INIT_LIST_HEAD(&simple_hash[idx].link);
		}
		usleep(rand() % 10000);

	}

	printf("simple hash has %d collisions\n", collisions);

	for (i = 0; i < NUM_ITER; i++) {
		uint64_t	 value;
		int		 item = i % NUM_ENTRIES;
		uint64_t	 key = keys[item];

		d_gettime(&t1);
		lookup_ht(umm, key, ht, &value);
		d_gettime(&t2);
		total_time += d_timediff_ns(&t1, &t2);

		D_ASSERT(values[item].value == value);
	}
	printf("hash table time = %8.2lfs\n", total_time / 1e9);
	total_time = 0;
	for (i = 0; i < NUM_ITER; i++) {
		uint64_t	 value;
		int		 item = i % NUM_ENTRIES;
		uint64_t	 key = keys[item];

		d_gettime(&t1);
		lookup_simple(umm, key, simple_hash, &value);
		d_gettime(&t2);
		total_time += d_timediff_ns(&t1, &t2);

		D_ASSERT(values[item].value == value);
	}
	printf("simple hash table time = %8.2lfs\n", total_time / 1e9);

	total_time = 0;

	for (i = 0; i < NUM_ITER; i++) {
		int		item = i % NUM_ENTRIES;
		uint64_t	value;
		uint64_t	*value_ptr;

		d_gettime(&t1);
		lookup_deref(umm, values[item].ptr2ptr, &value);
		d_gettime(&t2);
		total_time += d_timediff_ns(&t1, &t2);

		D_ASSERT(values[item].value == value);

		if ((i % 20) != 0)
			continue;

		/* Cause some cache misses on 5% of iterations */
		rc = utest_tx_begin(utx);
		umem_tx_add(umm, values_off[item], sizeof(uint64_t));
		value_ptr = umem_off2ptr(umm, values_off[item]);
		value = *value_ptr;
		*value_ptr = 0;
		rc = utest_tx_end(utx, rc);

		rc = utest_tx_begin(utx);
		umem_tx_add(umm, values_off[item], sizeof(uint64_t));
		value_ptr = umem_off2ptr(umm, values_off[item]);
		*value_ptr = value;
		rc = utest_tx_end(utx, rc);

	}
	printf("pointer chase time = %8.2lfs\n", total_time / 1e9);

cleanup:
	utest_utx_destroy(utx);

	d_log_fini();
}
