//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "BroadcastTestApp.h"

#include <UnderlayConfiguratorAccess.h>
#include <GlobalNodeListAccess.h>
#include <NotificationBoard.h>
#include <GlobalStatisticsAccess.h>
#include <GlobalBroadcastAccess.h>
#include <RpcMacros.h>

Define_Module(BroadcastTestApp);

BroadcastTestApp::BroadcastTestApp()
{
	initTimer = NULL;
	bcastTimer = NULL;
}

BroadcastTestApp::~BroadcastTestApp()
{
	cancelAndDelete(initTimer);
	cancelAndDelete(bcastTimer);
}

void BroadcastTestApp::initializeApp(int stage)
{
	if (stage != MIN_STAGE_APP)
		return;

	// find friend modules
	underlayConfigurator = UnderlayConfiguratorAccess().get();
	globalStatistics = GlobalStatisticsAccess().get();
	notificationBoard = NotificationBoardAccess().get();
	globalNodeList = GlobalNodeListAccess().get();
	globalBroadcast = GlobalBroadcastAccess().get();

	// Find the DHT app
	storage = check_and_cast<DHTDataStorage*> (getParentModule()->getParentModule()->getSubmodule("tier1")->getSubmodule("dhtDataStorage"));

	ttl = par("ttl");
	perNode = par("itemsPerNode");

	// subscribe to the notification board
	notificationBoard->subscribe(this, NF_OVERLAY_BROADCAST_INIT);

	initTimer = new cMessage("initTimer");
	bcastTimer = new cMessage("bcastTimer");

	initFinished = 0;
	numMessages = 0;
	numBytes = 0;

	WATCH(numMessages);
	WATCH(numBytes);
}

void BroadcastTestApp::handleReadyMessage(CompReadyMessage* msg)
{
	if ((getThisCompType() - msg->getComp() == 2) && msg->getReady())
	{
		// Copy the bcastStarted data so we know what was started when we started
		for (SearchStat::iterator it = globalStatistics->bcastSearch.begin();it != globalStatistics->bcastSearch.end();it++)
			searches[it->first].started = !it->second.query.isUnspecified();

		scheduleAt(simTime(), initTimer);
	}

	delete msg;
}

void BroadcastTestApp::handleTimerEvent(cMessage *msg)
{
	if (msg == initTimer) {
		// do nothing if the network is still in the initialization phase
		if (underlayConfigurator->isInInitPhase()
				|| !underlayConfigurator->isTransitionTimeFinished()
				|| underlayConfigurator->isSimulationEndingSoon()) {
			scheduleAt(simTime() + 1, msg);
			return;
		}

		for (int i = 0;i < perNode;i++) {
			DHTputCAPICall* dhtPutMsg = new DHTputCAPICall();
			dhtPutMsg->setKey(OverlayKey::random());
			dhtPutMsg->setValue(globalBroadcast->getAvailableDataValue());
			dhtPutMsg->setTtl(0);
			dhtPutMsg->setIsModifiable(true);

			int nonce = sendInternalRpcCall(TIER1_COMP, dhtPutMsg);

			// Put the data in a pending map so we can recall it when we receive a response
			pendingData[nonce] = dhtPutMsg->getValue();
		}
	}
	else if (msg == bcastTimer) {
		if (initFinished >= perNode) {
			// Only start the broadcast if we have finished inserting data into the network.
			// Note: This doesn't confirm other nodes are finished inserting data, only our own...
			initBroadcast();
			return;
		}

		scheduleAt(simTime() + 1, msg);
	}
}

bool BroadcastTestApp::handleRpcCall(BaseCallMessage* msg)
{
	// delegate messages
	RPC_SWITCH_START( msg )
	// RPC_DELEGATE( <messageName>[Call|Response], <methodToCall> )
	RPC_DELEGATE( BroadcastRequest, rpcBroadcastRequest );
	RPC_DELEGATE( BroadcastResponse, rpcBroadcastResponse );
	RPC_SWITCH_END( )

	return RPC_HANDLED;
}

void BroadcastTestApp::handleRpcResponse(BaseResponseMessage* msg, cPolymorphic* context, int rpcId, simtime_t rtt)
{
	RPC_SWITCH_START(msg)
	RPC_ON_RESPONSE( DHTputCAPI ) {
		if (_DHTputCAPIResponse->getIsSuccess()) {
			BinaryValue* value = &pendingData[_DHTputCAPIResponse->getNonce()];
			if (value == NULL)
				throw new cRuntimeError("Received a response for unknown put request.");

			globalBroadcast->setUsedDataValue(value);
			pendingData.erase(_DHTputCAPIResponse->getNonce());
		}

		initFinished++;
		break;
	}
	RPC_ON_RESPONSE( BroadcastRequest ) {
		// ignore?
		break;
	}
	RPC_SWITCH_END( )
}

void BroadcastTestApp::handleRpcTimeout(BaseCallMessage* msg, const TransportAddress& dest, cPolymorphic* context, int rpcId, const OverlayKey& destKey)
{
	RPC_SWITCH_START(msg)
	RPC_ON_CALL( DHTputCAPI ) {
		// failed to put data, try another
		int nonce = _DHTputCAPICall->getNonce();

		// Remove the data from the pending set
		BinaryValue value = pendingData[nonce];
		pendingData.erase(nonce);

		// Create a new put call with the data item
		DHTputCAPICall* dhtPutMsg = new DHTputCAPICall();
		dhtPutMsg->setKey(OverlayKey::random());
		dhtPutMsg->setValue(value);
		dhtPutMsg->setTtl(0);
		dhtPutMsg->setIsModifiable(true);

		// Store the data back in the pending set with the new key
		nonce = sendInternalRpcCall(TIER1_COMP, dhtPutMsg);
		pendingData[nonce] = value;

		break;
	}
	RPC_ON_CALL( BroadcastRequest ) {
//		std::cout << "Broadcast['" << _BroadcastRequestCall->getQuery() << "'] timed out" << endl;
		// TODO: Resend this via another node?
		break;
	}
	RPC_SWITCH_END( )
}

void BroadcastTestApp::receiveChangeNotification(int category, const cPolymorphic *details)
{
	Enter_Method_Silent();
	if (category == NF_OVERLAY_BROADCAST_INIT) {
		cancelEvent(bcastTimer);
		scheduleAt(simTime(), bcastTimer);
	}
}

void BroadcastTestApp::initBroadcast()
{
	int id = globalBroadcast->bcastCurrentID++;

	globalStatistics->bcastSearch[id].query = globalBroadcast->getUsedDataValue();
	globalStatistics->bcastSearch[id].expected = globalNodeList->getNumNodes();

	BroadcastRequestCall* call = new BroadcastRequestCall("BroadcastRequestCall");
	call->setOrigin(overlay->getThisNode());
	call->setQuery(globalStatistics->bcastSearch[id].query);
	call->setRequestID(id);
	call->setTTL(ttl);
	call->setBitLength(BROADCASTREQUESTCALL_L(call));

//	std::cout << overlay->getThisNode().getKey() << ": Searching '" << globalStatistics->bcastSearch[id].query << "' (" << id << ") at " << simTime() << endl;

	sendInternalRpcCall(TIER2_COMP, call);
}

void BroadcastTestApp::rpcBroadcastRequest(BroadcastRequestCall* broadcastRequestCall)
{
	int id = broadcastRequestCall->getRequestID();

	// If we're just acting as a forwarder...
	if (overlay->forwardAndForget(broadcastRequestCall)) {
		BroadcastRequestResponse* resp = new BroadcastRequestResponse();
		resp->setRequestID(id);
		resp->setValid(true);
		resp->setBitLength(BROADCASTREQUESTRESPONSE_L(resp));

		sendRpcResponse(broadcastRequestCall, resp);
		RECORD_STATS(numMessages++; numBytes += resp->getByteLength());
		return;
	}

	// If we've seen this broadcast before...
	if (!searches[id].receivedFrom.isUnspecified()) {
		globalStatistics->bcastSearch[id].duplicated++;

		BroadcastRequestResponse* resp = new BroadcastRequestResponse();
		resp->setRequestID(id);
		resp->setValid(false);
		resp->setBitLength(BROADCASTREQUESTRESPONSE_L(resp));

		sendRpcResponse(broadcastRequestCall, resp);
		RECORD_STATS(numMessages++; numBytes += resp->getByteLength());
		return;
	}

	if (searches[id].started) {
		globalStatistics->bcastSearch[id].extra++;

		BroadcastRequestResponse* resp = new BroadcastRequestResponse();
		resp->setRequestID(id);
		resp->setValid(false);
		resp->setBitLength(BROADCASTREQUESTRESPONSE_L(resp));

		sendRpcResponse(broadcastRequestCall, resp);
		RECORD_STATS(numMessages++; numBytes += resp->getByteLength());
		return;
	}

	int ttl = broadcastRequestCall->getTTL();
	globalStatistics->addStdDev("Broadcast: Hop count", this->ttl - ttl);

	std::list<const OverlayKey*> results;

	if (storage->size() > 0) {
		for (DhtDataMap::iterator it = storage->begin();it != storage->end();it++) {
			if (broadcastRequestCall->getQuery() == (*it).second.value) {
				results.push_back(&(*it).first);
				break;
			}
		}
	}

	if (!results.empty()) {
		BroadcastResponseCall* reply = new BroadcastResponseCall();
		reply->setRequestID(id);

		reply->setResultsArraySize(results.size());

		uint i = 0;
		for (std::list<const OverlayKey*>::iterator it = results.begin();it != results.end();it++)
			reply->setResults(i++, *(*it));

		reply->setBitLength(BROADCASTRESPONSECALL_L(reply));
		sendRouteRpcCall(TIER2_COMP, broadcastRequestCall->getOrigin(), reply);
		RECORD_STATS(numMessages++; numBytes += reply->getByteLength());

		results.clear();
	}

	if (ttl <= 0) {
		globalStatistics->bcastSearch[id].expired++;
		BroadcastRequestResponse* resp = new BroadcastRequestResponse();
		resp->setRequestID(id);
		resp->setValid(true);

		sendRpcResponse(broadcastRequestCall, resp);
		RECORD_STATS(numMessages++; numBytes += resp->getByteLength());
		return;
	}
	broadcastRequestCall->setTTL(ttl - 1);

	globalStatistics->bcastSearch[id].actual++;

	// Forward the broadcast onwards
	std::list<const BroadcastInfo*> requests = overlay->forwardBroadcast(broadcastRequestCall);
	BroadcastRequestCall* newCall;

	for (std::list<const BroadcastInfo*>::iterator it = requests.begin();it != requests.end();it++) {
		newCall = new BroadcastRequestCall();
		newCall->setOrigin(broadcastRequestCall->getOrigin());
		newCall->setQuery(broadcastRequestCall->getQuery());
		newCall->setRequestID(broadcastRequestCall->getRequestID());
		newCall->setTTL(broadcastRequestCall->getTTL());
		newCall->setBitLength(BROADCASTREQUESTCALL_L(newCall));

		newCall->addObject((*it)->info);

		if (!(*it)->node.isUnspecified())
			sendRouteRpcCall(TIER2_COMP, (*it)->node, newCall);
		else if (!(*it)->key.isUnspecified())
			sendRouteRpcCall(TIER2_COMP, (*it)->key, newCall);
		else
			throw cRuntimeError("BroadcastTestApp: Nothing to route to!");

		RECORD_STATS(numMessages++; numBytes += newCall->getByteLength());
		delete (*it);
	}

	requests.clear();

	searches[id].receivedFrom = broadcastRequestCall->getSrcNode().getKey();

	BroadcastRequestResponse* resp = new BroadcastRequestResponse();
	resp->setRequestID(id);
	resp->setValid(true);
	resp->setBitLength(BROADCASTREQUESTRESPONSE_L(resp));

	sendRpcResponse(broadcastRequestCall, resp);
	RECORD_STATS(numMessages++; numBytes += resp->getByteLength());
}

void BroadcastTestApp::rpcBroadcastResponse(BroadcastResponseCall* broadcastResponseCall)
{
	int id = broadcastResponseCall->getRequestID();

	globalStatistics->bcastSearch[id].success = true;

//	for (uint i = 0;i < broadcastResponseCall->getResultsArraySize();i++)
//		std::cout << overlay->getThisNode().getKey() << ": Found '" << globalStatistics->bcastSearch[id].query << "' with key: " << broadcastResponseCall->getResults(i) << " at " << simTime() << endl;

	// We don't send a response, so just delete the call message
	delete broadcastResponseCall;
}

void BroadcastTestApp::finishApp()
{
	simtime_t time = globalStatistics->calcMeasuredLifetime(creationTime);

	if (time >= GlobalStatistics::MIN_MEASURED) {
		globalStatistics->addStdDev("Broadcast: Sent Messages/s", numMessages / time);
		globalStatistics->addStdDev("Broadcast: Sent Bytes/s", numBytes / time);
	}
}
