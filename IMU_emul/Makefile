NOMAN=

PROG_CXX=boat_emul
SRCS.boat_emul= main.cpp NMEA2000.cpp nmea2000_rateofturn_tx.cpp nmea2000_rxtx.cpp nmea2000_attitude_tx.cpp

CXXFLAGS+= -std=c++11
LDFLAGS.boat_emul+= -lpthread

.include <bsd.prog.mk>

