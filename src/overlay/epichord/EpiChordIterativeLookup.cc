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
 * @file EpiChordIterativeLookup.cc
 * @author Jamie Furness
 */

#include <BaseOverlay.h>

#include "EpiChordIterativeLookup.h"

namespace oversim {

EpiChordIterativeLookup::EpiChordIterativeLookup(BaseOverlay* overlay, RoutingType routingType, const IterativeLookupConfiguration& config, const cPacket* findNodeExt, bool appLookup) : IterativeLookup::IterativeLookup(overlay, routingType, config, findNodeExt, appLookup)
{
	epichord = static_cast<EpiChord*>(overlay);
}

IterativePathLookup* EpiChordIterativeLookup::createPathLookup()
{
    return new EpiChordIterativePathLookup(this, epichord);
}

EpiChordIterativePathLookup::EpiChordIterativePathLookup(EpiChordIterativeLookup* lookup, EpiChord* epichord) : IterativePathLookup(lookup)
{
	this->nextHops = LookupVector(lookup->config.redundantNodes * lookup->config.redundantNodes, lookup);
	this->epichord = epichord;

	bestPredecessor = epichord->getThisNode();
	bestPredecessorsSuccessor = epichord->successorList->getNode();
	bestSuccessor = epichord->getThisNode();
	bestSuccessorsPredecessor = epichord->predecessorList->getNode();
}

void EpiChordIterativePathLookup::checkFalseNegative()
{
	// If we have success then we don't have a negative at all :)
	if (success)
		return;

	LookupEntry* preceedingEntry = getPreceedingEntry();
	LookupEntry* succeedingEntry = getSucceedingEntry();

	if (preceedingEntry == NULL || succeedingEntry == NULL)
		return;

	// Check that we have visited the closest surrounding nodes
	if (!lookup->getVisited(preceedingEntry->handle) || !lookup->getVisited(succeedingEntry->handle))
		return;

	bool falseNegative = false;

	// One of the 2 nodes has outdated successor/predecessor - this is a false negative
	if (bestSuccessor == bestPredecessorsSuccessor || bestPredecessor == bestSuccessorsPredecessor) {
		falseNegative = true;
	}
	// There is only 1 node in between, and it is dead (or only just joined/not ready) - this is a false negative
	else if (bestPredecessorsSuccessor == bestSuccessorsPredecessor) {
		falseNegative = true;
	}
	// both nodes have dead blockers, but there could be alive in the middle
	else if (lookup->getDead(bestPredecessorsSuccessor) && lookup->getDead(bestSuccessorsPredecessor)) {
		// TODO: Maybe?
	}

	// If this isn't a false negative, do nothing
	if (!falseNegative)
		return;

	NodeVector* deadNodes = new NodeVector();

	// Find all dead nodes
	for (LookupVector::iterator it = nextHops.begin();it != nextHops.end();it++) {
		if (lookup->getDead(it->handle))
			deadNodes->push_back(it->handle);
	}

	// There is no dead nodes inbetween the 2 best options - this shouldn't happen!
	if (deadNodes->isEmpty())
		return;

	epichord->sendFalseNegWarning(bestPredecessor, bestSuccessor, deadNodes);
	delete deadNodes;

	lookup->addSibling(bestSuccessor);

	finished = true;
	success = true;
}

void EpiChordIterativePathLookup::handleResponse(FindNodeResponse* msg)
{
	if (finished)
		return;

	if (msg->getClosestNodesArraySize() > 0) {
		NodeHandle source = msg->getSrcNode();

		// This is the best predecessor so far
		if (source.getKey().isBetween(bestPredecessor.getKey(), lookup->getKey())) {
			bestPredecessor = source;
			bestPredecessorsSuccessor = msg->getClosestNodes(0);
		}
		// This is the best successor so far
		else if (source.getKey().isBetween(lookup->getKey(), bestSuccessor.getKey())) {
			bestSuccessor = source;
			bestSuccessorsPredecessor = msg->getClosestNodes(0);
		}
	}

	IterativePathLookup::handleResponse(msg);

	// The lookup isn't finished, but the response was
	// negative so check if it was a false-negative.
	checkFalseNegative();
}

void EpiChordIterativePathLookup::handleTimeout(BaseCallMessage* msg, const TransportAddress& dest, int rpcId)
{
	if (finished)
		return;

	IterativePathLookup::handleTimeout(msg, dest, rpcId);

	// The lookup isn't finished, but a node timed out
	// so check if a previous response was a false-negative.
	checkFalseNegative();
}

LookupEntry* EpiChordIterativePathLookup::getPreceedingEntry(bool incDead, bool incUsed)
{
	// First look for the closest node before the key
	LookupEntry* entry = NULL;
	OverlayKey maxDistance = 0;

	for (LookupVector::iterator it = nextHops.begin();it != nextHops.end();it++) {
		if (!incDead && lookup->getDead(it->handle))
			continue;

		if (!incUsed && it->alreadyUsed)
			continue;

		OverlayKey distance = KeyUniRingMetric().distance(lookup->getKey(), it->handle.getKey());
		if (distance <= maxDistance)
			continue;

		maxDistance = distance;
		entry = &(*it);
	}

	return entry;
}

LookupEntry* EpiChordIterativePathLookup::getSucceedingEntry(bool incDead, bool incUsed)
{
	// First look for the closest node after the key
	LookupEntry* entry = NULL;
	OverlayKey minDistance = OverlayKey::getMax();

	for (LookupVector::iterator it = nextHops.begin();it != nextHops.end();it++) {
		if (!incDead && lookup->getDead(it->handle))
			continue;

		if (!incUsed && it->alreadyUsed)
			continue;

		OverlayKey distance = KeyUniRingMetric().distance(lookup->getKey(), it->handle.getKey());
		if (distance >= minDistance)
			continue;

		minDistance = distance;
		entry = &(*it);
	}

	return entry;
}

LookupEntry* EpiChordIterativePathLookup::getNextEntry()
{
	// First look for the closest node after the key
	LookupEntry* entry = getSucceedingEntry(false, true);

	// If the closest alive node after the key isn't checked, use it
	if (entry != NULL && !entry->alreadyUsed)
		return entry;

	// Otherwise simply look for the closest alive not used node
	return IterativePathLookup::getNextEntry();
}

}; //namespace
