#define D_LOGFAC DD_FAC(vos)
#include <daos_srv/vos.h>
#include <daos/btree.h>
#include "vos_internal.h"
#include "vos_layout.h"
#include "vos_ilog.h"

#define ILOG_TREE_ORDER 11
/** The ilog is split into two parts.   If there is one entry, the ilog
 *  is embedded into the root df struct.   If not, a b+tree is used.
 *  The tree is used more like a set where only the key is used.
 */

/** This is the non-hashed, more user readable key */
struct ilog_key {
	/** The timestamp of the entry */
	daos_epoch_t	ik_epoch;
	/** The dtx entry */
	umem_off_t	ik_dtx;
};

struct ilog_val {
	/** record is a punch or not */
	bool		iv_is_punch;
	/** pool map version */
	uint32_t	iv_ver;
};

/** We hijack the value offset to store the actual value inline */
D_CASSERT(sizeof(struct ilog_val) == sizeof(((struct btr_record *)0)->rec_off));

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
ilog_hkey_size(void)
{
	return sizeof(struct ilog_key);
}

static int
ilog_rec_msize(int alloc_overhead)
{
	/** No extra allocation for ilog entries */
	return 0;
}

/** generate hkey */
static void
ilog_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct ilog_key));
	memcpy(hkey, key_iov->iov_buf, sizeof(struct ilog_key));
}

/** compare the hashed key */
static int
ilog_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct ilog_key	*k1 = (struct ilog_key *)&rec->rec_hkey[0];
	struct ilog_key	*k2 = (struct ilog_key *)hkey;
	umem_off_t		 off1;
	umem_off_t		 off2;

	if (k1->ik_epoch < k2->ik_epoch)
		return BTR_CMP_LT;

	if (k1->ik_epoch > k2->ik_epoch)
		return BTR_CMP_GT;

	/** Note that a punch and a regular update in the same DTX match */
	off1 = umem_off2offset(k1->ik_dtx);
	off2 = umem_off2offset(k2->ik_dtx);

	if (off1 < off2)
		return BTR_CMP_LT;

	if (off1 > off2)
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

/** create a new key-record, or install an externally allocated key-record */
static int
ilog_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	      d_iov_t *val_iov, struct btr_record *rec)
{
	D_ASSERT(val_iov->iov_len == sizeof(struct ilog_val));
	/** Note the D_CASSERT below the definition of ilog_val ensures
	 *  that rec_off is large enough to fit the value without allocating
	 *  new memory.
	 */
	memcpy(&rec->rec_off, val_iov->iov_buf, val_iov->iov_len);

	return 0;
}

static int
ilog_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	return 0;
}

static int
ilog_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct ilog_key	*key = (struct ilog_key *)&rec->rec_hkey[0];
	struct ilog_val	*val = (struct ilog_val *)&rec->rec_off;

	if (key_iov != NULL) {
		if (key_iov->iov_buf == NULL) {
			d_iov_set(key_iov, key, sizeof(*key));
		} else {
			D_ASSERT(sizeof(*key) <= key_iov->iov_buf_len);
			memcpy(key_iov->iov_buf, key, sizeof(*key));
			key_iov->iov_len = sizeof(*key);
		}
	}
	if (val_iov != NULL) {
		if (val_iov->iov_buf == NULL) {
			d_iov_set(val_iov, val, sizeof(*val));
		} else {
			D_ASSERT(sizeof(*val) <= val_iov->iov_buf_len);
			memcpy(val_iov->iov_buf, val, sizeof(*val));
			val_iov->iov_len = sizeof(*val);
		}
	}
	return 0;
}

static int
ilog_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct ilog_val		*new_val = val_iov->iov_buf;
	struct ilog_val		*old_val = (struct ilog_val *)&rec->rec_off;
	int			 rc = 0;

	if (new_val->iv_is_punch <= old_val->iv_is_punch)
		return 0;

	/* The new one is a punch so promote the in-tree entry */
	rc = umem_tx_add_ptr(&tins->ti_umm, &rec->rec_off,
			     sizeof(rec->rec_off));
	if (rc != 0)
		goto done;

	old_val->iv_is_punch = true;
done:
	return rc;
}

static btr_ops_t ilog_btr_ops = {
	.to_rec_msize		= ilog_rec_msize,
	.to_hkey_size		= ilog_hkey_size,
	.to_hkey_gen		= ilog_hkey_gen,
	.to_hkey_cmp		= ilog_hkey_cmp,
	.to_rec_alloc		= ilog_rec_alloc,
	.to_rec_free		= ilog_rec_free,
	.to_rec_fetch		= ilog_rec_fetch,
	.to_rec_update		= ilog_rec_update,
};

int
vos_ilog_init(void)
{
	int	rc;

	rc = dbtree_class_register(VOS_BTR_ILOG, 0, &ilog_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register incarnation log btree class: %s\n",
			d_errstr(rc));

	return rc;
}

enum {
	ILOG_STATE_NONE,
	ILOG_STATE_INIT,
	ILOG_STATE_READY,
	ILOG_STATE_FINI,
};

struct ilog_iterator {
	int32_t		li_idx;
	uint32_t	li_state;
	daos_handle_t	li_toh;
};

struct ilog_context {
	/** Root pointer */
	struct vos_ilog_df		*ic_root;
	/** pool instance */
	struct vos_pool			*ic_pool;
	/** embedded iterator */
	struct ilog_iterator		 ic_iter;
	/** ref count for iterator */
	uint32_t			 ic_ref;
	/** padding */
	uint32_t			 ic_pad;
};

static inline bool
ilog_empty(struct vos_ilog_df *root)
{
	return root->lr_ts == 0 && root->lr_dtx == UMOFF_NULL;
}

#define ilog_tx_add_ptr(pool, ptr, len)	\
	umem_tx_add_ptr(&(pool)->vp_umm, ptr, len)

#define ILOG_MAGIC 0xdeadbaadbeeff00d
static void
ilog_addref(struct ilog_context *lctx)
{
	lctx->ic_ref++;
}

static void
ilog_decref(struct ilog_context *lctx)
{
	lctx->ic_ref--;
	if (lctx->ic_ref == 0)
		D_FREE(lctx);
}

static int
ilog_ctx_create(struct vos_pool *pool, struct vos_ilog_df *root,
		    struct ilog_context **lctxp)
{
	D_ALLOC_PTR(*lctxp);
	if (*lctxp == NULL) {
		D_ERROR("Could not allocate memory for open incarnation log\n");
		return -DER_NOMEM;
	}

	(*lctxp)->ic_root = root;
	(*lctxp)->ic_pool = pool;
	ilog_addref(*lctxp);
	return 0;
}

static daos_handle_t
ilog_lctx2hdl(struct ilog_context *lctx)
{
	daos_handle_t	hdl;

	hdl.cookie = (uint64_t)lctx;

	return hdl;
}

static struct ilog_context *
ilog_hdl2lctx(daos_handle_t hdl)
{
	struct ilog_context	*lctx;

	if (daos_handle_is_inval(hdl))
		return NULL;

	lctx = (struct ilog_context *)hdl.cookie;

	if (lctx->ic_root->lr_magic != ILOG_MAGIC)
		return NULL;

	return lctx;
}

static int
ilog_ptr_set_full(struct vos_pool *pool, void *dest, const void *src,
		  size_t len)
{
	int	rc = 0;

	D_ASSERT(pool != NULL);

	rc = vos_tx_begin(pool);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = ilog_tx_add_ptr(pool, dest, len);
	if (rc != 0) {
		D_ERROR("Failed to add to undo log\n");
		goto end;
	}

	memcpy(dest, src, len);
end:
	rc = vos_tx_end(pool, rc);
done:
	return rc;
}

#define ilog_ptr_set(pool, dest, src)	\
	ilog_ptr_set_full(pool, dest, src, sizeof(*(src)))

int
vos_ilog_create(struct vos_pool *pool, struct vos_ilog_df *root,
		daos_handle_t *loh)
{
	struct ilog_context	*lctx;
	struct vos_ilog_df	 tmp = {0};
	int			 rc = 0;

	tmp.lr_magic = ILOG_MAGIC;
	rc = ilog_ptr_set(pool, root, &tmp);
	if (rc != 0)
		goto done;

	rc = ilog_ctx_create(pool, root, &lctx);
	if (rc != 0)
		goto done;

	*loh = ilog_lctx2hdl(lctx);
done:
	return rc;
}

int
vos_ilog_open(struct vos_pool *pool, struct vos_ilog_df *root,
	      daos_handle_t *loh)
{
	struct ilog_context	*lctx;
	int			 rc;

	if (root->lr_magic != ILOG_MAGIC) {
		D_ERROR("Could not open uninitialized incarnation log\n");
		return -DER_INVAL;
	}

	rc = ilog_ctx_create(pool, root, &lctx);
	if (rc != 0)
		return rc;

	*loh = ilog_lctx2hdl(lctx);

	return 0;
}

int
vos_ilog_close(daos_handle_t loh)
{
	struct ilog_context *lctx = ilog_hdl2lctx(loh);

	if (lctx == NULL) {
		D_ERROR("Invalid incarnation log handle\n");
		return -DER_INVAL;
	}

	ilog_decref(lctx);

	return 0;
}

/*
int vos_ilog_iter_prepare(daos_handle_t loh, daos_handle_t *lih)
{
	struct vos_ilog_df	*root;
	struct ilog_iterator	*iter;
	struct ilog_context	*lctx;

	lctx = ilog_hdl2lctx(loh);
	if (lctx == NULL) {
		D_ERROR("Invalid log handle\n");
		return -DER_INVAL;
	}

	root = lctx->ic_root;
	iter = &lctx->ic_iter;

	if (iter->ic_state != ILOG_ITER_NONE) {
		D_ERROR("Iterator already in use\n");
		return -DER_INVAL;
	}

	if (root->lr_ts != 0) {
		lctx->ic_state = ILOG_ITER_INIT;
		lctx->ic_idx = -1;
	}

	return -DER_NOTSUP;
}
*/

static int
ilog_root_migrate(struct ilog_context *lctx, uint32_t pm_ver,
		  daos_epoch_t epoch, umem_off_t dtx, bool is_punch)
{
	struct vos_ilog_df	*root;
	struct vos_ilog_df	 tmp;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	struct ilog_key		 key;
	struct ilog_val		 val;
	umem_off_t		 tree_root;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 rc = 0;

	root = lctx->ic_root;
	tmp = *root;

	/** If any of these three conditions fail, we will insert a new entry
	 *  1. The existing entry is committed
	 *  2. The existing entry has a lower timestamp
	 *  3. The existing entry is the same type
	 */
	if (epoch > root->lr_ts &&
	    root->lr_is_punch == is_punch)
		return 0;

	rc = vos_tx_begin(lctx->ic_pool);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = dbtree_create(VOS_BTR_ILOG, 0, ILOG_TREE_ORDER,
			   &lctx->ic_pool->vp_uma, &tree_root, &toh);
	if (rc != 0) {
		D_ERROR("Failed to create an incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto end;
	}

	d_iov_set(&key_iov, &key, sizeof(key));
	d_iov_set(&val_iov, &val, sizeof(val));

	key.ik_epoch = root->lr_ts;
	key.ik_dtx = root->lr_dtx;
	val.iv_is_punch = root->lr_is_punch;
	val.iv_ver = root->lr_ver;

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto end;
	}

	key.ik_epoch = epoch;
	key.ik_dtx = dtx;
	val.iv_is_punch = is_punch;
	val.iv_ver = pm_ver;

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto end;
	}

	tmp.lr_ts = 0;
	tmp.lr_ilog = tree_root;
	tmp.lr_is_punch = false;
	tmp.lr_ver = 0;
	tmp.lr_magic = ILOG_MAGIC;

	rc = ilog_ptr_set(lctx->ic_pool, root, &tmp);
end:
	rc = vos_tx_end(lctx->ic_pool, rc);
done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	return rc;
}

static int
ilog_tree_upsert(struct ilog_context *lctx, uint32_t pm_ver, daos_epoch_t epoch,
		 umem_off_t dtx, bool is_punch)
{
	struct vos_ilog_df	*root;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	struct ilog_key		 key;
	struct ilog_val		 val;
	int			 rc;

	root = lctx->ic_root;

	rc = dbtree_open(root->lr_ilog, &lctx->ic_pool->vp_uma, &toh);
	if (rc != 0) {
		D_ERROR("Failed to open incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	d_iov_set(&key_iov, &key, sizeof(key));
	d_iov_set(&val_iov, &val, sizeof(val));

	key.ik_epoch = epoch;
	key.ik_dtx = dtx;
	val.iv_is_punch = is_punch;
	val.iv_ver = pm_ver;

	rc = dbtree_upsert(toh, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &key_iov,
			   &val_iov);
	if (rc) {
		D_ERROR("Failed to update incarnation log: rc = %s\n",
			d_errstr(rc));
		goto done;
	}
done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	return 0;
}

int
vos_ilog_upsert(daos_handle_t loh, uint32_t pm_ver, daos_epoch_t epoch,
		umem_off_t dtx, bool is_punch)
{
	struct ilog_context	*lctx;
	struct vos_ilog_df	*root;
	struct vos_ilog_df	 tmp = {0};
	int			 rc = 0;

	lctx = ilog_hdl2lctx(loh);
	if (lctx == NULL) {
		D_ERROR("Invalid log handle\n");
		return -DER_INVAL;
	}

	root = lctx->ic_root;

	if (ilog_empty(root)) {
		tmp.lr_magic = ILOG_MAGIC;
		tmp.lr_ts = epoch;
		tmp.lr_dtx = dtx;
		tmp.lr_is_punch = is_punch;
		tmp.lr_ver = pm_ver;
		rc = ilog_ptr_set(lctx->ic_pool, root, &tmp);
	} else if (root->lr_ts == epoch) {
		if (root->lr_dtx == dtx && root->lr_is_punch && !is_punch)
			goto done;
		tmp.lr_is_punch = is_punch;
		rc = ilog_ptr_set(lctx->ic_pool, &root->lr_is_punch,
				  &tmp.lr_is_punch);
	} else if (root->lr_ts != 0) {
		rc = ilog_root_migrate(lctx, pm_ver, epoch, dtx, is_punch);
	} else {
		rc = ilog_tree_upsert(lctx, pm_ver, epoch, dtx, is_punch);
	}
done:
	return rc;
}
