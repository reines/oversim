//
// Copyright (C) 2007 Institut fuer Telematik, Universitaet Karlsruhe (TH)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

/**
 * @file RepeatedHashing.cc
 * @author Jamie Furness
 */

#include <IPAddressResolver.h>

#include "RepeatedHashingDHT.h"

#include <RpcMacros.h>
#include <BaseRpc.h>
#include <GlobalStatistics.h>
#include <GlobalBroadcastAccess.h>

Define_Module(RepeatedHashingDHT);

using namespace std;

void RepeatedHashingDHT::initializeDHT()
{
	numReplicaTeams = par("numReplicaTeams");
	if (numReplica % numReplicaTeams != 0)
		throw new cRuntimeError("RepeatedHashingDHT::initializeDHT(): numReplica must be a multiple of numReplicaTeams.");

	numReplica /= numReplicaTeams;
}

void RepeatedHashingDHT::handlePutCAPIRequest(DHTputCAPICall* capiPutMsg)
{
	OverlayKey key = capiPutMsg->getKey();

	for (int i = 0;i < numReplicaTeams;i++) {
		// Copy the original put request, but change its key
		DHTputCAPICall* copyPutMsg = capiPutMsg->dup();
		copyPutMsg->setKey(key);

		// Copy the control info
		copyPutMsg->setControlInfo(new OverlayCtrlInfo(*((OverlayCtrlInfo*) capiPutMsg->getControlInfo())));

		// Generate an unused rpcId
		uint32_t rpcId;
		do {
			rpcId = intuniform(1, 2147483647);
		} while (pendingRpcs.find(rpcId) != pendingRpcs.end());

		// Send the lookup message
		sendPutLookupCall(copyPutMsg, rpcId);

		// hash the key again
		key = OverlayKey::sha1(BinaryValue(key.toString(16).c_str()));
	}

	delete capiPutMsg;
}

void RepeatedHashingDHT::handleGetCAPIRequest(DHTgetCAPICall* capiGetMsg)
{
	OverlayKey key = capiGetMsg->getKey();

	for (int i = 0;i < numReplicaTeams;i++) {
		// Copy the original get request, but change its key
		DHTgetCAPICall* copyGetMsg = capiGetMsg->dup();
		copyGetMsg->setKey(key);

		// Copy the control info
		copyGetMsg->setControlInfo(new OverlayCtrlInfo(*((OverlayCtrlInfo*) capiGetMsg->getControlInfo())));

		// Generate an unused rpcId
		uint32_t rpcId;
		do {
			rpcId = intuniform(1, 2147483647);
		} while (pendingRpcs.find(rpcId) != pendingRpcs.end());

		// Send the lookup message
		sendGetLookupCall(copyGetMsg, rpcId);

		// hash the key again
		key = OverlayKey::sha1(BinaryValue(key.toString(16).c_str()));
	}

	delete capiGetMsg;
}
