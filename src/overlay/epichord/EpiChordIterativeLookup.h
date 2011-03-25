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
 * @file EpiChordIterativeLookup.h
 * @author Jamie Furness
 */

#ifndef __EPICHORDITERATIVE_LOOKUP_H
#define __EPICHORDITERATIVE_LOOKUP_H

#include <IterativeLookup.h>

#include "EpiChord.h"
#include "EpiChordNodeList.h"

namespace oversim {

class EpiChordIterativeLookup : public IterativeLookup
{
	friend class EpiChordIterativePathLookup;

protected:
	EpiChord* epichord;

public:
	EpiChordIterativeLookup(BaseOverlay* overlay, RoutingType routingType, const IterativeLookupConfiguration& config, const cPacket* findNodeExt = NULL, bool appLookup = false);

	IterativePathLookup* createPathLookup();
};

class EpiChordIterativePathLookup : public IterativePathLookup
{
	friend class EpiChordIterativeLookup;

protected:
	EpiChord* epichord;

	NodeHandle bestPredecessor;
	NodeHandle bestPredecessorsSuccessor;
	NodeHandle bestSuccessor;
	NodeHandle bestSuccessorsPredecessor;

	EpiChordIterativePathLookup(EpiChordIterativeLookup* lookup, EpiChord* epichord);

	void handleResponse(FindNodeResponse* msg);
	void handleTimeout(BaseCallMessage* msg, const TransportAddress& dest, int rpcId);

	void checkFalseNegative();

	LookupEntry* getPreceedingEntry(bool incDead = false, bool incUsed = true);
	LookupEntry* getSucceedingEntry(bool incDead = false, bool incUsed = true);
	LookupEntry* getNextEntry();
};

}; //namespace

#endif
