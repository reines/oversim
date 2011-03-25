/*
 * BroadcastInfo.h
 *
 *  Created on: Dec 14, 2009
 *      Author: Jamie Furness
 */

#ifndef BROADCASTINFO_H_
#define BROADCASTINFO_H_

class BroadcastInfo {
public:
	inline BroadcastInfo(NodeHandle node, OverlayKey key, cPacket* info) { this->node = node; this->key = key; this->info = info; }

	NodeHandle node;
	OverlayKey key;
	cPacket* info;
};

#endif /* BROADCASTINFO_H_ */
