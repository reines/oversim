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

package oversim.tier2.broadcasttestapp;

import oversim.common.BaseApp;
import oversim.common.ITier;


//
// Simple module of the DHT test application
//
// @author Gregoire Menuel, Ingmar Baumgart
//
simple BroadcastTestApp extends BaseApp
{
	parameters:
		@class(BroadcastTestApp);
		int ttl;
		int itemsPerNode;
}


//
// Compound module for a simple DHT test application
//
// @author Gregoire Menuel, Ingmar Baumgart
//
module BroadcastTestAppModules like ITier
{
	gates:
		input from_lowerTier;	// gate from the lower tier
		input from_upperTier;	// gate from the upper tier
		input udpIn;			// gate from the UDP layer
		input trace_in;			// gate for trace file commands
		input tcpIn;			// gate from the TCP layer
		output to_lowerTier;	// gate to the lower tier
		output to_upperTier;	// gate to the upper tier
		output udpOut;			// gate to the UDP layer
		output tcpOut;			// gate to the TCP layer

	submodules:
		bcastTestApp: BroadcastTestApp;
	connections allowunconnected:
		from_lowerTier --> bcastTestApp.from_lowerTier;
		to_lowerTier <-- bcastTestApp.to_lowerTier;
		trace_in --> bcastTestApp.trace_in;
}
