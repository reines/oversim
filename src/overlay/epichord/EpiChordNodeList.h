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
 * @file EpiChordNodeList.h
 * @author Jamie Furness, Markus Mauch, Ingmar Baumgart
 */

#ifndef __EPICHORDNODELIST_H_
#define __EPICHORDNODELIST_H_

#include <map>

#include <omnetpp.h>
#include <InitStages.h>
#include <NodeHandle.h>

class OverlayKey;
class EpiChordStabilizeResponse;

namespace oversim {

struct EpiChordNodeListEntry
{
	NodeHandle nodeHandle ;//*< the nodehandle
	bool newEntry;  //*< true, if this entry has just been added
};

typedef std::map<OverlayKey, EpiChordNodeListEntry> NodeMap;

class EpiChordFingerCache;
class EpiChord;

std::ostream& operator<<(std::ostream& os, const EpiChordNodeListEntry& e);


/**
 * EpiChord's node list module
 *
 * This module contains the node list of the EpiChord implementation.
 *
 * @author Jamie Furness, Markus Mauch, Ingmar Baumgart
 * @see EpiChord
 */
class EpiChordNodeList : public cSimpleModule
{
  public:
	virtual int numInitStages() const
	{
		return MAX_STAGE_OVERLAY + 1;
	}
	virtual void initialize(int stage);
	virtual void handleMessage(cMessage* msg);

	/**
	 * Initializes the node list. This should be called on startup
	 *
	 * @param size maximum number of neighbors in the node list
	 * @param owner the node owner is added to the node list
	 * @param overlay pointer to the main chord module
	 * @param forwards the direction in which to access nodes
	 */
	virtual void initializeList(uint32_t size, NodeHandle owner, EpiChordFingerCache* cache, double cacheTTL, EpiChord* overlay, bool forwards);

	/**
	 * Returns number of neighbors in the node list
	 *
	 * @return number of neighbors
	 */
	virtual uint32_t getSize();

	/**
	 * Checks if the node list is empty
	 *
	 * @return returns false if the node list contains other nodes
	 *		 than this node, true otherwise.
	 */
	virtual bool isEmpty();

	/**
	 * Checks if the node list is full.
	 * In other words, does it contain outself still
	 *
	 * @return returns false if the node list doesn't contain this node,
	 * 		true otherwise.
	 */
	virtual bool isFull();

	/**
	 * Returns a particular node
	 *
	 * @param pos position in the node list
	 * @return node at position pos
	 */
	virtual const NodeHandle& getNode(uint32_t pos = 0);

	/**
	 * Adds new nodes to the node list
	 *
	 * Adds new nodes to the node list and sorts the
	 * list using the corresponding keys. If the list size exceeds
	 * the maximum size nodes at the end of the list will be removed.
	 *
	 * @param node the node handle of the node to be added
	 * @param resize if true, shrink the list to nodeListSize
	 */
	virtual void addNode(NodeHandle node, bool resize = true);

	virtual bool contains(const TransportAddress& node);

	bool handleFailedNode(const TransportAddress& failed);

	void removeOldNodes();

	bool hasChanged();
	NodeVector* getAdditions();
	NodeVector* getRemovals();

	void display ();


  protected:
	NodeHandle thisNode; /**< own node handle */
	NodeMap nodeMap; /**< internal representation of the node list */

	NodeVector additions;
	NodeVector removals;

	uint32_t nodeListSize; /**< maximum size of the node list */
	bool forwards;
	double cacheTTL;

	EpiChordFingerCache* cache; /**< pointer to EpiChords finger cache module */
	EpiChord* overlay; /**< pointer to the main EpiChord module */

	/**
	 * Displays the current number of nodes in the list
	 */
	void updateDisplayString();

	/**
	 * Displays the first 4 nodes as tooltip.
	 */
	void updateTooltip();
};

}; //namespace
#endif
