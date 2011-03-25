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

//
// @author Jamie Furness
//

package oversim.tier2.broadcasttestapp;

import oversim.common.IGlobalFunctions;

//
// Module to start broadcasts from any node
//
simple GlobalBroadcast like IGlobalFunctions
{
	parameters:
		double testInterval @unit(s);	// time interval for DHT operations (put/get/modify)
		string dataFile;	// file containg test data, 1 item per line
}
