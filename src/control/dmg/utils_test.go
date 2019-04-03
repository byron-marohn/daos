//
// (C) Copyright 2018 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"errors"
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
)

var (
	addresses  = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features   = []*pb.Feature{MockFeaturePB()}
	ctrlrs     = NvmeControllers{MockControllerPB("")}
	modules    = ScmModules{MockModulePB()}
	exampleErr = errors.New("something went wrong")
)

func init() {
	log.NewDefaultLogger(log.Error, "utils_test: ", os.Stderr)
}

func TestHasConnection(t *testing.T) {
	var shelltests = []struct {
		addrs Addresses
		eMap  ResultMap
		out   string
	}{
		{
			addresses,
			ResultMap{},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			Addresses{"1.2.3.5:10001"},
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, exampleErr}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			Addresses{},
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, exampleErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, exampleErr}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nfailed to connect to 1.2.3.5:10001 (something went wrong)\nActive connections: []\nNo active connections!",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, hasConns(tt.addrs, tt.eMap), tt.out, "bad output")
	}
}

func TestSprintConns(t *testing.T) {
	var shelltests = []struct {
		addrs Addresses
		eMap  ResultMap
		out   string
	}{
		{
			addresses,
			ResultMap{},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			Addresses{"1.2.3.5:10001"},
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, exampleErr}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			Addresses{},
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, exampleErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, exampleErr}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nfailed to connect to 1.2.3.5:10001 (something went wrong)\nActive connections: []\n",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, sprintConns(tt.addrs, tt.eMap), tt.out, "bad output")
	}
}

func marshal(i interface{}) string {
	s, _ := StructsToString(i)
	return s
}

func TestCheckSprint(t *testing.T) {
	var shelltests = []struct {
		m   interface{}
		out string
	}{
		{
			NewClientFM(features, addresses),
			"Listing %[1]ss on connected storage servers:\n1.2.3.4:10000:\n  burn-name: category nvme, run workloads on device to test\n1.2.3.5:10001:\n  burn-name: category nvme, run workloads on device to test\n\n\n",
		},
		{
			NewClientNvme(ctrlrs, addresses),
			"Listing %[1]ss on connected storage servers:\n1.2.3.4:10000:\n- id: 12345\n  model: ABC\n  serial: 123ABC\n  pciaddr: \"1:2:3.0\"\n  fwrev: \"\"\n  namespace:\n  - id: 12345\n    capacity: 99999\n1.2.3.5:10001:\n- id: 12345\n  model: ABC\n  serial: 123ABC\n  pciaddr: \"1:2:3.0\"\n  fwrev: \"\"\n  namespace:\n  - id: 12345\n    capacity: 99999\n\n\n",
		},
		{
			NewClientScm(modules, addresses),
			"Listing %[1]ss on connected storage servers:\n1.2.3.4:10000:\n- physicalid: 12345\n  channel: 1\n  channelpos: 2\n  memctrlr: 3\n  socket: 4\n  capacity: 12345\n1.2.3.5:10001:\n- physicalid: 12345\n  channel: 1\n  channelpos: 2\n  memctrlr: 3\n  socket: 4\n  capacity: 12345\n\n\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, exampleErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, exampleErr}},
			"Listing %[1]ss on connected storage servers:\n1.2.3.4:10000: something went wrong\n1.2.3.5:10001: something went wrong\n\n\n",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, unpackFormat(tt.m), tt.out, "bad output")
	}
}