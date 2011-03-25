#ifndef __SYMMETRICDHT_H_
#define __SYMMETRICDHT_H_

#include <omnetpp.h>

#include "DHT.h"

class SymmetricDHT : public DHT
{
protected:
	int numReplicaTeams;
	OverlayKey overlayKeyOffset;

	void initializeDHT();
	void handlePutCAPIRequest(DHTputCAPICall* capiPutMsg);
	void handleGetCAPIRequest(DHTgetCAPICall* capiPutMsg);
};

#endif
