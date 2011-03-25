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

	// Add ourselves to the cache with an expire time of 0
	updateFinger(owner, true, simTime(), 0);
}

bool EpiChordFingerCache::contains(const TransportAddress& node)
{
	for (CacheMap::iterator it = liveCache.begin();it != liveCache.end();it++)
		if (node == it->second.nodeHandle)
			return true;

	return false;
}

void EpiChordFingerCache::updateFinger(const NodeHandle& node, bool direct, simtime_t lastUpdate, double ttl)
{
	// Trying to update an unspecified node (happens if we receive a local findNode call)
	if (node.isUnspecified() || node.getKey().isUnspecified())
		return;

	OverlayKey sum = node.getKey() - (thisNode.getKey() + OverlayKey::ONE);

	CacheMap::iterator it = deadCache.find(sum);
	// We were alerted of a node which recently timed out for us
	if (it != deadCache.end()) {
		// If we heard from them directly then I guess they are still alive...
		if (direct)
			deadCache.erase(it);
		// Otherwise don't bother adding them
		else
			return;
	}

	successfulUpdates++;

	simtime_t now = simTime();

	it = liveCache.find(sum);
	if (it != liveCache.end()) {
		// Update the existing nodes added time
		if (lastUpdate < it->second.added)
			it->second.added = lastUpdate;

		// Update the existing nodes last_update time
		if (lastUpdate > it->second.lastUpdate)
			it->second.lastUpdate = lastUpdate;

		// Update the existing nodes ttl
		if (it->second.ttl > 0 && ttl > it->second.ttl)
			it->second.ttl = ttl;

		return;
	}

	// Create a new map entry
	EpiChordFingerCacheEntry entry;
	entry.nodeHandle = node;
	entry.added = lastUpdate;
	entry.lastUpdate = lastUpdate;
	entry.ttl = ttl;

//	std::cout << simTime() << ": [" << thisNode.getKey() << "] Adding cache entry: " << entry << std::endl;
	liveCache[sum] = entry;
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

	for (CacheMap::iterator it = deadCache.begin();it != deadCache.end();) {
		if (it->second.lastUpdate + (it->second.ttl * 3) < now) {
//			std::cout << now << ": [" << thisNode.getKey() << "] Removing dead cache entry: " << it->second << std::endl;

			deadCache.erase(it++);
			continue;
		}

		it++;
	}
}

EpiChordFingerCacheEntry EpiChordFingerCache::getNode(uint32_t pos)
{
	if (pos >= liveCache.size())
		throw cRuntimeError("Index out of bound (EpiChordFingerCache, getNode())");

	CacheMap::iterator it = liveCache.begin();
	for (uint32_t i = 0; i < pos; i++) {
		it++;
		if (i == (pos - 1))
			return it->second;
	}
	return it->second;
}

uint32_t EpiChordFingerCache::countSlice(OverlayKey start, OverlayKey end)
{
	uint32_t count = 0;

	start -= thisNode.getKey() + OverlayKey::ONE;
	end -= thisNode.getKey() + OverlayKey::ONE;

	for (CacheMap::iterator it = liveCache.lower_bound(start);it != liveCache.end();it++) {
		if (it->first >= end)
			break;

		count++;
	}

	return count;
}

uint32_t EpiChordFingerCache::getSize()
{
	return liveCache.size();
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

	for (CacheMap::iterator it = deadCache.begin();it != deadCache.end();it++)
		lifetime += (it->second.lastUpdate - it->second.added);

	return lifetime / count;
}


}; // namespace
