/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos two-phase commit transaction.
 *
 * vos/vos_dtx_iter.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include "vos_layout.h"
#include "vos_internal.h"

/** Iterator for active-DTX table. */
struct vos_dtx_iter {
	/** embedded VOS common iterator */
	struct vos_iterator	 oit_iter;
	/** Handle of iterator */
	daos_handle_t		 oit_hdl;
	/** Reference to the container */
	struct vos_container	*oit_cont;
};

static struct vos_dtx_iter *
iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_dtx_iter, oit_iter);
}

static int
dtx_iter_fini(struct vos_iterator *iter)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	int			 rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	if (!daos_handle_is_inval(oiter->oit_hdl)) {
		rc = dbtree_iter_finish(oiter->oit_hdl);
		if (rc != 0)
			D_ERROR("oid_iter_fini failed: rc = %d\n", rc);
	}

	if (oiter->oit_cont != NULL)
		vos_cont_decref(oiter->oit_cont);

	D_FREE_PTR(oiter);
	return rc;
}

static int
dtx_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	      struct vos_iterator **iter_pp)
{
	struct vos_dtx_iter	*oiter;
	struct vos_container	*cont;
	int			 rc;

	if (type != VOS_ITER_DTX) {
		D_ERROR("Expected Type: %d, got %d\n", VOS_ITER_DTX, type);
		return -DER_INVAL;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->oit_iter.it_type = type;
	oiter->oit_cont = cont;
	vos_cont_addref(cont);

	rc = dbtree_iter_prepare(cont->vc_dtx_active_hdl, 0, &oiter->oit_hdl);
	if (rc != 0) {
		D_ERROR("Failed to prepare DTX iteration: rc = %d\n", rc);
		dtx_iter_fini(&oiter->oit_iter);
	} else {
		*iter_pp = &oiter->oit_iter;
	}

	return rc;
}

static int
dtx_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	dbtree_probe_opc_t	 opc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	return dbtree_iter_probe(oiter->oit_hdl, opc, vos_iter_intent(iter),
				 NULL, anchor);
}

static int
dtx_iter_next(struct vos_iterator *iter)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	return dbtree_iter_next(oiter->oit_hdl);
}

static int
dtx_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	       daos_anchor_t *anchor)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	struct vos_dtx_entry_df	*dtx;
	d_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching DTX info: rc = %d\n", rc);
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_dtx_entry_df));
	dtx = (struct vos_dtx_entry_df *)rec_iov.iov_buf;

	it_entry->ie_xid = dtx->te_xid;
	it_entry->ie_oid = dtx->te_oid;
	it_entry->ie_dtx_sec = dtx->te_sec;
	it_entry->ie_dtx_intent = dtx->te_intent;
	it_entry->ie_dtx_hash = dtx->te_dkey_hash;

	D_DEBUG(DB_TRACE, "DTX iterator fetch the one "DF_DTI"\n",
		DP_DTI(&dtx->te_xid));

	return 0;
}

static int
dtx_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	struct umem_instance	*umm;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	umm = &oiter->oit_cont->vc_pool->vp_umm;
	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc != 0)
		return rc;

	rc = dbtree_iter_delete(oiter->oit_hdl, args);
	if (rc != 0) {
		umem_tx_abort(umm, rc);
		D_ERROR("Failed to delete DTX entry: rc = %d\n", rc);
	} else {
		umem_tx_commit(umm);
	}

	return rc;
}

struct vos_iter_ops vos_dtx_iter_ops = {
	.iop_prepare =	dtx_iter_prep,
	.iop_finish  =  dtx_iter_fini,
	.iop_probe   =	dtx_iter_probe,
	.iop_next    =  dtx_iter_next,
	.iop_fetch   =  dtx_iter_fetch,
	.iop_delete  =	dtx_iter_delete,
};
