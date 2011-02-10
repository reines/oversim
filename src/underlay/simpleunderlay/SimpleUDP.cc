//
// Copyright (C) 2000 Institut fuer Telematik, Universitaet Karlsruhe
// Copyright (C) 2004,2005 Andras Varga
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
 * @file SimpleUDP.cc
 * @author Jochen Reber
 */

//
// Author: Jochen Reber
// Rewrite: Andras Varga 2004,2005
// Modifications: Stephan Krause
//

#include <omnetpp.h>

#include <CommonMessages_m.h>
#include <GlobalNodeListAccess.h>
#include <GlobalStatisticsAccess.h>

#include <SimpleInfo.h>
#include "UDPPacket.h"
#include "SimpleUDP.h"
#include "IPControlInfo.h"
#include "IPv6ControlInfo.h"

#include "IPAddressResolver.h"

#include "IPDatagram_m.h"
#include "IPv6Datagram_m.h"


#define EPHEMERAL_PORTRANGE_START 1024
#define EPHEMERAL_PORTRANGE_END   5000


Define_Module( SimpleUDP );

std::string SimpleUDP::delayFaultTypeString;
std::map<std::string, SimpleUDP::delayFaultTypeNum> SimpleUDP::delayFaultTypeMap;

static std::ostream & operator<<(std::ostream & os,
                                 const SimpleUDP::SockDesc& sd)
{
    os << "sockId=" << sd.sockId;
    os << " appGateIndex=" << sd.appGateIndex;
    os << " userId=" << sd.userId;
    os << " localPort=" << sd.localPort;
    if (sd.remotePort!=0)
        os << " remotePort=" << sd.remotePort;
    if (!sd.localAddr.isUnspecified())
        os << " localAddr=" << sd.localAddr;
    if (!sd.remoteAddr.isUnspecified())
        os << " remoteAddr=" << sd.remoteAddr;
    if (sd.interfaceId!=-1)
        os << " interfaceId=" << sd.interfaceId;

    return os;
}

static std::ostream & operator<<(std::ostream & os,
                                 const SimpleUDP::SockDescList& list)
{
    for (SimpleUDP::SockDescList::const_iterator i=list.begin();
     i!=list.end(); ++i)
        os << "sockId=" << (*i)->sockId << " ";
    return os;
}

//--------

SimpleUDP::SimpleUDP()
{
    globalStatistics = NULL;
}

SimpleUDP::~SimpleUDP()
{

}

void SimpleUDP::initialize(int stage)
{
    if(stage == MIN_STAGE_UNDERLAY) {
        WATCH_PTRMAP(socketsByIdMap);
        WATCH_MAP(socketsByPortMap);

        lastEphemeralPort = EPHEMERAL_PORTRANGE_START;
        icmp = NULL;
        icmpv6 = NULL;

        numSent = 0;
        numPassedUp = 0;
        numDroppedWrongPort = 0;
        numDroppedBadChecksum = 0;
        numQueueLost = 0;
        numPartitionLost = 0;
        numDestUnavailableLost = 0;
        WATCH(numSent);
        WATCH(numPassedUp);
        WATCH(numDroppedWrongPort);
        WATCH(numDroppedBadChecksum);
        WATCH(numQueueLost);
        WATCH(numPartitionLost);
        WATCH(numDestUnavailableLost);

        globalNodeList = GlobalNodeListAccess().get();
        globalStatistics = GlobalStatisticsAccess().get();
        constantDelay = par("constantDelay");
        useCoordinateBasedDelay = par("useCoordinateBasedDelay");

        delayFaultTypeString = par("delayFaultType").stdstringValue();
        delayFaultTypeMap["live_all"] = delayFaultLiveAll;
        delayFaultTypeMap["live_planetlab"] = delayFaultLivePlanetlab;
        delayFaultTypeMap["simulation"] = delayFaultSimulation;

        switch (delayFaultTypeMap[delayFaultTypeString]) {
        case SimpleUDP::delayFaultLiveAll:
        case SimpleUDP::delayFaultLivePlanetlab:
        case SimpleUDP::delayFaultSimulation:
            faultyDelay = true;
            break;
        default:
            faultyDelay = false;
        }

        jitter = par("jitter");
        nodeEntry = NULL;
        WATCH_PTR(nodeEntry);
    }
}

void SimpleUDP::finish()
{
    globalStatistics->addStdDev("SimpleUDP: Packets sent",
                                numSent);
    globalStatistics->addStdDev("SimpleUDP: Packets dropped with bad checksum",
                                numDroppedBadChecksum);
    globalStatistics->addStdDev("SimpleUDP: Packets dropped due to queue overflows",
                                numQueueLost);
    globalStatistics->addStdDev("SimpleUDP: Packets dropped due to network partitions",
                                numPartitionLost);
    globalStatistics->addStdDev("SimpleUDP: Packets dropped due to unavailable destination",
                                numDestUnavailableLost);
}

void SimpleUDP::updateDisplayString()
{
    char buf[80];
    sprintf(buf, "passed up: %d pks\nsent: %d pks", numPassedUp, numSent);
    if (numDroppedWrongPort>0) {
        sprintf(buf+strlen(buf), "\ndropped (no app): %d pks", numDroppedWrongPort);
        getDisplayString().setTagArg("i",1,"red");
    }
    if (numQueueLost>0) {
        sprintf(buf+strlen(buf), "\nlost (queue overflow): %d pks", numQueueLost);
        getDisplayString().setTagArg("i",1,"red");
    }
    getDisplayString().setTagArg("t",0,buf);
}

void SimpleUDP::processUndeliverablePacket(UDPPacket *udpPacket, cPolymorphic *ctrl)
{
    numDroppedWrongPort++;
    EV << "[SimpleUDP::processUndeliverablePacket()]\n"
       << "    Dropped packet bound to unreserved port, ignoring ICMP error"
       << endl;

    delete udpPacket;
}

void SimpleUDP::processUDPPacket(UDPPacket *udpPacket)
{
    // simulate checksum: discard packet if it has bit error
    EV << "Packet " << udpPacket->getName() << " received from network, dest port " << udpPacket->getDestinationPort() << "\n";
    if (udpPacket->hasBitError())
    {
        EV << "Packet has bit error, discarding\n";
        delete udpPacket;
        numDroppedBadChecksum++;
        return;
    }

    int destPort = udpPacket->getDestinationPort();
    cPolymorphic *ctrl = udpPacket->removeControlInfo();

    // send back ICMP error if no socket is bound to that port
    SocketsByPortMap::iterator it = socketsByPortMap.find(destPort);
    if (it==socketsByPortMap.end())
    {
        EV << "No socket registered on port " << destPort << "\n";
        processUndeliverablePacket(udpPacket, ctrl);
        return;
    }
    SockDescList& list = it->second;

    int matches = 0;

    // deliver a copy of the packet to each matching socket
    cPacket *payload = udpPacket->decapsulate();
    if (dynamic_cast<IPControlInfo *>(ctrl)!=NULL)
    {
        IPControlInfo *ctrl4 = (IPControlInfo *)ctrl;
        for (SockDescList::iterator it=list.begin(); it!=list.end(); ++it)
        {
            SockDesc *sd = *it;
            if (sd->onlyLocalPortIsSet || matchesSocket(sd, udpPacket, ctrl4))
            {
                //EV << "Socket sockId=" << sd->sockId << " matches, sending up a copy.\n";
                //sendUp((cPacket*)payload->dup(), udpPacket, ctrl4, sd);
                // ib: speed hack
                if (matches == 0) {
                    sendUp((cPacket*)payload, udpPacket, ctrl4, sd);
                } else {
                    opp_error("Edit SimpleUDP.cc to support multibinding.");
                }
                matches++;
            }
        }
    }
    else if (dynamic_cast<IPv6ControlInfo *>(ctrl)!=NULL)
    {
        IPv6ControlInfo *ctrl6 = (IPv6ControlInfo *)ctrl;
        for (SockDescList::iterator it=list.begin(); it!=list.end(); ++it)
        {
            SockDesc *sd = *it;
            if (sd->onlyLocalPortIsSet || matchesSocket(sd, udpPacket, ctrl6))
            {
                //EV << "Socket sockId=" << sd->sockId << " matches, sending up a copy.\n";
                //sendUp((cPacket*)payload->dup(), udpPacket, ctrl6, sd);
                // ib: speed hack
                if (matches == 0) {
                    sendUp((cPacket*)payload, udpPacket, ctrl6, sd);
                } else {
                    opp_error("Edit SimpleUDP.cc to support multibinding.");
                }
                matches++;
            }
        }
    }
    else
    {
        error("(%s)%s arrived from lower layer without control info", udpPacket->getClassName(), udpPacket->getName());
    }

    // send back ICMP error if there is no matching socket
    if (matches==0)
    {
        EV << "None of the sockets on port " << destPort << " matches the packet\n";
        processUndeliverablePacket(udpPacket, ctrl);
        return;
    }

    delete udpPacket;
    delete ctrl;
}

void SimpleUDP::processMsgFromApp(cPacket *appData)
{
    cModule *node = getParentModule();
//    IPvXAddress ip = IPAddressResolver().addressOf(node);
//    Speedhack SK

    IPvXAddress srcAddr, destAddr;
    //cGate* destGate;

    UDPControlInfo *udpCtrl = check_and_cast<UDPControlInfo *>(appData->removeControlInfo());

    UDPPacket *udpPacket = createUDPPacket(appData->getName());

    // add header byte length for the skipped IP header
    if (udpCtrl->getDestAddr().isIPv6()) {
        udpPacket->setByteLength(UDP_HEADER_BYTES + IPv6_HEADER_BYTES);
    } else {
        udpPacket->setByteLength(UDP_HEADER_BYTES + IP_HEADER_BYTES);
    }
    udpPacket->encapsulate(appData);

    // set source and destination port
    udpPacket->setSourcePort(udpCtrl->getSrcPort());
    udpPacket->setDestinationPort(udpCtrl->getDestPort());

    /* main modifications for SimpleUDP start here */

    srcAddr = udpCtrl->getSrcAddr();
    destAddr = udpCtrl->getDestAddr();

    SimpleInfo* info = dynamic_cast<SimpleInfo*>(globalNodeList->getPeerInfo(destAddr));
    numSent++;

    if (info == NULL) {
        EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
           << "    No route to host " << destAddr
           << endl;

        delete udpPacket;
        delete udpCtrl;
        numDestUnavailableLost++;
        return;
    }

    SimpleNodeEntry* destEntry = info->getEntry();

    // calculate delay
    simtime_t totalDelay = 0;
    if (srcAddr != destAddr) {
        SimpleNodeEntry::SimpleDelay temp;
        if (faultyDelay) {
            SimpleInfo* thisInfo = static_cast<SimpleInfo*>(globalNodeList->getPeerInfo(srcAddr));
            temp = nodeEntry->calcDelay(udpPacket, *destEntry,
                                        !(thisInfo->getNpsLayer() == 0 ||
                                          info->getNpsLayer() == 0)); //TODO
        } else {
            temp = nodeEntry->calcDelay(udpPacket, *destEntry);
        }
        if (useCoordinateBasedDelay == false) {
            totalDelay = constantDelay;
        } else if (temp.second == false) {
            EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
               << "    Send queue full: packet " << udpPacket << " dropped"
               << endl;
            delete udpCtrl;
            delete udpPacket;
            numQueueLost++;
            return;
        } else {
            totalDelay = temp.first;
        }
    }

    SimpleInfo* thisInfo = dynamic_cast<SimpleInfo*>(globalNodeList->getPeerInfo(srcAddr));

    if (!globalNodeList->areNodeTypesConnected(thisInfo->getTypeID(), info->getTypeID())) {
        EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
                   << "    Partition " << thisInfo->getTypeID() << "->" << info->getTypeID()
                   << " is not connected"
                   << endl;
        delete udpCtrl;
        delete udpPacket;
        numPartitionLost++;
        return;
    }

    if (jitter) {
        // jitter
        //totalDelay += truncnormal(0, SIMTIME_DBL(totalDelay) * jitter);

        //workaround (bug in truncnormal(): sometimes returns inf)
        double temp = truncnormal(0, SIMTIME_DBL(totalDelay) * jitter);
        while (temp == INFINITY || temp != temp) { // reroll if temp is INF or NaN
            std::cerr << "\n******* SimpleUDP: truncnormal() -> inf !!\n"
                      << std::endl;
            temp = truncnormal(0, SIMTIME_DBL(totalDelay) * jitter);
        }

        totalDelay += temp;
    }

    BaseOverlayMessage* temp = NULL;

    if (ev.isGUI() && udpPacket->getEncapsulatedPacket()) {
        if ((temp = dynamic_cast<BaseOverlayMessage*>(udpPacket
                ->getEncapsulatedPacket()))) {
            switch (temp->getStatType()) {
            case APP_DATA_STAT:
                udpPacket->setKind(1);
                break;
            case APP_LOOKUP_STAT:
                udpPacket->setKind(2);
                break;
            case MAINTENANCE_STAT:
            default:
                udpPacket->setKind(3);
            }
        } else {
            udpPacket->setKind(1);
        }
    }

    EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
       << "    Packet " << udpPacket << " sent with delay = " << totalDelay
       << endl;

    //RECORD_STATS(globalStatistics->addStdDev("SimpleUDP: delay", totalDelay));

    /* main modifications for SimpleUDP end here */

    if (!udpCtrl->getDestAddr().isIPv6()) {
        // send to IPv4
        //EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
        //<< "    Sending app packet " << appData->getName() << " over IPv4"
        //<< endl;
        IPControlInfo *ipControlInfo = new IPControlInfo();
        ipControlInfo->setProtocol(IP_PROT_UDP);
        ipControlInfo->setSrcAddr(srcAddr.get4());
        ipControlInfo->setDestAddr(destAddr.get4());
        ipControlInfo->setInterfaceId(udpCtrl->getInterfaceId());
        udpPacket->setControlInfo(ipControlInfo);
        delete udpCtrl;

        // send directly to IPv4 gate of the destination node
        sendDirect(udpPacket, totalDelay, 0, destEntry->getUdpIPv4Gate());

    } else {
        // send to IPv6
        //EV << "[SimpleUDP::processMsgFromApp() @ " << IPAddressResolver().addressOf(node) << "]\n"
        //<< "    Sending app packet " << appData->getName() << " over IPv6"
        //<< endl;
        IPv6ControlInfo *ipControlInfo = new IPv6ControlInfo();
        ipControlInfo->setProtocol(IP_PROT_UDP);
        ipControlInfo->setSrcAddr(srcAddr.get6());
        ipControlInfo->setDestAddr(destAddr.get6());
        ipControlInfo->setInterfaceId(udpCtrl->getInterfaceId()); //FIXME extend IPv6 with this!!!
        udpPacket->setControlInfo(ipControlInfo);
        delete udpCtrl;

        // send directly to IPv4 gate of the destination node
        sendDirect(udpPacket, totalDelay, 0, destEntry->getUdpIPv6Gate());
    }

}


void SimpleUDP::setNodeEntry(SimpleNodeEntry* entry)
{
    nodeEntry = entry;
}
