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

//
// @author Jamie Furness
//

cplusplus {{
#include <NodeHandle.h>
#include <TransportAddress.h>
#include <CommonMessages_m.h>

static const int EPICHORDCOMMAND_L = 8;
static const int NODETYPE_L = 8;

#define EPICHORD_JOINCALL_L(msg) BASECALL_L(msg)
#define EPICHORD_JOINRESPONSE_L(msg) (BASERESPONSE_L(msg) + (msg->getSucNodeArraySize() * NODEHANDLE_L) + (msg->getPreNodeArraySize() * NODEHANDLE_L) + (msg->getCacheNodeArraySize() * NODEHANDLE_L) + (msg->getCacheLastUpdateArraySize() * TTL_L))
#define EPICHORD_JOINACKCALL_L(msg) BASECALL_L(msg)
#define EPICHORD_JOINACKRESPONSE_L(msg) BASERESPONSE_L(msg)

#define EPICHORD_FALSENEGWARNINGCALL_L(msg) (BASECALL_L(msg) + NODEHANDLE_L + (msg->getDeadNodeArraySize() * NODEHANDLE_L))
#define EPICHORD_FALSENEGWARNINGRESPONSE_L(msg) BASERESPONSE_L(msg)

#define EPICHORD_STABILIZECALL_L(msg) (BASECALL_L(msg) + (msg->getAdditionsArraySize() * NODEHANDLE_L) + (msg->getRemovalsArraySize()) + NODETYPE_L)
#define EPICHORD_STABILIZERESPONSE_L(msg) (BASERESPONSE_L(msg) + (msg->getPredecessorsArraySize() * NODEHANDLE_L) + (msg->getSuccessorsArraySize() * NODEHANDLE_L) + NODETYPE_L)

#define EPICHORD_FINDNODEEXTMESSAGE_L(msg) ((msg->getLastUpdatesArraySize() * TTL_L) + (msg->getAlreadyVisitedNodeArraySize() * NODEHANDLE_L) + (msg->getAlreadyVisitedLastUpdateArraySize() * TTL_L))
#define EPICOORD_BROADCASTINFO_L(msg) (KEY_L)
}}

class noncobject NodeHandle;
class noncobject TransportAddress;
class noncobject OverlayKey;

class BaseOverlayMessage;
class BaseCallMessage;
class BaseResponseMessage;

class noncobject EpiChordNodeType;

enum EpiChordNodeType
{
	PREDECESSOR = 0;
	SUCCESSOR = 1;
}

//
// Message used to join an EpiChord overlay
//
packet EpiChordJoinCall extends BaseCallMessage
{
}

//
// Message used to answer to a JoinCall message
//
packet EpiChordJoinResponse extends BaseResponseMessage
{
	NodeHandle sucNode[];		// successor list of the message originator
	NodeHandle preNode[];		// predecessor list of the message originator

	NodeHandle cacheNode[];		// nodes in the cache table
	simtime_t cacheLastUpdate[];	// expire times of nodes in the cache table
}

packet EpiChordJoinAckCall extends BaseCallMessage
{
}

packet EpiChordJoinAckResponse extends BaseResponseMessage
{
}

packet EpiChordFalseNegWarningCall extends BaseCallMessage
{
	NodeHandle bestPredecessor;
	NodeHandle deadNode[];
}

packet EpiChordFalseNegWarningResponse extends BaseResponseMessage
{
}

//
// Message used for the  stabilization protocol
//
packet EpiChordStabilizeCall extends BaseCallMessage
{
	NodeHandle additions[];
	NodeHandle removals[];
	int nodeType enum(EpiChordNodeType);	// Type of node we are
}

//
// Message used to answer to a StabilizeCall message
//
packet EpiChordStabilizeResponse extends BaseResponseMessage
{
	NodeHandle predecessors[];		// predecessor list of the message originator
	NodeHandle successors[];		// successor list of the message originator
	int nodeType enum(EpiChordNodeType);	// Type of node we are
}

//
// Message used to find nodes in the EpiChord overlay
//
packet EpiChordFindNodeExtMessage
{
	simtime_t lastUpdates[];		// expire times of the nodes in this findNodeResponse
	NodeHandle alreadyVisitedNode[];	// nodes we have already visited in this lookup
	simtime_t alreadyVisitedLastUpdate[];	// last update times for nodes already visited
}

packet EpiCoordBroadcastInfo
{
	OverlayKey limit;
}
