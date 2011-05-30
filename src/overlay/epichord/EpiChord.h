//
// Copyright (C) 2009 Institut fuer Telematik, Universitaet Karlsruhe (TH)
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
 * @author Jamie Furness
 */

#ifndef _EPICHORD_
#define _EPICHORD_

#include <BaseOverlay.h>

#include "EpiChordFingerCache.h"
#include "EpiChordMessage_m.h"

namespace oversim {

class EpiChordNodeList;

/**
 * EpiChord overlay module
 *
 * Implementation of the EpiChord KBR overlay as described in
 * "EpiChord: Parallelizing the Chord Lookup Algorithm with Reactive Routing State Management" by B. Leong et al.
 *
 * @author Jamie Furness
 * @see BaseOverlay, EpiChordNodeList
 */
class EpiChord : public BaseOverlay
{
public:
	EpiChord();
	virtual ~EpiChord();

	// see BaseOverlay.h
	virtual void initializeOverlay(int stage);

	// see BaseOverlay.h
	virtual void handleTimerEvent(cMessage* msg);

	// see BaseOverlay.h
	virtual void recordOverlaySentStats(BaseOverlayMessage* msg);

	// see BaseOverlay.h
	virtual void finishOverlay();

	// see BaseOverlay.h
    OverlayKey distance(const OverlayKey& x, const OverlayKey& y, bool useAlternative = false) const;

	/**
	 * updates information shown in tk-environment
	 */
	virtual void updateTooltip();

protected:
	int successorListSize;
	double nodesPerSlice;
	int joinRetry;
	double joinDelay;
	double stabilizeDelay;
	bool stabilizeEstimation;
	double stabilizeEstimateMuliplier;
	double cacheFlushDelay;
	int cacheCheckMultiplier;
	int cacheCheckCounter;
	double cacheTTL;
	double nodeProbes;
	double nodeTimeouts;
	double cacheUpdateDelta;
	bool activePropagation;
	bool sendFalseNegWarnings;
	bool fibonacci;

	// timer messages
	cMessage* join_timer;
	cMessage* stabilize_timer;
	cMessage* cache_timer;

	// statistics
	int joinCount;
	int joinBytesSent;
	int stabilizeCount;
	int stabilizeBytesSent;

	// node references
	TransportAddress bootstrapNode;

	// module references
	EpiChordNodeList* successorList;
	EpiChordNodeList* predecessorList;
	EpiChordFingerCache* fingerCache;

	// EpiChord routines

	/**
	 * Assigns the successor and predecessor list modules to our reference
	 */
	virtual void findFriendModules();

	/**
	 * initializes successor and predecessor lists
	 */
	virtual void initializeFriendModules();

	/**
	 * changes node state
	 *
	 * @param toState state to change to
	 */
	virtual void changeState(int toState);

	/**
	 * handle an expired join timer
	 *
	 * @param msg the timer self-message
	 */
	virtual void handleJoinTimerExpired(cMessage* msg);

	/**
	 * handle an expired stabilize timer
	 *
	 * @param msg the timer self-message
	 */
	virtual void handleStabilizeTimerExpired(cMessage* msg);

	/**
	 * handle an expired fixfingers timer
	 *
	 * @param msg the timer self-message
	 */
	virtual void handleCacheFlushTimerExpired(cMessage* msg);
	virtual void checkCacheInvariant();
	virtual void checkCacheSlice(OverlayKey start, OverlayKey end);

	// see BaseOverlay.h
	NodeVector* findNode(const OverlayKey& key, int numRedundantNodes, int numSiblings, BaseOverlayMessage* msg);

	// see BaseOverlay.h
	virtual void joinOverlay();

	// see BaseOverlay.h
	virtual void joinForeignPartition(const NodeHandle &node);

	// see BaseOverlay.h
	virtual bool isSiblingFor(const NodeHandle& node, const OverlayKey& key, int numSiblings, bool* err);

	// see BaseOverlay.h
	int getMaxNumSiblings();

	// see BaseOverlay.h
	int getMaxNumRedundantNodes();

	double calculateGamma();

	// see BaseOverlay.h
	virtual bool handleRpcCall(BaseCallMessage* msg);

	// see BaseOverlay.h
	virtual void handleRpcResponse(BaseResponseMessage* msg, cPolymorphic* context, int rpcId, simtime_t rtt);

	// see BaseOverlay.h
	virtual void handleRpcTimeout(BaseCallMessage* msg, const TransportAddress& dest, cPolymorphic* context, int rpcId, const OverlayKey& destKey);

	// see BaseOverlay.h
	virtual bool handleFailedNode(const TransportAddress& failed);

	// see BaseRpc.h
	virtual void pingResponse(PingResponse* pingResponse, cPolymorphic* context, int rpcId, simtime_t rtt);

	void sendFalseNegWarning(NodeHandle bestPredecessor, NodeHandle bestSuccessor, NodeVector* deadNodes);

	/**
	 * Join Remote-Procedure-Call
	 *
	 * @param call RPC Parameter Message
	 */
	virtual void rpcJoin(EpiChordJoinCall* call);

	virtual void handleRpcJoinResponse(EpiChordJoinResponse* joinResponse);

	virtual void rpcJoinAck(EpiChordJoinAckCall* joinAck);

	virtual void rpcFalseNegWarning(EpiChordFalseNegWarningCall* warning);

	/**
	 * Stabilize Remote-Procedure-Call
	 *
	 * @param call RPC Parameter Message
	 */
	void rpcStabilize(EpiChordStabilizeCall* call);

	AbstractLookup* createLookup(RoutingType routingType, const BaseOverlayMessage* msg, const cPacket* findNodeExt, bool appLookup);

	virtual void handleRpcStabilizeResponse(EpiChordStabilizeResponse* stabilizeResponse);
	virtual void handleRpcFindNodeResponse(FindNodeResponse* response);

	virtual void receiveNewNode(const NodeHandle& node, bool direct, NodeSource source, simtime_t lastUpdate);

	friend class EpiChordFingerCache;
	friend class EpiChordNodeList;
	friend class EpiChordIterativeLookup;
	friend class EpiChordIterativePathLookup;

private:
};

}; // namespace
#endif
