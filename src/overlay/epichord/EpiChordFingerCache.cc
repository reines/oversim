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
 * @file EpiChordFingerCache.cc
 * @author Jamie Furness
 */

#include <cassert>

#include <GlobalNodeListAccess.h>
#include "hashWatch.h"

#include "EpiChordFingerCache.h"
#include "EpiChord.h"

namespace oversim {

Define_Module(EpiChordFingerCache);

std::ostream& operator<<(std::ostream& os, const EpiChordFingerCacheEntry& e)
{
	os << e.nodeHandle << " (added: " << e.added << ", last_update: " << e.lastUpdate << ")";
	return os;
};

void EpiChordFingerCache::initialize(int stage)
{
	// because of IPAddressResolver, we need to wait until interfaces
	// are registered, address auto-assignment takes place etc.
	if(stage != MIN_STAGE_OVERLAY)
		return;

	globalNodeList = GlobalNodeListAccess().get();

	WATCH_MAP(liveCache);
}

void EpiChordFingerCache::handleMessage(cMessage* msg)
{
	throw new cRuntimeError("this module doesn't handle messages, it runs only in initialize()");
}

void EpiChordFingerCache::initializeCache(NodeHandle owner, EpiChord* overlay, double ttl)
{
	this->overlay = overlay;
	this->ttl = ttl;
	thisNode = owner;
	liveCache.clear();
	deadCache.clear();

	successfulUpdates = 0;
}

bool EpiChordFingerCache::contains(const TransportAddress& node)
{
	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++)
		if (node == it->second.nodeHandle)
			return true;

	return false;
}

void EpiChordFingerCache::updateFinger(const NodeHandle& node, bool direct, simtime_t lastUpdate, double ttl, NodeSource source)
{
	// Trying to update an unspecified node (happens if we receive a local findNode call)
	if (node.isUnspecified() || node.getKey().isUnspecified() || node == thisNode)
		return;

	OverlayKey sum = node.getKey() - (thisNode.getKey() + OverlayKey::ONE);

	DeadMap::iterator dit = deadCache.find(sum);
	// We were alerted of a node which recently timed out for us
	if (dit != deadCache.end()) {
		// If we heard from them directly then I guess they are still alive...
		if (direct)
			deadCache.erase(dit);
		// Otherwise don't bother adding them
		else
			return;
	}

	successfulUpdates++;

	CacheMap::iterator it = liveCache.find(sum);
	if (it != liveCache.end()) {
		// Update the existing nodes added time
		if (lastUpdate < it->second.added)
			it->second.added = lastUpdate;

		// Update the existing nodes last_update time
		if (lastUpdate > it->second.lastUpdate)
			it->second.lastUpdate = lastUpdate;

		// Update the existing nodes ttl
		if (it->second.ttl > 0 && (ttl > it->second.ttl || ttl == 0))
			it->second.ttl = ttl;

		return;
	}

	// Create a new map entry
	EpiChordFingerCacheEntry entry;
	entry.nodeHandle = node;
	entry.added = lastUpdate;
	entry.lastUpdate = lastUpdate;
	entry.ttl = ttl;
	entry.source = source;

//	std::cout << simTime() << ": [" << thisNode.getKey() << "] Adding cache entry: " << entry << std::endl;
	liveCache[sum] = entry;
}

void EpiChordFingerCache::setFingerTTL(const NodeHandle& node, double ttl)
{
	// Trying to update an unspecified node (happens if we receive a local findNode call)
	if (node.isUnspecified() || node.getKey().isUnspecified())
		return;

	OverlayKey sum = node.getKey() - (thisNode.getKey() + OverlayKey::ONE);

	CacheMap::iterator it = liveCache.find(sum);
	if (it == liveCache.end())
		return;

	it->second.ttl = ttl;
}

bool EpiChordFingerCache::handleFailedNode(const TransportAddress& failed)
{
	assert(failed != thisNode);

	simtime_t now = simTime();

	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++) {
		if (failed == it->second.nodeHandle) {
			it->second.lastUpdate = now;

			deadCache[it->first] = it->second;
			liveCache.erase(it);
			return true;
		}
	}
	return false;
}

void EpiChordFingerCache::removeOldFingers()
{
	simtime_t now = simTime();

	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();) {
		if (it->second.ttl > 0 && (it->second.lastUpdate + it->second.ttl < now)) {
//			std::cout << now << ": [" << thisNode.getKey() << "] Removing live cache entry: " << it->second << std::endl;

			liveCache.erase(it++);
			continue;
		}

		it++;
	}

	for (DeadMap::iterator it = deadCache.begin();it != deadCache.end();) {
		if (it->second.lastUpdate + (it->second.ttl * 3) < now) {
//			std::cout << now << ": [" << thisNode.getKey() << "] Removing dead cache entry: " << it->second << std::endl;

			deadCache.erase(it++);
			continue;
		}

		it++;
	}
}

EpiChordFingerCacheEntry* EpiChordFingerCache::getNode(const NodeHandle& node)
{
	OverlayKey sum = node.getKey() - (thisNode.getKey() + OverlayKey::ONE);

	CacheMap::iterator it = liveCache.find(sum);
	if (it == liveCache.end())
		return NULL;

	return &it->second;
}

EpiChordFingerCacheEntry* EpiChordFingerCache::getNode(uint32_t pos)
{
	if (pos >= liveCache.size())
		return NULL;

	CacheMap::iterator it = liveCache.begin();
	std::advance(it, pos);

	return &it->second;
}

std::vector<EpiChordFingerCacheEntry> EpiChordFingerCache::getDeadRange(OverlayKey start, OverlayKey end)
{
	std::vector<EpiChordFingerCacheEntry> entries;

	start -= (thisNode.getKey() + OverlayKey::ONE);
	end -= (thisNode.getKey() + OverlayKey::ONE);

	for (DeadMap::iterator it = deadCache.lower_bound(start);it != deadCache.end();it++) {
		if (it->first > end)
			break;

		entries.push_back(it->second);
	}

	return entries;
}

uint32_t EpiChordFingerCache::countSlice(OverlayKey start, OverlayKey end)
{
	uint32_t count = 0;

	start -= (thisNode.getKey() + OverlayKey::ONE);
	end -= (thisNode.getKey() + OverlayKey::ONE);

	for (CacheMap::iterator it = liveCache.lower_bound(start);it != liveCache.end();it++) {
		if (it->first > end)
			break;

		count++;
	}

	return count;
}

bool EpiChordFingerCache::isDead(const NodeHandle& node)
{
	OverlayKey sum = node.getKey() - (thisNode.getKey() + OverlayKey::ONE);

	return deadCache.find(sum) != deadCache.end();
}

uint32_t EpiChordFingerCache::getSize()
{
	return liveCache.size();
}

uint32_t EpiChordFingerCache::countLive()
{
	return liveCache.size();
}

uint32_t EpiChordFingerCache::countRealLive()
{
	uint32_t count = 0;

	// Check all supposedly alive nodes
	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++) {
		PeerInfo* info = globalNodeList->getPeerInfo(it->second.nodeHandle);
		if (info != NULL)
			count++;
	}

	// Check all supposedly dead nodes
	for (DeadMap::iterator it = deadCache.begin();it != deadCache.end();it++) {
		PeerInfo* info = globalNodeList->getPeerInfo(it->second.nodeHandle);
		if (info != NULL)
			count++;
	}

	return count;
}

uint32_t EpiChordFingerCache::countDead()
{
	return deadCache.size();
}

uint32_t EpiChordFingerCache::countRealDead()
{
	uint32_t count = 0;

	// Check all supposedly alive nodes
	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++) {
		PeerInfo* info = globalNodeList->getPeerInfo(it->second.nodeHandle);
		if (info == NULL)
			count++;
	}

	// Check all supposedly dead nodes
	for (DeadMap::iterator it = deadCache.begin();it != deadCache.end();it++) {
		PeerInfo* info = globalNodeList->getPeerInfo(it->second.nodeHandle);
		if (info == NULL)
			count++;
	}

	return count;
}

void EpiChordFingerCache::findBestHops(OverlayKey key, NodeVector* nodes, std::vector<simtime_t>* lastUpdates, std::set<NodeHandle>* exclude, int numRedundantNodes)
{
	key -= thisNode.getKey() + OverlayKey::ONE;

	// Remove any old fingers from the cache so we don't return any expired entries
	removeOldFingers();

	// locate the node we want
	CacheMap::iterator it = liveCache.lower_bound(key);
	if (it == liveCache.end()) // This shouldn't happen!
		it = liveCache.begin();

	// Store the first node so we can detect loops
	NodeHandle* first = &it->second.nodeHandle;

	// Keep going forwards until we find an alive node
	while (exclude->find(it->second.nodeHandle) != exclude->end()) {
		it++;

		if (it == liveCache.end())
			it = liveCache.begin();

		// We have already tried this node so we must have looped right around and not found a single alive node
		if (&it->second.nodeHandle == first)
			return;
	}

	first = &it->second.nodeHandle;

	for (int i = 0;i < numRedundantNodes;) {
		// Add the node
		if (exclude->find(it->second.nodeHandle) == exclude->end()) {
			nodes->push_back(it->second.nodeHandle);
			lastUpdates->push_back(it->second.lastUpdate);
			i++;
		}

		// Check the predecessor iterator hasn't gone past the start
		if (it == liveCache.begin())
			it = liveCache.end();

		it--;

		// We have already tried this node so we must have looped right around
		if (&it->second.nodeHandle == first)
			break;
	}
}

simtime_t EpiChordFingerCache::estimateNodeLifetime(int minSampleSize)
{
	if (minSampleSize < 1)
		throw new cRuntimeError("EpiChordFingerCache::estimateNodeLifetime: minSampleSize must be > 0");

	int count = deadCache.size();
	if (count < minSampleSize)
		return 0;

	simtime_t lifetime = 0;

	for (DeadMap::iterator it = deadCache.begin();it != deadCache.end();it++)
		lifetime += (it->second.lastUpdate - it->second.added);

	return lifetime / count;
}

void EpiChordFingerCache::display()
{
	std::cout << "---------------------------" << std::endl;
	std::cout << "live: " << this->countLive() << std::endl;

	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++)
		std::cout << it->second.nodeHandle.getKey() << std::endl;

	std::cout << "dead: " << this->countDead() << std::endl;

	for (DeadMap::iterator it = deadCache.begin();it != deadCache.end();it++)
		std::cout << it->second.nodeHandle.getKey() << std::endl;

	int local = 0, observed = 0, maintenance = 0, cache_transfer = 0;
	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++) {
		switch (it->second.source) {
			case LOCAL: local++; break;
			case OBSERVED: observed++; break;
			case MAINTENANCE: maintenance++; break;
			case CACHE_TRANSFER: cache_transfer++; break;
		}
	}

	std::cout << "from local: " << local << std::endl;
	std::cout << "from observed: " << observed << std::endl;
	std::cout << "from maintenance: " << maintenance << std::endl;
	std::cout << "from cache transfer: " << cache_transfer << std::endl;
	std::cout << "---------------------------" << std::endl;
}

}; // namespace
