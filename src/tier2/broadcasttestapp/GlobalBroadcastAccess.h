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

#ifndef __GLOBALBROADCAST_ACCESS_H__
#define __GLOBALBROADCAST_ACCESS_H__

#include <omnetpp.h>
#include "GlobalBroadcast.h"

/**
 * Gives access to the GlobalBroadcast module.
 */
class GlobalBroadcastAccess
{
public:

	/**
	 * returns the GlobalBroadcast module
	 *
	 * @return the GlobalBroadcast module
	 */
	GlobalBroadcast* get()
	{
		return check_and_cast<GlobalBroadcast*>(
			simulation.getModuleByPath("globalObserver.globalFunctions[0].function"));
	}
};

#endif
