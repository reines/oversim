#ifndef __REPEATEDHASHINGDHT_H_
#define __REPEATEDHASHINGDHT_H_

#include <omnetpp.h>

#include "DHT.h"

class RepeatedHashingDHT : public DHT
{
protected:
	int numReplicaTeams;

	void initializeDHT();
	void handlePutCAPIRequest(DHTputCAPICall* capiPutMsg);
	void handleGetCAPIRequest(DHTgetCAPICall* capiPutMsg);
};

#endif
