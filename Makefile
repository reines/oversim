INETDIR = `pwd`/../../INET-OverSim-20101019
REASEDIR = `pwd`/../../ReaSE

BUILD_OPTIONS = -f --deep -linet -O out -o OverSim $(DEFS) -I$(INETDIR)/src/transport/tcp -I$(INETDIR)/src/applications/pingapp -I$(INETDIR)/src/networklayer/mpls -I$(INETDIR)/src/networklayer/ipv4 -I$(INETDIR)/src/linklayer/contract -I$(INETDIR)/src/linklayer/mfcore -I$(INETDIR)/src/networklayer/ldp -I$(INETDIR)/src/networklayer/ipv6 -I$(INETDIR)/src/networklayer/arp -I$(INETDIR)/src/networklayer/autorouting -I$(INETDIR)/src/applications/udpapp -I$(INETDIR)/src/networklayer/ted -I$(INETDIR)/src/networklayer/rsvp_te -I$(INETDIR)/src/util/headerserializers -I$(INETDIR)/src/networklayer/contract -I$(INETDIR)/src/transport/contract -I$(INETDIR)/src/networklayer/common -I$(INETDIR)/src/transport/sctp -I$(INETDIR)/src/transport/udp -I$(INETDIR)/src/base -I$(INETDIR)/src/networklayer/icmpv6 -I$(INETDIR)/src/world -I$(INETDIR)/src/util -L$(INETDIR)/src -L/usr/lib

ifeq "$(REASE)" "true"
	BUILD_OPTIONS += -lrease -L$(REASEDIR)/src -KINET_PROJ=$(INETDIR) -KREASE_PROJ=$(REASEDIR)
endif

BUILD_OPTIONS += -- -lgmp

all: makefiles
	cd src && $(MAKE)

clean:
	cd src && $(MAKE) clean

cleanall:
	cd src && $(MAKE) MODE=release clean
	cd src && $(MAKE) MODE=debug clean

makefiles:
	cd src && opp_makemake $(BUILD_OPTIONS)

doxy:
	doxygen doxy.cfg
	
verify:
	cd simulations && ../src/OverSim -fverify.ini -cChord | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cKoorde | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cKademlia | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cBroose | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cPastry | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cBamboo | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cKademliaInet | grep Fingerprint
	cd simulations && ../src/OverSim -fverify.ini -cChordSource | grep Fingerprint

dist: makefiles
	cd src && $(MAKE) MODE=release
	rm -rf dist
	mkdir dist
	mkdir dist/ned
	mkdir dist/ned/INET
	mkdir dist/ned/OverSim
	cp src/OverSim dist/
	cp simulations/default.ini dist/
	(cd src && cd $(INETDIR)/src && tar -cf - `find . -name '*.ned'`) | tar -xC dist/ned/INET
	(cd src && tar -cf - `find . -name '*.ned'`) | tar -xC dist/ned/OverSim
	

	
