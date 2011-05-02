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
 * @file EpiChordFingerCache.h
 * @author Jamie Furness
 */

#ifndef __EPICHORDFINGERCACHE_H_
#define __EPICHORDFINGERCACHE_H_

#include <map>

#include <omnetpp.h>
#include <GlobalNodeList.h>
#include <NodeVector.h>
#include <InitStages.h>

class BaseOverlay;

namespace oversim {

enum NodeSource { OBSERVED, LOCAL, MAINTENANCE, CACHE_TRANSFER };

struct EpiChordFingerCacheEntry
{
	NodeHandle nodeHandle;
	simtime_t lastUpdate;
	simtime_t added;
	double ttl;
	NodeSource source;
};

typedef std::map<OverlayKey, EpiChordFingerCacheEntry> CacheMap;
typedef std::map<TransportAddress, EpiChordFingerCacheEntry> DeadMap;

class EpiChord;

std::ostream& operator<<(std::ostream& os, const EpiChordFingerCacheEntry& e);

/**
 * EpiChord's finger cache module
 *
 * This module contains the finger cache of the EpiChord implementation.
 *
 * @author Jamie Furness
 * @see EpiChord
 */
class EpiChordFingerCache : public cSimpleModule
{
public:

	virtual int numInitStages() const
	{
		return MAX_STAGE_OVERLAY + 1;
	}

	virtual void initialize(int stage);
	virtual void handleMessage(cMessage* msg);

	/**
	 * Sets up the finger cache
	 *
	 * Sets up the finger cache and makes all fingers as empty.
	 * Should be called on startup to initialize the finger cache.
	 *
	 * @param overlay pointer to the main chord module
	 */
	virtual void initializeCache(NodeHandle owner, EpiChord* overlay, double ttl);

	virtual void updateFinger(const NodeHandle& node, bool direct, NodeSource source, simtime_t lastUpdate = simTime()) { updateFinger(node, direct, lastUpdate, ttl, source); }
	virtual void updateFinger(const NodeHandle& node, bool direct, simtime_t lastUpdate, double ttl, NodeSource source);

	virtual void setFingerTTL(const NodeHandle& node) { setFingerTTL(node, ttl); }
	virtual void setFingerTTL(const NodeHandle& node, double ttl);

	bool handleFailedNode(const TransportAddress& failed);

	void removeOldFingers();

	EpiChordFingerCacheEntry* getNode(const NodeHandle& node);
	EpiChordFingerCacheEntry* getNode(uint32_t pos);

	uint32_t countSlice(OverlayKey startOffset, OverlayKey endOffset);

	virtual uint32_t getSize();
	virtual uint32_t countLive();
	virtual uint32_t countRealLive();
	virtual uint32_t countDead();
	virtual uint32_t countRealDead();

	virtual int getSuccessfulUpdates() { return successfulUpdates; }

	void findBestHops(OverlayKey key, NodeVector* nodes, std::vector<simtime_t>* lastUpdates, std::set<NodeHandle>* exclude, int numRedundantNodes);

	simtime_t estimateNodeLifetime(int minSampleSize = 5);

	virtual bool contains(const TransportAddress& node);
	virtual void display();

protected:
	CacheMap liveCache;
	DeadMap deadCache;
	NodeHandle thisNode;
	EpiChord* overlay;
	double ttl;
	int successfulUpdates;
	GlobalNodeList* globalNodeList;
};

}; // namespace

#endif
