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
 * @file Symmetric.cc
 * @author Jamie Furness
 */

#include <IPAddressResolver.h>

#include "SymmetricDHT.h"

#include <RpcMacros.h>
#include <BaseRpc.h>
#include <GlobalStatistics.h>
#include <GlobalBroadcastAccess.h>

Define_Module(SymmetricDHT);

using namespace std;

void SymmetricDHT::initializeDHT()
{
	numReplicaTeams = par("numReplicaTeams");
	if (numReplicaTeams != 0 && (numReplica % numReplicaTeams != 0))
		throw new cRuntimeError("RepeatedHashingDHT::initializeDHT(): numReplica must be a multiple of numReplicaTeams.");

	// Hack for ease of configuration with multiple runs
	if (numReplicaTeams == 0) {
		numReplicaTeams = numReplica;
		numReplica = 1;
	}
	else {
		// Divide the number of replica among teams
		numReplica /= numReplicaTeams;
	}

	overlayKeyOffset = OverlayKey::getMax() / numReplicaTeams;
}

void SymmetricDHT::handlePutCAPIRequest(DHTputCAPICall* capiPutMsg)
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
		key += overlayKeyOffset;
	}

	delete capiPutMsg;
}

void SymmetricDHT::handleGetCAPIRequest(DHTgetCAPICall* capiGetMsg)
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
		key += overlayKeyOffset;
	}

	delete capiGetMsg;
}
