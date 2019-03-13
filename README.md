# boat_emul
various tools to emulate a boat

Here are various small programs that helps me testing OpenCPN,
the nmea2000_pi OpenCPN plugin, and canbus_autopilot.

gpsemul is a perl script that emulates a GPS moving. It needs
a .gpx (from e.g. OpenCPN) file as input to provide the starting
point, can follow a route if more than one point is provided.
It can also be driven from keyboard, allowing the user to set heading
and speed. It can provide the NEMA stream to a pseudo-tty, or TCP.

IMU_emul emulates an IMU (e,g, the one used by canbus_autopilot) and sends
attitude and rate or turn frame to the can socket. The rate of turn can
be read from stdin, it will then update the heading for each time step.

rudder_emul emulates a boat with it rudder. It takes a rudder angle (either
from stdin or the PRIVATE_COMMAND_STATUS PGN sent by the autopilot), and
computes the rate of turn for each time step. Its output can be piped to
IMU_emul's stdin. The boat speed can be set on the command line.

sea_emul computes periodic rudder angles (sinus, sqare and random)
and outputs a pseudo rudder angle to stdout, modelizing external
perturbations (sea, wind) on a boat's heading. The user can specify an
additional pseudo rudder angle on stdin. Several wave forms can be
specified on the command line, the output will be the sum of the
waves. This output can then be piped to rudder_emul's stdin.

canip is not exactly a simulator; it allows to forward can bus between
hosts over a UDP socket (e.g. to connect the chartplotter's sunxican0
interface with my PC's canlo0)
