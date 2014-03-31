//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#ifndef __BROADCASTTESTAPP_H_
#define __BROADCASTTESTAPP_H_

#include <omnetpp.h>

#include <BaseApp.h>
#include <INotifiable.h>
#include <OverlayKey.h>
#include <DHTDataStorage.h>
#include <BroadcastInfo.h>

class NotificationBoard;
class BaseApp;
class GlobalNodeList;
class GlobalBroadcast;
class DHTDataStorage;
class GlobalBroadcast;

struct SearchInfoEntry
{
	bool started;
	OverlayKey receivedFrom;

	SearchInfoEntry()
	{
		started = false;
		receivedFrom = OverlayKey::UNSPECIFIED_KEY;
	}
};

typedef std::map<uint32_t, BinaryValue> PendingData;
typedef std::map<uint32_t, SearchInfoEntry> SearchInfo;

class BroadcastTestApp : public BaseApp {
private:
	GlobalBroadcast* globalBroadcast;
	DHTDataStorage* storage;
	PendingData pendingData;
	SearchInfo searches;

	cMessage* initTimer;
	cMessage* bcastTimer;

	int initFinished;
	int numMessages;
	int numBytes;

	void initBroadcast();
public:
	BroadcastTestApp();
	~BroadcastTestApp();
protected:
	void initializeApp(int stage);
	void handleTimerEvent(cMessage *msg);
	void handleReadyMessage(CompReadyMessage* msg);
	void receiveChangeNotification(int category, const cPolymorphic *details);
	bool handleRpcCall(BaseCallMessage* msg);
	void handleRpcResponse(BaseResponseMessage* msg, cPolymorphic* context, int rpcId, simtime_t rtt);
	void handleRpcTimeout(BaseCallMessage* msg, const TransportAddress& dest, cPolymorphic* context, int rpcId, const OverlayKey& destKey);

	void rpcBroadcastRequest(BroadcastRequestCall* broadcastRequestCall);
	void rpcBroadcastResponse(BroadcastResponseCall* broadcastResponseCall);
	void finishApp();

	NotificationBoard* notificationBoard;	   /**< pointer to NotificationBoard in this node */
	int ttl;
	int perNode;
	int branchingFactor;
};

#endif
