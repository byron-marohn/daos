/**
 * (C) Copyright 2016-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_io.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include <vos_internal.h>

#if 0
static int
ilog_tst_teardown(void **state)
{
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}
#endif

static struct vos_ilog_df *
ilog_alloc_root(struct vos_pool *pool)
{
	int		 rc = 0;
	umem_off_t	 ilog_off = UMOFF_NULL;

	rc = vos_tx_begin(pool);
	if (rc != 0) {
		print_message("Tx begin failed\n");
		goto done;
	}

	ilog_off = umem_alloc(&pool->vp_umm, sizeof(struct vos_ilog_df));
	if (ilog_off == UMOFF_NULL) {
		print_message("Allocation failed\n");
		rc = -DER_NOMEM;
	}

	rc = vos_tx_end(pool, rc);
done:
	assert_int_equal(rc, 0);

	return umem_off2ptr(&pool->vp_umm, ilog_off);
}

static void
ilog_test_upsert(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct vos_ilog_df	*ilog;
	daos_handle_t		 loh;
	int			 rc;
	int			 idx;
	daos_epoch_t		 epoch;

	pool = vos_hdl2pool(args->ctx.tc_po_hdl);

	ilog = ilog_alloc_root(pool);

	rc = vos_ilog_create(pool, ilog, &loh);
	if (rc != 0) {
		print_message("Failed to create a new incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	rc = vos_ilog_upsert(loh, 1, 1, 1, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	/* Test upgrade to punch in root */
	rc = vos_ilog_upsert(loh, 1, 1, 1, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	/** Same epoch, different DTX */
	rc = vos_ilog_upsert(loh, 1, 1, 2, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	/** New epoch, creation */
	rc = vos_ilog_upsert(loh, 1, 2, 2, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	/** New epoch, updgrade to punch */
	rc = vos_ilog_upsert(loh, 1, 2, 2, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	epoch = 3;
	for (idx = 0; idx < 1000; idx++) {
		rc = vos_ilog_upsert(loh, 1, epoch++, 2, idx & 1);
		if (rc != 0) {
			print_message("Failed to insert log entry: %s\n",
				      d_errstr(rc));
			assert(0);
		}
	}
}

static const struct CMUnitTest inc_tests[] = {
	{ "VOS500.1: VOS incarnation log test", ilog_test_upsert, NULL, NULL},
};

int
run_ilog_tests(void)
{
	return cmocka_run_group_tests_name("VOS Incarnation log tests",
					   inc_tests, setup_io, teardown_io);
}
