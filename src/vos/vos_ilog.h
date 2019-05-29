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
 * VOS Object/Key incarnation log
 * vos/vos_ilog.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_ILOG_H__
#define __VOS_ILOG_H__

struct vos_ilog_df {
	/** Timestamp of embedded log entry. */
	uint64_t		lr_ts;
	/** log entry is a punch */
	bool			lr_is_punch;
	/** Pool map version of log entry */
	uint32_t		lr_ver;
	union {
		/** DTX of embedded log entry if timestamp is not 0 */
		umem_off_t	lr_dtx;
		/** offset of allocated log if timestamp is 0 */
		umem_off_t	lr_ilog;
	};
	/** For sanity check, also padding to 32 bytes */
	uint64_t		lr_magic;
};

struct vos_pool;

int vos_ilog_init(void);
int vos_ilog_create(struct vos_pool *pool, struct vos_ilog_df *root,
		    daos_handle_t *loh);
int vos_ilog_open(struct vos_pool *pool, struct vos_ilog_df *root,
		  daos_handle_t *loh);
int vos_ilog_close(daos_handle_t loh);
int vos_ilog_upsert(daos_handle_t loh, uint32_t pm_ver, daos_epoch_t epoch,
		    umem_off_t dtx, bool is_punch);

#endif /* __VOS_ILOG_H__ */
