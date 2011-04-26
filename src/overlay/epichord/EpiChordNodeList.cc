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
 * @file EpiChordNodeList.cc
 * @author Jamie Furness
 * based on ChordSuccessorList.cc, by Markus Mauch and Ingmar Baumgart
 */

#include <cassert>

#include "EpiChordFingerCache.h"
#include "EpiChordNodeList.h"
#include "EpiChord.h"

namespace oversim {

Define_Module(EpiChordNodeList);

std::ostream& operator<<(std::ostream& os, const EpiChordNodeListEntry& e)
{
	os << e.nodeHandle << " " << e.newEntry;
	return os;
};

void EpiChordNodeList::initialize(int stage)
{
	// because of IPAddressResolver, we need to wait until interfaces
	// are registered, address auto-assignment takes place etc.
	if (stage != MIN_STAGE_OVERLAY)
		return;

	WATCH_MAP(nodeMap);
}

void EpiChordNodeList::handleMessage(cMessage* msg)
{
	throw new cRuntimeError("this module doesn't handle messages, it runs only in initialize()");
}

void EpiChordNodeList::initializeList(uint32_t size, NodeHandle owner, EpiChordFingerCache* cache, EpiChord *overlay, bool forwards)
{
	nodeMap.clear();
	nodeListSize = size;
	thisNode = owner;
	this->cache = cache;
	this->overlay = overlay;
	this->forwards = forwards;

	addNode(thisNode);
}

uint32_t EpiChordNodeList::getSize()
{
	return nodeMap.size();
}

bool EpiChordNodeList::isEmpty()
{
	return nodeMap.size() == 1 && getNode() == thisNode;
}

bool EpiChordNodeList::isFull()
{
	if (nodeMap.size() == 0)
		return false;

	NodeMap::iterator it = nodeMap.end();
	it--;

	return it->second.nodeHandle != thisNode;
}

const NodeHandle& EpiChordNodeList::getNode(uint32_t pos)
{
	// check boundaries
	if (pos == 0 && nodeMap.size() == 0)
		return NodeHandle::UNSPECIFIED_NODE;

	if (pos >= nodeMap.size())
		throw cRuntimeError("Index out of bound (EpiChordNodeList, getNode())");

	NodeMap::iterator it = nodeMap.begin();

	for (uint32_t i = 0; i < pos; i++) {
		it++;
		if (i == (pos - 1))
			return it->second.nodeHandle;
	}
	return it->second.nodeHandle;
}

void EpiChordNodeList::addNode(NodeHandle node, bool resize)
{
	if (node.isUnspecified())
		return;

	bool changed = false;
	OverlayKey sum = node.getKey() - thisNode.getKey();

	// If sorting backwards we need to invert the offset
	if (!forwards)
		sum = OverlayKey::ZERO - sum;

	sum -= OverlayKey::ONE;

	NodeMap::iterator it = nodeMap.find(sum);

	// Make a CommonAPI update() upcall to inform application
	// about our new neighbor in the node list

	if (it == nodeMap.end()) {
		changed = true;

		EpiChordNodeListEntry entry;
		entry.nodeHandle = node;
		entry.newEntry = true;

		nodeMap[sum] = entry;

		overlay->callUpdate(node, true);
	}
	else
		it->second.newEntry = true;

	cache->updateFinger(node, true, simTime(), 0);

	if ((resize == true) && (nodeMap.size() > (uint32_t)nodeListSize)) {
		it = nodeMap.end();
		it--;

		// If we simply removed the new node again
		if (node == it->second.nodeHandle)
			changed = false;

		overlay->callUpdate(it->second.nodeHandle, false);
		cache->setFingerTTL(it->second.nodeHandle);
		nodeMap.erase(it);
	}

	if (changed && node != thisNode)
		additions.push_back(node);
}

bool EpiChordNodeList::contains(const TransportAddress& node)
{
	for (NodeMap::iterator it = nodeMap.begin();it != nodeMap.end();it++)
		if (node == it->second.nodeHandle)
			return true;

	return false;
}

bool EpiChordNodeList::handleFailedNode(const TransportAddress& failed)
{
	assert(failed != thisNode);
	for (NodeMap::iterator it = nodeMap.begin();it != nodeMap.end();it++) {
		if (failed == it->second.nodeHandle) {
			nodeMap.erase(it);
			overlay->callUpdate(failed, false);
			cache->handleFailedNode(failed);

			// ensure that thisNode is always in the node list
			if (getSize() == 0)
				addNode(thisNode);

			return true;
		}
	}
	return false;
}

void EpiChordNodeList::removeOldNodes()
{
	NodeMap::iterator it;

	for (it = nodeMap.begin(); it != nodeMap.end();) {
		if (it->second.newEntry == false) {
			overlay->callUpdate(it->second.nodeHandle, false);
			cache->setFingerTTL(it->second.nodeHandle);
			nodeMap.erase(it++);
		}
		else {
			it->second.newEntry = false;
			it++;
		}
	}

	it = nodeMap.end();
	it--;

	while (nodeMap.size() > nodeListSize) {
		cache->setFingerTTL(it->second.nodeHandle);
		nodeMap.erase(it--);
	}

	if (getSize() == 0)
		addNode(thisNode);

	assert(!isEmpty());
}

bool EpiChordNodeList::hasChanged()
{
	return !additions.isEmpty();
}

NodeVector* EpiChordNodeList::getAdditions()
{
	return &additions;
}

void EpiChordNodeList::updateDisplayString()
{
	// FIXME: doesn't work without tcl/tk
	//		if (ev.isGUI()) {
	if (1) {
		char buf[80];

		if (nodeMap.size() == 1)
			sprintf(buf, "1 node");
		else
			sprintf(buf, "%zi nodes", nodeMap.size());

		getDisplayString().setTagArg("t", 0, buf);
		getDisplayString().setTagArg("t", 2, "blue");
	}

}

void EpiChordNodeList::updateTooltip()
{
	if (ev.isGUI()) {
		std::stringstream str;
		for (uint32_t i = 0; i < nodeMap.size(); i++)	{
			str << getNode(i);
			if (i != nodeMap.size() - 1)
				str << endl;
		}

		char buf[1024];
		sprintf(buf, "%s", str.str().c_str());
		getDisplayString().setTagArg("tt", 0, buf);
	}
}

void EpiChordNodeList::display()
{
	std::cout << "Content of EpiChordNodeList:" << endl;
	for (NodeMap::iterator it = nodeMap.begin(); it != nodeMap.end(); it++)
		std::cout << it->first << " with Node: " << it->second.nodeHandle << endl;
}

}; //namespace
