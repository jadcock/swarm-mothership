all:  ptest pi2cface #piface

#piface: piface.cpp protocol.cpp
CXXFLAGS=-I../mothership_avr -Wall -Werror -pedantic
pi2cface: pi2cface.cpp ../mothership_avr/protocol.cpp joystick.cpp systemtime.cpp pfsm.cpp

ptest: ptest.cpp ../mothership_avr/protocol.cpp

clean:
	@rm -f pi2cface ptest *.o
