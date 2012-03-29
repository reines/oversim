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

/**
 * @file KademliaBucket.cc
 * @author Sebastian Mies, Ingmar Baumgart, Bernhard Heep
 */


#include "KademliaBucket.h"
#include "Kademlia.h"

KademliaBucket::KademliaBucket(Kademlia* overlay, uint16_t maxSize, const Comparator<OverlayKey>* comparator)
	: BaseKeySortedVector< KademliaBucketEntry >(maxSize, comparator)
{
	this->overlay = overlay;

    lastUsage = -1;
    managedConnections = 0;
}

KademliaBucket::~KademliaBucket()
{
}

void KademliaBucket::updateManagedConnections()
{
	std::cout << overlay->getThisNode().getKey() << ": " << this->countManagedConnections() << std::endl;

	// If all nodes in this bucket (if any) have managed connections, stop
	if (this->size() <= this->countManagedConnections())
		return;

	// If we have enough managed connections in this bucket, stop
	if (this->countManagedConnections() >= overlay->managedConnectionBucketLimit)
		return;

	// Choose which node to upgrade to a managed connection, and do so
	KademliaBucketEntry* handle = this->getOldestNode(); // TODO: Choose the "best" node, that *isn't* already a managed connection

	overlay->openManagedConnection(*handle);
}

KademliaBucketEntry* KademliaBucket::getOldestNode()
{
	if (this->isEmpty())
		return NULL;

	uint32_t oldest = 0;
	for (uint32_t i = 1;i < this->size();i++) {
		if (this->at(i).getLastSeen() < this->at(oldest).getLastSeen())
			oldest = i;
	}

	return &this->at(oldest);
}

