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

#ifndef __GLOBALBROADCAST_H__
#define __GLOBALBROADCAST_H__

#include <omnetpp.h>
#include <CommonMessages_m.h>
#include <SimpleInfo.h>

class GlobalNodeList;
class GlobalStatistics;
class UnderlayConfigurator;

class GlobalBroadcast : public cSimpleModule
{
public:
	GlobalBroadcast();
	~GlobalBroadcast();

	const BinaryValue getAvailableDataValue();		// Gets an available data value
	const BinaryValue getUsedDataValue();				// Gets a used data value
	void setUsedDataValue(BinaryValue* data);	// Sets a data value as used
	void setUnusedDataValue(BinaryValue* data);	// Sets a data value as unused

	const BinaryValue generateRandomValue(uint len);

	int bcastCurrentID;

protected:
	virtual void initialize();
	virtual void handleMessage(cMessage *msg);
	virtual void finish();
	virtual void loadData(const char* filename);
private:
	UnderlayConfigurator* underlayConfigurator;
	GlobalNodeList* globalNodeList;
	GlobalStatistics* globalStatistics;

	cMessage* bcastTimer;
	cMessage* shutdownTimer;

	std::set<BinaryValue> availableData;
	std::map<BinaryValue, uint> usedData;
	double mean;
	double deviation;

	void initBroadcast();
};

#endif
