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

#include <fstream>
#include <algorithm>

#include <UnderlayConfiguratorAccess.h>
#include <GlobalNodeListAccess.h>
#include <GlobalStatisticsAccess.h>
#include "NotificationBoard.h"
#include "NotifierConsts.h"

#include "GlobalBroadcast.h"

Define_Module(GlobalBroadcast);

GlobalBroadcast::GlobalBroadcast()
{
	bcastTimer = NULL;;
}

GlobalBroadcast::~GlobalBroadcast()
{
	cancelAndDelete(bcastTimer);
}

void GlobalBroadcast::initialize()
{
	underlayConfigurator = UnderlayConfiguratorAccess().get();
	globalNodeList = GlobalNodeListAccess().get();
	globalStatistics = GlobalStatisticsAccess().get();

	mean = par("testInterval");
	deviation = mean / 10;

	bcastCurrentID = 0;

	// data loading
	const char* dataFile = par("dataFile");
	if (strlen(dataFile) > 0) {
		loadData(dataFile);

		if (availableData.size() <= 0)
			throw cRuntimeError("Unable to load test data, list size is 0.");
	}

	bcastTimer = new cMessage("broadcastTimer");
	scheduleAt(simTime() + truncnormal(mean, deviation), bcastTimer);
}

void GlobalBroadcast::loadData(const char* filename)
{
	std::ifstream file;
	file.open(filename);

	if (!file.is_open())
		throw cRuntimeError("Unable to open data file.");

	std::string line;
	for (uint i = 1;!file.eof();i++) {
		getline (file, line);

		line.erase(0, line.find_first_not_of(" \n\r\t"));
		line.erase(line.find_last_not_of(" \n\r\t") + 1);

		if (line.length() < 1)
			continue;

		std::transform(line.begin(), line.end(), line.begin(), tolower);

		availableData.insert(BinaryValue(line));
	}

	file.close();
}

const BinaryValue GlobalBroadcast::getAvailableDataValue()
{
	if (availableData.size() == 0)
		return generateRandomValue(20);

	BinaryValue randomValue = generateRandomValue(20);

	// return random value in O(log n)
	std::set<BinaryValue>::iterator it = availableData.find(randomValue);

	if (it == availableData.end()) {
		it = availableData.insert(randomValue).first;
		availableData.erase(it++);
	}

	if (it == availableData.end()) {
		it = availableData.begin();
	}

	const BinaryValue data = *it;
	availableData.erase(it);

	return data;
}

const BinaryValue GlobalBroadcast::getUsedDataValue()
{
	if (usedData.size() == 0)
		throw cRuntimeError("No used data values!");

	BinaryValue randomValue = generateRandomValue(20);

	// return random value in O(log n)
	std::map<BinaryValue, uint>::iterator it = usedData.find(randomValue);

	if (it == usedData.end()) {
		it = usedData.insert(make_pair(randomValue, 0)).first;
		usedData.erase(it++);
	}

	if (it == usedData.end()) {
		it = usedData.begin();
	}

	return (*it).first;
}

void GlobalBroadcast::setUsedDataValue(BinaryValue* data)
{
	usedData[*data]++;
}

void GlobalBroadcast::setUnusedDataValue(BinaryValue* data)
{
	usedData[*data]--;

	if (usedData[*data] <= 0)
		usedData.erase(*data);
}

const BinaryValue GlobalBroadcast::generateRandomValue(uint len)
{
	char value[len + 1];

	for (uint i = 0; i < len; i++) {
		value[i] = intuniform(0, 25) + 'a';
	}

	value[len] = '\0';
	return BinaryValue(value);
}

void GlobalBroadcast::handleMessage(cMessage *msg)
{
	if (msg == bcastTimer) {
		scheduleAt(simTime() + truncnormal(mean, deviation), msg);

		// do nothing if the network is still in the initialization phase
		if (underlayConfigurator->isInInitPhase()
				|| !underlayConfigurator->isTransitionTimeFinished()
				|| underlayConfigurator->isSimulationEndingSoon()
				|| usedData.size() < 1) {
			return;
		}

		int moduleID = globalNodeList->getRandomPeerInfo(-1, -1, true)->getModuleID();

		// inform the notification board about the removal
		NotificationBoard* nb = check_and_cast<NotificationBoard*>(simulation.getModule(moduleID)->getSubmodule("notificationBoard"));
		nb->fireChangeNotification(NF_OVERLAY_BROADCAST_INIT);
	}
	else
		throw cRuntimeError("GlobalBroadcast::handleMessage(): Unknown message type!");
}

void GlobalBroadcast::finish()
{
	recordScalar("Broadcast: Count", bcastCurrentID);

	uint success = 0;
	for (int i = 0;i <= bcastCurrentID;i++) {
		SearchStatEntry* search = &globalStatistics->bcastSearch[i];
		if (search->query.isUnspecified())
			continue;

		globalStatistics->addStdDev("Broadcast: Expected", search->expected);
		globalStatistics->addStdDev("Broadcast: Actual", search->actual);
		globalStatistics->addStdDev("Broadcast: Duplicated", search->duplicated);
		globalStatistics->addStdDev("Broadcast: Extra", search->extra);
		globalStatistics->addStdDev("Broadcast: Expired", search->expired);

		if (search->success)
			success++;
	}

	recordScalar("Broadcast: Success", success);
}
