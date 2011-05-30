//
// Copyright (C) 2006 Institut fuer Telematik, Universitaet Karlsruhe (TH)
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
 * @file EpiChord.cc
 * @author Jamie Furness
 */

#include <GlobalStatistics.h>
#include <BootstrapList.h>
#include <IterativeLookup.h>

#include "EpiChordNodeList.h"
#include "EpiChordFingerCache.h"
#include "EpiChord.h"
#include "EpiChordIterativeLookup.h"

namespace oversim {

Define_Module(EpiChord);

EpiChord::EpiChord()
{

}

EpiChord::~EpiChord()
{
	// destroy self timer messages
	cancelAndDelete(join_timer);
	cancelAndDelete(stabilize_timer);
	cancelAndDelete(cache_timer);
}

void EpiChord::initializeOverlay(int stage)
{
	// because of IPAddressResolver, we need to wait until interfaces
	// are registered, address auto-assignment takes place etc.
	if (stage != MIN_STAGE_OVERLAY)
		return;

	if (defaultRoutingType != ITERATIVE_ROUTING && defaultRoutingType != EXHAUSTIVE_ITERATIVE_ROUTING)
		throw new cRuntimeError("EpiChord::initializeOverlay(): EpiChord only works with iterative routing.");

	if (iterativeLookupConfig.redundantNodes < 2)
		throw new cRuntimeError("EpiChord::initializeOverlay(): EpiChord requires lookupRedundantNodes >= 2.");

	// EpiChord provides KBR services
	kbr = true;

	// fetch some parameters
	successorListSize = par("successorListSize");
	nodesPerSlice = par("nodesPerSlice");
	joinRetry = par("joinRetry");
	joinDelay = par("joinDelay");
	stabilizeDelay = par("stabilizeDelay");
	stabilizeEstimation = par("stabilizeEstimation");
	stabilizeEstimateMuliplier = par("stabilizeEstimateMuliplier");
	cacheFlushDelay = par("cacheFlushDelay");
	cacheCheckMultiplier = par("cacheCheckMultiplier");
	cacheTTL = par("cacheTTL");
	cacheUpdateDelta = par("cacheUpdateDelta");
	activePropagation = par("activePropagation");
	sendFalseNegWarnings = par("sendFalseNegWarnings");
	fibonacci = par("fibonacci");

	// statistics
	joinCount = 0;
	joinBytesSent = 0;
	stabilizeCount = 0;
	stabilizeBytesSent = 0;

	nodeProbes = 0;
	nodeTimeouts = 0;
	cacheCheckCounter = 0;

	// find friend modules
	findFriendModules();

	// add some watches
	WATCH(thisNode);
	WATCH(bootstrapNode);
	WATCH(joinRetry);

	// self-messages
	join_timer = new cMessage("join_timer");
	stabilize_timer = new cMessage("stabilize_timer");
	cache_timer = new cMessage("cache_timer");
}

void EpiChord::handleTimerEvent(cMessage* msg)
{
	// catch JOIN timer
	if (msg == join_timer)
		handleJoinTimerExpired(msg);
	// catch STABILIZE timer
	else if (msg == stabilize_timer)
		handleStabilizeTimerExpired(msg);
	// catch CACHE FLUSH timer
	else if (msg == cache_timer)
		handleCacheFlushTimerExpired(msg);
	else
		error("EpiChord::handleTimerEvent(): received self message of unknown type!");
}

void EpiChord::recordOverlaySentStats(BaseOverlayMessage* msg)
{
	BaseOverlayMessage* innerMsg = msg;
	while (innerMsg->getType() != APPDATA && innerMsg->getEncapsulatedPacket() != NULL)
		innerMsg = static_cast<BaseOverlayMessage*>(innerMsg->getEncapsulatedPacket());

	switch (innerMsg->getType()) {
		case OVERLAYSIGNALING: {
			throw new cRuntimeError("Unknown overlay signaling message type!");
		}

		case RPC: {
			nodeProbes++;

			// It was a stabilize call/response
			if ((dynamic_cast<EpiChordStabilizeCall*>(innerMsg) != NULL) || (dynamic_cast<EpiChordStabilizeResponse*>(innerMsg) != NULL))
				RECORD_STATS(stabilizeCount++; stabilizeBytesSent += msg->getByteLength());
			// It was a join call/response
			else if ((dynamic_cast<EpiChordJoinCall*>(innerMsg) != NULL) || (dynamic_cast<EpiChordJoinResponse*>(innerMsg) != NULL))
				RECORD_STATS(joinCount++; joinBytesSent += msg->getByteLength());
			break;
		}

		case APPDATA: {
			break;
		}

		default: {
			throw new cRuntimeError("Unknown message type!");
		}
	}
}

void EpiChord::finishOverlay()
{
	// remove this node from the bootstrap list
	bootstrapList->removeBootstrapNode(thisNode);

	simtime_t time = globalStatistics->calcMeasuredLifetime(creationTime);
	if (time < GlobalStatistics::MIN_MEASURED)
		return;

	globalStatistics->addStdDev("EpiChord: Sent JOIN Messages/s", joinCount / time);
	globalStatistics->addStdDev("EpiChord: Sent STABILIZE Messages/s", stabilizeCount / time);
	globalStatistics->addStdDev("EpiChord: Sent JOIN Bytes/s", joinBytesSent / time);
	globalStatistics->addStdDev("EpiChord: Sent STABILIZE Bytes/s", stabilizeBytesSent / time);
	globalStatistics->addStdDev("EpiChord: Finger updates success/s", fingerCache->getSuccessfulUpdates() / time);

	globalStatistics->addStdDev("EpiChord: Cache live nodes", fingerCache->countLive());
	globalStatistics->addStdDev("EpiChord: Cache live nodes (real)", fingerCache->countRealLive());
	globalStatistics->addStdDev("EpiChord: Cache dead nodes", fingerCache->countDead());
	globalStatistics->addStdDev("EpiChord: Cache dead nodes (real)", fingerCache->countRealDead());

	// Estimated node lifetime
	if (stabilizeEstimation)
		globalStatistics->addStdDev("EpiChord: Estimated node lifetime", SIMTIME_DBL(fingerCache->estimateNodeLifetime()));
}

OverlayKey EpiChord::distance(const OverlayKey& x, const OverlayKey& y, bool useAlternative) const
{
	return KeyRingMetric().distance(x, y);
}

void EpiChord::updateTooltip()
{
	if (ev.isGUI()) {
		std::stringstream ttString;

		// show our predecessor and successor in tooltip
		ttString << predecessorList->getNode() << endl << thisNode << endl << successorList->getNode();

		getParentModule()->getParentModule()->getDisplayString().
		setTagArg("tt", 0, ttString.str().c_str());
		getParentModule()->getDisplayString().
		setTagArg("tt", 0, ttString.str().c_str());
		getDisplayString().setTagArg("tt", 0, ttString.str().c_str());

		// draw an arrow to our current successor
		showOverlayNeighborArrow(successorList->getNode(), true, "m=m,50,0,50,0;ls=red,1");
		showOverlayNeighborArrow(predecessorList->getNode(), false, "m=m,50,100,50,100;ls=green,1");
	}
}

void EpiChord::findFriendModules()
{
	successorList = check_and_cast<EpiChordNodeList*>(getParentModule()->getSubmodule("successorList"));
	predecessorList = check_and_cast<EpiChordNodeList*>(getParentModule()->getSubmodule("predecessorList"));
	fingerCache = check_and_cast<EpiChordFingerCache*>(getParentModule()->getSubmodule("fingerCache"));
}

void EpiChord::initializeFriendModules()
{
	// initialize finger cache
	fingerCache->initializeCache(thisNode, this, cacheTTL);

	// initialize successor list
	successorList->initializeList(successorListSize, thisNode, fingerCache, this, true);

	// initialize predecessor list
	predecessorList->initializeList(successorListSize, thisNode, fingerCache, this, false);
}

void EpiChord::changeState(int toState)
{
	//
	// Defines tasks to be executed when a state change occurs.
	//

	switch (toState) {
	case INIT:
		state = INIT;

		setOverlayReady(false);

		// initialize successor and predecessor lists
		initializeFriendModules();

		updateTooltip();

		// debug message
		if (debugOutput) {
			EV << "[EpiChord::changeState() @ " << thisNode.getIp()
			<< " (" << thisNode.getKey().toString(16) << ")]\n"
			<< "	Entered INIT stage"
			<< endl;
		}

		getParentModule()->getParentModule()->bubble("Enter INIT state.");
		break;

	case BOOTSTRAP:
		state = BOOTSTRAP;

		// initiate bootstrap process
		cancelEvent(join_timer);
		// workaround: prevent notificationBoard from taking
		// ownership of join_timer message
		take(join_timer);
		scheduleAt(simTime(), join_timer);

		// debug message
		if (debugOutput) {
			EV << "[EpiChord::changeState() @ " << thisNode.getIp()
			<< " (" << thisNode.getKey().toString(16) << ")]\n"
			<< "	Entered BOOTSTRAP stage"
			<< endl;
		}
		getParentModule()->getParentModule()->bubble("Enter BOOTSTRAP state.");

		// find a new bootstrap node and enroll to the bootstrap list
		bootstrapNode = bootstrapList->getBootstrapNode();

		// is this the first node?
		if (bootstrapNode.isUnspecified()) {
			// create new epichord ring
			assert(successorList->isEmpty());
			assert(predecessorList->isEmpty());
			bootstrapNode = thisNode;
			changeState(READY);
			updateTooltip();
		}
		break;

	case READY:
		state = READY;

		setOverlayReady(true);

		// initiate stabilization protocol
		cancelEvent(stabilize_timer);
		scheduleAt(simTime() + stabilizeDelay, stabilize_timer);

		// initiate finger repair protocol
		cancelEvent(cache_timer);
		scheduleAt(simTime() + cacheFlushDelay, cache_timer);

		// debug message
		if (debugOutput) {
			EV << "[EpiChord::changeState() @ " << thisNode.getIp()
			<< " (" << thisNode.getKey().toString(16) << ")]\n"
			<< "	Entered READY stage"
			<< endl;
		}
		getParentModule()->getParentModule()->bubble("Enter READY state.");
		break;
	}
}

void EpiChord::handleJoinTimerExpired(cMessage* msg)
{
	// only process timer, if node is not bootstrapped yet
	if (state == READY)
		return;

	// enter state BOOTSTRAP
	if (state != BOOTSTRAP)
		changeState(BOOTSTRAP);

	// change bootstrap node from time to time
	joinRetry--;
	if (joinRetry == 0) {
		joinRetry = par("joinRetry");
		changeState(BOOTSTRAP);
		return;
	}

	// call JOIN RPC
	EpiChordJoinCall* call = new EpiChordJoinCall("EpiChordJoinCall");
	call->setBitLength(EPICHORD_JOINCALL_L(call));

	sendRouteRpcCall(OVERLAY_COMP, bootstrapNode, thisNode.getKey(), call, NULL, defaultRoutingType, joinDelay);

	// schedule next bootstrap process in the case this one fails
	cancelEvent(join_timer);
	scheduleAt(simTime() + joinDelay, msg);
}

void EpiChord::handleStabilizeTimerExpired(cMessage* msg)
{
	if (state != READY)
		return;

	if (!predecessorList->isEmpty()) {
		// call STABILIZE RPC
		EpiChordStabilizeCall* call = new EpiChordStabilizeCall("PredecessorStabilizeCall");
		call->setNodeType(SUCCESSOR);

		NodeVector* successorAdditions = successorList->getAdditions();

		int numAdditions = successorAdditions->size();
		call->setAdditionsArraySize(numAdditions);

		for (int i = 0;i < numAdditions;i++)
			call->setAdditions(i, (*successorAdditions)[i]);

		successorAdditions->clear();

		call->setBitLength(EPICHORD_STABILIZECALL_L(call));
		sendUdpRpcCall(predecessorList->getNode(), call);
	}

	if (!successorList->isEmpty()) {
		// call STABILIZE RPC
		EpiChordStabilizeCall* call = new EpiChordStabilizeCall("SuccessorStabilizeCall");
		call->setNodeType(PREDECESSOR);

		NodeVector* predecessorAdditions = predecessorList->getAdditions();

		int numAdditions = predecessorAdditions->size();
		call->setAdditionsArraySize(numAdditions);

		for (int i = 0;i < numAdditions;i++)
			call->setAdditions(i, (*predecessorAdditions)[i]);

		predecessorAdditions->clear();

		call->setBitLength(EPICHORD_STABILIZECALL_L(call));
		sendUdpRpcCall(successorList->getNode(), call);
	}

	// Decide when to next schedule stabilization
	simtime_t avgLifetime = stabilizeDelay;
	if (stabilizeEstimation) {
		simtime_t estimate = fingerCache->estimateNodeLifetime();
		if (estimate > 0)
			avgLifetime = estimate * stabilizeEstimateMuliplier;
	}

	// schedule next stabilization process
	cancelEvent(stabilize_timer);
	scheduleAt(simTime() + avgLifetime, msg);
}

void EpiChord::handleCacheFlushTimerExpired(cMessage* msg)
{
	if (state != READY)
		return;

	// Remove expired entries from the finger cache
	fingerCache->removeOldFingers();

	// Check we have enough remaining cache entries
	if (++cacheCheckCounter > cacheCheckMultiplier) {
		this->checkCacheInvariant();
		cacheCheckCounter = 0;
	}

	nodeProbes *= cacheUpdateDelta;
	nodeTimeouts *= cacheUpdateDelta;

	// schedule next cache flush process
	cancelEvent(cache_timer);
	scheduleAt(simTime() + cacheFlushDelay, msg);
}

void EpiChord::checkCacheInvariant()
{
	if (state != READY || !predecessorList->isFull() || !successorList->isFull())
		return;

	if (fibonacci) {
		// Set the limits
		OverlayKey nearOffset = 0;
		OverlayKey farOffset = 1;
		OverlayKey limitOffset = (OverlayKey::getMax() / 2);
		OverlayKey neighbourOffset = successorList->getNode(successorList->getSize() - 1).getKey() - thisNode.getKey();
		OverlayKey temp;

		// Until we pass halfway
		while (nearOffset < limitOffset) {
			// Only check if we have passed the end of the successor list
			if (neighbourOffset < nearOffset)
				checkCacheSlice(thisNode.getKey() + nearOffset, thisNode.getKey() + farOffset);

			temp = farOffset;
			farOffset = nearOffset + farOffset;
			nearOffset = temp;
		}

		// Reset the limits
		nearOffset = 0;
		farOffset = 1;
		neighbourOffset = thisNode.getKey() - predecessorList->getNode(predecessorList->getSize() - 1).getKey();

		// Until we pass halfway
		while (nearOffset < limitOffset) {
			// Only check if we have passed the end of the successor list
			if (neighbourOffset < nearOffset)
				checkCacheSlice(thisNode.getKey() - farOffset, thisNode.getKey() - nearOffset);

			temp = farOffset;
			farOffset = nearOffset + farOffset;
			nearOffset = temp;
		}
	}
	else {
		// Set the offset
		int offset = 1;

		// Set the limits
		OverlayKey farLimit = thisNode.getKey() + (OverlayKey::getMax() >> offset++);
		OverlayKey nearLimit = thisNode.getKey() + (OverlayKey::getMax() >> offset++);

		// Check successor list
		OverlayKey lastSuccessor = successorList->getNode(successorList->getSize() - 1).getKey();
		//   ---- (us) ---- (last successor) ---- (near limit) ----
		while (lastSuccessor.isBetween(thisNode.getKey(), nearLimit)) {
			checkCacheSlice(nearLimit, farLimit);

			// Calculate the limits of the next slice
			farLimit = nearLimit;
			nearLimit = thisNode.getKey() + (OverlayKey::getMax() >> offset++);
		}

		// Reset the offset
		offset = 1;

		// Reset the limits
		farLimit = thisNode.getKey() - (OverlayKey::getMax() >> offset++);
		nearLimit = thisNode.getKey() - (OverlayKey::getMax() >> offset++);

		// Check predecessor list
		OverlayKey lastPredecessor = predecessorList->getNode(predecessorList->getSize() - 1).getKey();
		//   ---- (near limit) ---- (last predecessor) ---- (us) ----
		while (lastPredecessor.isBetween(nearLimit, thisNode.getKey())) {
			checkCacheSlice(farLimit, nearLimit);

			// Calculate the limits of the next slice
			farLimit = nearLimit;
			nearLimit = thisNode.getKey() - (OverlayKey::getMax() >> offset++);
		}
	}
}

void EpiChord::checkCacheSlice(OverlayKey start, OverlayKey end)
{
//	double gamma = this->calculateGamma();

	int numNodes = fingerCache->countSlice(start, end);
	int requiredNodes = nodesPerSlice; // Disable the nodes per slice calculation since it isn't used in the original implementation
	// int requiredNodes = (int) ceil(nodesPerSlice / (1.0 - gamma));

	// If this slice doesn't have enough nodes, try look up some more...
	if (numNodes < requiredNodes) {
		OverlayKey offset = (end - start) / 2;
		OverlayKey destKey = start + offset; // Halfway between start and end

		LookupCall* call = new LookupCall();
		call->setKey(destKey);
		call->setNumSiblings(requiredNodes > this->getMaxNumSiblings() ? this->getMaxNumSiblings() : requiredNodes);

//		std::cout << simTime() << ": [" << thisNode.getKey() << "] Sending fix fingers to: " << destKey << std::endl;
		sendInternalRpcCall(OVERLAY_COMP, call);
	}
}

NodeVector* EpiChord::findNode(const OverlayKey& key, int numRedundantNodes, int numSiblings, BaseOverlayMessage* msg)
{
	EpiChordFindNodeExtMessage* findNodeExt = NULL;

	if (msg != NULL) {
		if (!msg->hasObject("findNodeExt")) {
			findNodeExt = new EpiChordFindNodeExtMessage("findNodeExt");
			msg->addObject(findNodeExt);
		}
		else
			findNodeExt = (EpiChordFindNodeExtMessage*) msg->getObject("findNodeExt");

		// Reset the expires array to 0 incase we return before setting the new size
		findNodeExt->setLastUpdatesArraySize(0);
	}

	NodeVector* nextHop = new NodeVector();

	if (state != READY)
		return nextHop;

	simtime_t now = simTime();
	NodeHandle source = NodeHandle::UNSPECIFIED_NODE;
	std::vector<simtime_t>* lastUpdates = new std::vector<simtime_t>();
	std::set<NodeHandle>* exclude = new std::set<NodeHandle>();
	bool err;

	exclude->insert(thisNode);

	if (msg != NULL) {
		// Add the origin node to the finger cache
		source = ((FindNodeCall*) msg)->getSrcNode();
		if (!source.isUnspecified())
			exclude->insert(source);

		this->receiveNewNode(source, true, OBSERVED, now);
	}

	// see section II of EpiChord MIT-LCS-TR-963

	// if the message is destined for this node
	if (key.isUnspecified() || isSiblingFor(thisNode, key, 1, &err)) {
		nextHop->push_back(thisNode);
		lastUpdates->push_back(now);

		if (!predecessorList->isEmpty()) {
			EpiChordFingerCacheEntry* entry = fingerCache->getNode(predecessorList->getNode());
			nextHop->push_back(predecessorList->getNode());
			if (entry != NULL)
				lastUpdates->push_back(entry->lastUpdate);
			else
				lastUpdates->push_back(now);
		}

		if (!successorList->isEmpty()) {
			EpiChordFingerCacheEntry* entry = fingerCache->getNode(successorList->getNode());
			nextHop->push_back(successorList->getNode());
			if (entry != NULL)
				lastUpdates->push_back(entry->lastUpdate);
			else
				lastUpdates->push_back(now);
		}
	}
	else {
		const EpiChordFingerCacheEntry* entry;

		// No source specified, this implies it is a local request
		if (source.isUnspecified()) {
			OverlayKey successorDistance = distance(successorList->getNode().getKey(), key);
			OverlayKey predecessorDistance = distance(predecessorList->getNode().getKey(), key);

			// add a successor or predecessor, depending on which one is closer to the target
			entry = fingerCache->getNode(predecessorDistance < successorDistance ? predecessorList->getNode() : successorList->getNode());
		}
		//   ---- (source) ---- (us) ---- (destination) ----
		else if (thisNode.getKey().isBetween(source.getKey(), key)) {
			// add a successor
			entry = fingerCache->getNode(successorList->getNode());
		}
		// ---- (destination) ---- (us) ---- (source) ----
		else {
			// add a predecessor
			entry = fingerCache->getNode(predecessorList->getNode());
		}

		if (entry != NULL) {
			nextHop->push_back(entry->nodeHandle);
			lastUpdates->push_back(entry->lastUpdate);
			exclude->insert(entry->nodeHandle);
		}

		// Add the numRedundantNodes best next hops
		fingerCache->findBestHops(key, nextHop, lastUpdates, exclude, numRedundantNodes);
	}

	// Check we managed to actually find something
	if (nextHop->empty())
		throw new cRuntimeError("EpiChord::findNode() Failed to find node");

	if (msg != NULL) {
		int numVisited = nextHop->size();
		findNodeExt->setLastUpdatesArraySize(numVisited);

		for (int i = 0;i < numVisited;i++)
			findNodeExt->setLastUpdates(i, (*lastUpdates)[i]);

		findNodeExt->setBitLength(EPICHORD_FINDNODEEXTMESSAGE_L(findNodeExt));
	}

	delete exclude;
	delete lastUpdates;
	return nextHop;
}

void EpiChord::joinOverlay()
{
	changeState(INIT);
	changeState(BOOTSTRAP);
}

void EpiChord::joinForeignPartition(const NodeHandle &node)
{
	Enter_Method_Silent();

	// create a join call and sent to the bootstrap node.
	EpiChordJoinCall *call = new EpiChordJoinCall("EpiChordJoinCall");
	call->setBitLength(EPICHORD_JOINCALL_L(call));

	this->receiveNewNode(node, true, LOCAL, simTime());

	sendRouteRpcCall(OVERLAY_COMP, node, thisNode.getKey(), call, NULL, defaultRoutingType, joinDelay);
}

bool EpiChord::isSiblingFor(const NodeHandle& node, const OverlayKey& key, int numSiblings, bool* err)
{
	assert(!key.isUnspecified());

	if (state != READY) {
		*err = true;
		return false;
	}

	// set default number of siblings to consider
	if (numSiblings < 0 || numSiblings > this->getMaxNumSiblings())
		numSiblings = this->getMaxNumSiblings();

	// if this is the first and only node on the ring, it is responsible
	if (predecessorList->isEmpty() && node == thisNode) {
		if (successorList->isEmpty() || node.getKey() == key) {
			*err = false;
			return true;
		}
		else {
			*err = true;
			return false;
		}
	}

	// if its between our predecessor and us, it's for us
	if (node == thisNode && key.isBetweenR(predecessorList->getNode().getKey(), thisNode.getKey())) {
		*err = false;
		return true;
	}

	NodeHandle prevNode = predecessorList->getNode();
	NodeHandle curNode;

	for (int i = -1;i < (int)successorList->getSize();i++) {
		curNode = i < 0 ? thisNode : successorList->getNode(i);

		if (node == curNode) {
			// is the message destined for curNode?
			if (key.isBetweenR(prevNode.getKey(), curNode.getKey())) {
				if (numSiblings <= ((int)successorList->getSize() - i)) {
					*err = false;
					return true;
				}
				else {
					*err = true;
					return false;
				}
			}
			else {
				// the key doesn't directly belong to this node, but
				// the node could be a sibling for this key
				if (numSiblings <= 1) {
					*err = false;
					return false;
				}
				else {
					// In EpiChord we don't know if we belong to the
					// replicaSet of one of our predecessors
					*err = true;
					return false;
				}
			}
		}

		prevNode = curNode;
	}

	// node is not in our neighborSet
	*err = true;
	return false;
}

int EpiChord::getMaxNumSiblings()
{
	return successorListSize;
}

int EpiChord::getMaxNumRedundantNodes()
{
	return iterativeLookupConfig.redundantNodes;
}

double EpiChord::calculateGamma()
{
	double gamma = 0.0; // ratio of lookup failures

	// Make sure we don't divide by 0!
	if (nodeProbes > 0)
		gamma = nodeTimeouts / nodeProbes;

	return gamma;
}

bool EpiChord::handleRpcCall(BaseCallMessage* msg)
{
	if (state != READY) {
		EV << "[EpiChord::handleRpcCall() @ " << thisNode.getIp()
		   << " (" << thisNode.getKey().toString(16) << ")]\n"
		   << "	Received RPC call and state != READY"
		   << endl;
		return false;
	}

	// Add the origin node to the finger cache
	this->receiveNewNode(msg->getSrcNode(), true, OBSERVED, simTime());

	// delegate messages
	RPC_SWITCH_START(msg)
	// RPC_DELEGATE(<messageName>[Call|Response], <methodToCall>)

	RPC_DELEGATE(EpiChordJoin, rpcJoin);
	RPC_DELEGATE(EpiChordJoinAck, rpcJoinAck);
	RPC_DELEGATE(EpiChordStabilize, rpcStabilize);
	RPC_DELEGATE(EpiChordFalseNegWarning, rpcFalseNegWarning);

	RPC_SWITCH_END()

	return RPC_HANDLED;
}

void EpiChord::handleRpcResponse(BaseResponseMessage* msg, cPolymorphic* context, int rpcId, simtime_t rtt)
{
	// Add the origin node to the finger cache
	this->receiveNewNode(msg->getSrcNode(), true, OBSERVED, simTime());

	RPC_SWITCH_START(msg)
	RPC_ON_RESPONSE(FindNode) {
		handleRpcFindNodeResponse(_FindNodeResponse);
		EV << "[EpiChord::handleRpcResponse() @ " << thisNode.getIp()
		<< " (" << thisNode.getKey().toString(16) << ")]\n"
		<< "	Received a FindNode Response: id=" << rpcId << "\n"
		<< "	msg=" << *_FindNodeResponse << " rtt=" << rtt
		<< endl;
		break;
	}
	RPC_ON_RESPONSE(EpiChordJoin) {
		handleRpcJoinResponse(_EpiChordJoinResponse);
		EV << "[EpiChord::handleRpcResponse() @ " << thisNode.getIp()
		<< " (" << thisNode.getKey().toString(16) << ")]\n"
		<< "	Received a Join RPC Response: id=" << rpcId << "\n"
		<< "	msg=" << *_EpiChordJoinResponse << " rtt=" << rtt
		<< endl;
		break;
	}
	RPC_ON_RESPONSE(EpiChordStabilize) {
		handleRpcStabilizeResponse(_EpiChordStabilizeResponse);
		EV << "[EpiChord::handleRpcResponse() @ " << thisNode.getIp()
		<< " (" << thisNode.getKey().toString(16) << ")]\n"
		<< "	Received a Stabilize RPC Response: id=" << rpcId << "\n"
		<< "	msg=" << *_EpiChordStabilizeResponse << " rtt=" << rtt
		<< endl;
		break;
	}
	RPC_SWITCH_END( )
}

void EpiChord::handleRpcTimeout(BaseCallMessage* msg, const TransportAddress& dest, cPolymorphic* context, int rpcId, const OverlayKey&)
{
	nodeTimeouts++;

	// Handle failed node
	if (!dest.isUnspecified() && !handleFailedNode(dest))
		join();
}

bool EpiChord::handleFailedNode(const TransportAddress& failed)
{
	Enter_Method_Silent();

	assert(failed != thisNode);

	successorList->handleFailedNode(failed);
	predecessorList->handleFailedNode(failed);
	fingerCache->handleFailedNode(failed);

	if (activePropagation && (predecessorList->hasChanged() || successorList->hasChanged())) {
		// schedule next stabilization process
		cancelEvent(stabilize_timer);
		scheduleAt(simTime(), stabilize_timer);

		updateTooltip();
	}

	if (state != READY)
		return true;

	// lost our last successor - cancel periodic stabilize tasks and wait for rejoin
	if (successorList->isEmpty() || predecessorList->isEmpty()) {
		cancelEvent(stabilize_timer);
		cancelEvent(cache_timer);

		return false;
	}

	return true;
}

void EpiChord::sendFalseNegWarning(NodeHandle bestPredecessor, NodeHandle bestSuccessor, NodeVector* deadNodes)
{
	Enter_Method_Silent();

	if (state != READY)
		return;

	// If we aren't to send warnings then stop
	if (!sendFalseNegWarnings)
		return;

	EpiChordFalseNegWarningCall* warning = new EpiChordFalseNegWarningCall("EpiChordFalseNegWarningCall");

	warning->setBestPredecessor(bestPredecessor);

	warning->setDeadNodeArraySize(deadNodes->size());
	for (uint i = 0;i < deadNodes->size();i++)
		warning->setDeadNode(i, (*deadNodes)[i]);

	warning->setBitLength(EPICHORD_FALSENEGWARNINGCALL_L(warning));
	sendUdpRpcCall(bestSuccessor, warning);
}

void EpiChord::rpcJoin(EpiChordJoinCall* joinCall)
{
	EpiChordJoinResponse* joinResponse = new EpiChordJoinResponse("EpiChordJoinResponse");

	// Add successor list
	int sucNum = successorList->getSize();
	joinResponse->setSucNodeArraySize(sucNum);

	for (int k = 0; k < sucNum; k++)
		joinResponse->setSucNode(k, successorList->getNode(k));

	// Add predecessor list
	int preNum = predecessorList->getSize();
	joinResponse->setPreNodeArraySize(preNum);

	for (int k = 0; k < preNum; k++)
		joinResponse->setPreNode(k, predecessorList->getNode(k));

	// Add finger cache
	int cacheNum = fingerCache->getSize();
	joinResponse->setCacheNodeArraySize(cacheNum);
	joinResponse->setCacheLastUpdateArraySize(cacheNum);

	for (int k = 0;k < cacheNum;k++) {
		EpiChordFingerCacheEntry* entry = fingerCache->getNode(k);
		if (entry == NULL)
			continue;

		joinResponse->setCacheNode(k, entry->nodeHandle);
		joinResponse->setCacheLastUpdate(k, entry->lastUpdate);
	}

	// Send the response
	joinResponse->setBitLength(EPICHORD_JOINRESPONSE_L(joinResponse));
	sendRpcResponse(joinCall, joinResponse);

	updateTooltip();
}

void EpiChord::handleRpcJoinResponse(EpiChordJoinResponse* joinResponse)
{
	// determine the number of successor nodes to add
	uint sucNum = successorListSize;
	if (joinResponse->getSucNodeArraySize() < sucNum)
		sucNum = joinResponse->getSucNodeArraySize();

	// add successor getNode(s)
	for (uint k = 0; k < sucNum; k++)
		successorList->addNode(joinResponse->getSucNode(k));

	// the sender of this message is our new successor
	successorList->addNode(joinResponse->getSrcNode());

	// determine the number of predecessor nodes to add
	uint preNum = successorListSize;
	if (joinResponse->getPreNodeArraySize() < preNum)
		preNum = joinResponse->getPreNodeArraySize();

	// add predecessor getNode(s)
	for (uint k = 0; k < preNum; k++)
		predecessorList->addNode(joinResponse->getPreNode(k));

	// if we don't have any predecessors, the requestor is also our new predecessor
	if (predecessorList->isEmpty())
		predecessorList->addNode(joinResponse->getSrcNode());

	int cacheNum = joinResponse->getCacheNodeArraySize();
	for (int k = 0;k < cacheNum; k++)
		this->receiveNewNode(joinResponse->getCacheNode(k), false, CACHE_TRANSFER, joinResponse->getCacheLastUpdate(k));

	updateTooltip();

	changeState(READY);

	EpiChordJoinAckCall* joinAck = new EpiChordJoinAckCall("EpiChordJoinAckCall");
	joinAck->setBitLength(EPICHORD_JOINACKCALL_L(joinAck));

	sendUdpRpcCall(joinResponse->getSrcNode(), joinAck);
}

void EpiChord::rpcJoinAck(EpiChordJoinAckCall* joinAck)
{
	// if we don't have a successor, the requestor is also our new successor
	if (successorList->isEmpty())
		successorList->addNode(joinAck->getSrcNode());

	// he is now our predecessor
	predecessorList->addNode(joinAck->getSrcNode());

	EpiChordJoinAckResponse* ackResponse = new EpiChordJoinAckResponse("EpiChordJoinAckResponse");

	// Send the response
	ackResponse->setBitLength(EPICHORD_JOINACKRESPONSE_L(ackResponse));
	sendRpcResponse(joinAck, ackResponse);
}

void EpiChord::rpcFalseNegWarning(EpiChordFalseNegWarningCall* warning)
{
	NodeHandle oldPredecessor = predecessorList->getNode();
	NodeHandle bestPredecessor = warning->getBestPredecessor();

	// Handle this warning!
	if (oldPredecessor != bestPredecessor) {
		// Remove all dead nodes
		int deadNum = warning->getDeadNodeArraySize();
		for (int i = 0;i < deadNum;i++)
			handleFailedNode(warning->getDeadNode(i));

		// Ensure the predecessor is known to us
		predecessorList->addNode(bestPredecessor);

		// If there were any changes, and they effected us
		if (activePropagation && (predecessorList->hasChanged() || successorList->hasChanged())) {
			// schedule next stabilization process
			cancelEvent(stabilize_timer);
			scheduleAt(simTime(), stabilize_timer);

			updateTooltip();
		}
	}

	EpiChordFalseNegWarningResponse* response = new EpiChordFalseNegWarningResponse("EpiChordFalseNegWarningResponse");

	// Send the response
	response->setBitLength(EPICHORD_FALSENEGWARNINGRESPONSE_L(response));
	sendRpcResponse(warning, response);
}

void EpiChord::rpcStabilize(EpiChordStabilizeCall* call)
{
	NodeHandle requestor = call->getSrcNode();

	// reply with StabilizeResponse message
	EpiChordStabilizeResponse* stabilizeResponse = new EpiChordStabilizeResponse("EpiChordStabilizeResponse");

	switch (call->getNodeType()) {
		// the call is from a predecessor
		case PREDECESSOR: {
			if (!predecessorList->contains(requestor))
				predecessorList->addNode(requestor);

			int numAdditions = call->getAdditionsArraySize();
			for (int i = 0;i < numAdditions;i++)
				predecessorList->addNode(call->getAdditions(i));

			break;
		}

		case SUCCESSOR: {
			if (!successorList->contains(requestor))
				successorList->addNode(requestor);

			int numAdditions = call->getAdditionsArraySize();
			for (int i = 0;i < numAdditions;i++)
				successorList->addNode(call->getAdditions(i));

			break;
		}

		default:
			throw new cRuntimeError("Received a stabilize request from an unknown source.");
	}

	// If there were any changes, and they effected us
	if (activePropagation && (predecessorList->hasChanged() || successorList->hasChanged())) {
		// schedule next stabilization process
		cancelEvent(stabilize_timer);
		scheduleAt(simTime(), stabilize_timer);

		updateTooltip();
	}

	simtime_t now = simTime();

	// Full stabilize response
	if (predecessorList->hasChanged() || successorList->hasChanged()) {
		// Add predecessor list
		{
			int preNum = predecessorList->getSize();
			stabilizeResponse->setPredecessorsArraySize(preNum);
			stabilizeResponse->setPredecessorsLastUpdateArraySize(preNum);

			for (int k = 0; k < preNum; k++) {
				EpiChordFingerCacheEntry* entry = fingerCache->getNode(predecessorList->getNode(k));
				stabilizeResponse->setPredecessors(k, predecessorList->getNode(k));
				if (entry != NULL)
					stabilizeResponse->setPredecessorsLastUpdate(k, entry->lastUpdate);
				else
					stabilizeResponse->setPredecessorsLastUpdate(k, now);
			}
		}

		// Add successor list
		{
			int sucNum = successorList->getSize();
			stabilizeResponse->setSuccessorsArraySize(sucNum);
			stabilizeResponse->setSuccessorsLastUpdateArraySize(sucNum);

			for (int k = 0; k < sucNum; k++) {
				EpiChordFingerCacheEntry* entry = fingerCache->getNode(successorList->getNode(k));
				stabilizeResponse->setSuccessors(k, successorList->getNode(k));
				if (entry != NULL)
					stabilizeResponse->setSuccessorsLastUpdate(k, entry->lastUpdate);
				else
					stabilizeResponse->setSuccessorsLastUpdate(k, now);
			}
		}

		// Add dead neighbouring nodes
		std::vector<EpiChordFingerCacheEntry> dead = fingerCache->getDeadRange(predecessorList->getNode(predecessorList->getSize() - 1).getKey(), successorList->getNode(successorList->getSize() - 1).getKey());
		stabilizeResponse->setDeadArraySize(dead.size());
		for (uint k = 0;k < dead.size();k++)
			stabilizeResponse->setDead(k, dead.at(k).nodeHandle);
	}
	// Partial stabilize response
	else {
		// Predecessors
		{
			stabilizeResponse->setPredecessorsArraySize(1);
			stabilizeResponse->setPredecessorsLastUpdateArraySize(1);

			EpiChordFingerCacheEntry* entry = fingerCache->getNode(predecessorList->getNode());
			stabilizeResponse->setPredecessors(0, predecessorList->getNode());
			if (entry != NULL)
				stabilizeResponse->setPredecessorsLastUpdate(0, entry->lastUpdate);
			else
				stabilizeResponse->setPredecessorsLastUpdate(0, now);
		}

		// Successors
		{
			stabilizeResponse->setSuccessorsArraySize(1);
			stabilizeResponse->setSuccessorsLastUpdateArraySize(1);

			EpiChordFingerCacheEntry* entry = fingerCache->getNode(successorList->getNode());
			stabilizeResponse->setSuccessors(0, successorList->getNode());
			if (entry != NULL)
				stabilizeResponse->setSuccessorsLastUpdate(0, entry->lastUpdate);
			else
				stabilizeResponse->setSuccessorsLastUpdate(0, now);
		}

		stabilizeResponse->setDeadArraySize(0);
	}

	stabilizeResponse->setBitLength(EPICHORD_STABILIZERESPONSE_L(stabilizeResponse));
	sendRpcResponse(call, stabilizeResponse);
}

void EpiChord::handleRpcStabilizeResponse(EpiChordStabilizeResponse* stabilizeResponse)
{
	if (state != READY)
		return;

	// Update the finger cache with them all
	int preNum = stabilizeResponse->getPredecessorsArraySize();
	for (int i = 0;i < preNum;i++)
		this->receiveNewNode(stabilizeResponse->getPredecessors(i), false, MAINTENANCE, stabilizeResponse->getPredecessorsLastUpdate(i));

	int sucNum = stabilizeResponse->getSuccessorsArraySize();
	for (int i = 0;i < sucNum;i++)
		this->receiveNewNode(stabilizeResponse->getSuccessors(i), false, MAINTENANCE, stabilizeResponse->getSuccessorsLastUpdate(i));

	// Handle any dead nodes
	int deadNum = stabilizeResponse->getDeadArraySize();
	for (int i = 0;i < deadNum;i++)
		this->handleFailedNode(stabilizeResponse->getDead(i));

	// If there were any changes, and they effected us
	if (activePropagation && (predecessorList->hasChanged() || successorList->hasChanged())) {
		// schedule next stabilization process
		cancelEvent(stabilize_timer);
		scheduleAt(simTime(), stabilize_timer);

		updateTooltip();
	}
}

AbstractLookup* EpiChord::createLookup(RoutingType routingType, const BaseOverlayMessage* msg, const cPacket* findNodeExt, bool appLookup)
{
	assert(findNodeExt == NULL);

	// Create a new EpiChordFindNodeExtMessage
	findNodeExt = new EpiChordFindNodeExtMessage("findNodeExt");

	AbstractLookup* newLookup = new EpiChordIterativeLookup(this, routingType, iterativeLookupConfig, findNodeExt, appLookup);

	delete findNodeExt;

	lookups.insert(newLookup);
	return newLookup;
}

void EpiChord::handleRpcFindNodeResponse(FindNodeResponse* response)
{
	if (!response->hasObject("findNodeExt"))
		return;

	EpiChordFindNodeExtMessage* findNodeExt = (EpiChordFindNodeExtMessage*) response->getObject("findNodeExt");
	assert(response->getClosestNodesArraySize() == findNodeExt->getLastUpdatesArraySize());

	// Take a note of all nodes returned in this FindNodeResponse
	int nodeNum = response->getClosestNodesArraySize();
	for (int i = 0;i < nodeNum;i++)
		this->receiveNewNode(response->getClosestNodes(i), false, OBSERVED, findNodeExt->getLastUpdates(i));
}

void EpiChord::receiveNewNode(const NodeHandle& node, bool direct, NodeSource source, simtime_t lastUpdate)
{
	if (node.isUnspecified())
		return;

	fingerCache->updateFinger(node, direct, lastUpdate, cacheTTL, source);

	// Attempt to add to successor list
	if (!successorList->contains(node) && (!successorList->isFull() || node.getKey().isBetween(thisNode.getKey(), successorList->getNode(successorList->getSize() - 1).getKey()))) {
		if (direct)
			successorList->addNode(node, true);
//		else
//			this->pingNode(node);
	}

	// Attempt to add to predecessor list
	if (!predecessorList->contains(node) && (!predecessorList->isFull() || node.getKey().isBetween(predecessorList->getNode(predecessorList->getSize() - 1).getKey(), thisNode.getKey()))) {
		if (direct)
			predecessorList->addNode(node, true);
//		else
//			this->pingNode(node);
	}

//	// If there were any changes, and they effected us
//	if (activePropagation && (predecessorList->hasChanged() || successorList->hasChanged())) {
//		// schedule next stabilization process
//		cancelEvent(stabilize_timer);
//		scheduleAt(simTime(), stabilize_timer);
//
//		updateTooltip();
//	}
}

void EpiChord::pingResponse(PingResponse* pingResponse, cPolymorphic* context, int rpcId, simtime_t rtt)
{
	this->receiveNewNode(pingResponse->getSrcNode(), true, MAINTENANCE, simTime());
}

}; //namespace
