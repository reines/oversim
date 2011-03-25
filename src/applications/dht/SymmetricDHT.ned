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

package oversim.applications.dht;

import oversim.applications.dht.BaseDHT;
import oversim.applications.dht.DHTDataStorage;
import oversim.common.ITier;

//
// Simple module of the SymmetricDHT
//
// @author Jamie Furness
//
simple SymmetricDHT extends DHT
{
	parameters:
		@class(SymmetricDHT);
		int numReplicaTeams;
}

//
// Compound module for a simple SymmetricDHT using the KBR interface
//
// @author Jamie Furness
//
module SymmetricDHTModules like ITier
{
	parameters:
		@display("i=block/segm");
	gates:
		input udpIn;			// gate from the UDP layer
		input from_lowerTier;	// gate from the lower tier
		input from_upperTier;	// gate from the upper tier
		input tcpIn;			// gate from the TCP layer
		output tcpOut;			// gate to the TCP layer
		output udpOut;			// gate to the UDP layer
		output to_lowerTier;	// gate to the lower tier
		output to_upperTier;	// gate to the upper tier

	submodules:
		dht: SymmetricDHT;
		dhtDataStorage: DHTDataStorage;
	connections allowunconnected:
		from_lowerTier --> dht.from_lowerTier;
		to_lowerTier <-- dht.to_lowerTier;
		from_upperTier --> dht.from_upperTier;
		to_upperTier <-- dht.to_upperTier;
}
