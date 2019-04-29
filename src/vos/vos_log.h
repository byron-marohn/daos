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
 * Generic log for storing VOS timestamps
 * vos/vos_log.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_LOG_H__
#define __VOS_LOG_H__

/** Opaque log root should be zero initialized to use the API */
struct vos_log_root_df {
	char		lr_data[sizeof(uint64_t) * 2];
};

struct vos_log_iter {
	char		lr_data[sizeof(uint64_t) * 2];
};

struct vos_pool;

struct vos_log_iter_entry {
	/** Timestamp of entry */
	uint64_t	le_time;
	/** DTX entry */
	umem_off_t	le_dte;
};

enum {
	VOS_LOG_PROBE_EQ,
	VOS_LOG_PROBE_LE,
	VOS_LOG_PROBE_LT,
	VOS_LOG_PROBE_GE,
	VOS_LOG_PROBE_GT,
};

int vos_log_upsert(struct vos_pool *pool, daos_epoch_t epoch, umem_off_t dte);

int vos_log_iter_prepare(struct vos_pool *pool, struct vos_log_root_df *root,
			 struct vos_log_iter *iter);
int vos_log_iter_probe(struct vos_pool *pool, struct vos_log_iter *iter,
		       daos_epoch_t epoch, int opc);
int vos_log_iter_fetch(struct vos_pool *pool, struct vos_log_iter *iter,
		       struct vos_log_iter_entry *entry);
int vos_log_iter_next(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_prev(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_delete(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_fini(struct vos_pool *pool, struct vos_log_iter *iter);

#endif /* __VOS_LOG_H__ */
