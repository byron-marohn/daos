/*
 * (C) Copyright 2018 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __DAOS_AGENT_H__
#define __DAOS_AGENT_H__

#include <daos/drpc.h>

/**
 *  Called during libray initialization to craft socket path for agent.
 */
int dc_agent_init(void);

/**
 *  Called during library finalization to free allocated agent resources
 */
void dc_agent_fini(void);

/**
 * Path to be used to communicate with the DAOS Agent set at library init.
 */
extern char *dc_agent_sockpath;

/**
 * Default runtime directory for daos_agent
 */
#define DAOS_AGENT_DRPC_DIR "/var/run/daos_agent/"

/**
 * Environment variable for specifying an alternate dRPC socket path
 */
#define DAOS_AGENT_DRPC_DIR_ENV "DAOS_AGENT_DRPC_DIR"

/**
 * Socket name used to craft path from environment variable
 */
#define DAOS_AGENT_DRPC_SOCK_NAME "agent.sock"

/**
 * Default Unix Domain Socket path for the DAOS agent dRPC connection
 */
#define DEFAULT_DAOS_AGENT_DRPC_SOCK	(DAOS_AGENT_DRPC_DIR \
					DAOS_AGENT_DRPC_SOCK_NAME)

/**
 * dRPC definitions for DAOS agent.
 */

/**
 *  Module: Security Agent
 *
 *  The agent module that deals with client security requests.
 */

/**
 * Method: Request Credentials
 *
 * Requests authentication credentials for the current user.
 */
#define DRPC_METHOD_SECURITY_AGENT_REQUEST_CREDENTIALS		101

#endif /* __DAOS_AGENT_H__ */
