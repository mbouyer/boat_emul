#!/usr/pkg/bin/perl

# Copyright (c) 2019, Manuel Bouyer
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# 
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

use strict;
use IO::Socket::INET;
use IO::Select;
use Geo::Gpx;
use Geo::Ellipsoid;
use DateTime;
use Time::HiRes;

use Socket qw(SOL_SOCKET SO_KEEPALIVE);
use Getopt::Std;

our($opt_s, $opt_r, $opt_l, $opt_p, $opt_t);
getopts("s:r:l:p:t") or usage();
usage() unless  (@ARGV == 1);

if (!defined($opt_s)) {
	$opt_s = 1;
}
if (!defined($opt_r)) {
	$opt_r = 1;
}
if (!defined($opt_l)) {
	$opt_l = 0;
}
if (!defined($opt_p)) {
	$opt_p = 2223;
}


# Open the GPX file
open my $fh_in, '<', $ARGV[0] or die "can't open xml file: $@";
# Parse GPX
my $gpx = Geo::Gpx->new( input => $fh_in );
# Close the GPX file
close $fh_in;

$SIG{'PIPE'} = sub {  };

#my $gpx = Geo::Gpx->new( xml => $ARGV[0] ) or die "can't open xml file: $_";

my $iter = $gpx->iterate_points();
my $pt = $iter->();
my $next_pt = $iter->();
my $to_pt = 0;
my $auto_pt = 0;
if ($next_pt) {
	$to_pt = 1;
	$auto_pt = 1;
}

my $clientssock = new IO::Socket::INET (
	LocalPort => $opt_p,
	Proto => 'tcp',
	Listen => 16,
	Reuse => 1,
);
die "Could not create network socket: $@\n" unless $clientssock;


my $read_set = new IO::Select();
$read_set->add($clientssock);

my $pty;
my @clients;
if ($opt_t) {
	require IO::Pty;
	$pty = new IO::Pty or die "can't create pty: $@\n";
	$pty->set_raw;
	$pty->slave->set_raw;
	#$read_set->add($pty);
	push @clients, $pty;
	printf STDOUT "using pty " . $pty->ttyname . "\n";
	$pty->close_slave;
}
	
$read_set->add(\*STDIN);


my $previous_lat = $pt->{lat};
my $previous_lon = $pt->{lon};
my $previous_time = Time::HiRes::time();

my $heading = 0;
my $speed = 0;

my  $ellips = Geo::Ellipsoid->new(
	ellipsoid=>'WGS84',
	units=>'degrees',
	distance_units => 'nm',
	longitude => 1,
	bearing => 0,
    ) or die "can't create ellips: $@\n";

show_prompt();
while (1) {
        my ($rh_set) = IO::Select->select($read_set, undef, undef, 1 / $opt_r);
        foreach my $rh (@$rh_set) {
		if ($rh == $clientssock) {
			# new client
			my $ns = $rh->accept();
			printf STDERR "new client " . $ns->peerhost() .
			    ":" . $ns->peerport() . "\n";
			setsockopt($ns, SOL_SOCKET, SO_KEEPALIVE, 1)
				or print STDERR "can't set keepalive: $!\n";
			push @clients, $ns;
			$read_set->add($ns);
		} else {
			#client data or socket close ?
			do_client($rh);
		}
	}
	my $delta_time = Time::HiRes::time() - $previous_time;
	$previous_time = Time::HiRes::time();
	if ($delta_time >= 1 / $opt_r) {
		if ($to_pt) {
			# compute heading to next point from current pos
			my ($distance, $new_heading) = $ellips->to(
			    $previous_lat, $previous_lon,
			    $next_pt->{lat}, $next_pt->{lon});
			$heading = $new_heading;
			# compute speed, based on average segment speed
			my $seg_delta_time = $next_pt->{time} - $pt->{time};
			$distance = $ellips->to(
			    $pt->{lat}, $pt->{lon},
			    $next_pt->{lat}, $next_pt->{lon});
			if ($seg_delta_time <= 0 && $opt_l > 0) {
				$speed = $opt_l * $opt_s;
			} else {
				$speed = $distance / $seg_delta_time * 3600 * $opt_s;
			}
			$to_pt = 0;
		}

		# compute new position
		my ($new_lat, $new_lon) = $ellips->at(
			$previous_lat, $previous_lon,
			$speed * $delta_time / 3600, $heading);
		gps_message($new_lat, $new_lon, $speed, $heading, time());

			
		if ($next_pt && $auto_pt) {
			# see if it's time to change to next point
			my $previous_pos_to_target = $ellips->to(
				$previous_lat, $previous_lon,
				$next_pt->{lat}, $next_pt->{lon}); 
			my $current_pos_to_target = $ellips->to(
				$new_lat, $new_lon,
				$next_pt->{lat}, $next_pt->{lon}); 
			if ($previous_pos_to_target < $current_pos_to_target) {
				$pt = $next_pt;
				$next_pt = $iter->();
				if ($next_pt) {
					$to_pt = 1;
				} else {
					if ($opt_l > 0) {
						$iter = $gpx->iterate_points();
						$next_pt = $iter->();
						$to_pt = 1;
					} else {
						$speed = 0;
					}
				}
			}
		}
		$previous_lat = $new_lat;
		$previous_lon = $new_lon;
	}
}


sub show_prompt {
	if ($opt_t) {
		printf STDOUT $pty->ttyname . " ";
	}
	printf STDOUT 'C{wp,nwp,s[+- ]<n>,h[+- ]<n>} >:';
	flush STDOUT;
}

sub gps_message {
	my ($lat, $lon, $speed, $heading, $time) = @_;
	my $ns;
	my $eo;

	if ($lat > 0) {
		$ns = "N";
	} else {
		$ns = "S";
		$lat = -$lat;
	}
	if ($lon > 0) {
		$eo = "E";
	} else {
		$eo = "W";
		$lon = -$lon;
	}
	$lat = todeg($lat);
	$lon = todeg($lon);
	my $dt = DateTime->from_epoch(epoch => $time);
	my $msg = 'GPGGA,' . $dt->hms('');
	$msg = $msg . "," . $lat . "," . $ns . "," . $lon . "," . $eo;
	$msg = $msg . ",1,08,5.6,0.6,M,34.5,M,,";
	sendtoclients($msg);
	$msg = 'GPRMC,' . $dt->hms('') . ",A";
	$msg = $msg . "," . $lat . "," . $ns . "," . $lon . "," . $eo;
	$msg = $msg . "," . todot($speed) . "," . todot($heading);
	$msg = $msg . "," . $dt->strftime("%d%m%g");
	$msg = $msg . ",5,E,A";
	sendtoclients($msg);
	$msg = 'GPGSV,8,1,32,01,83,091,48,02,82,308,47,03,41,261,11,04,81,281,47';
	sendtoclients($msg);
	$msg = 'GPGSV,8,2,32,05,26,093,-4,06,06,045,-24,07,02,140,00,08,53,225,23';
	sendtoclients($msg);
	$msg = 'GPGSV,8,3,32,09,37,129,07,10,52,053,22,11,35,056,05,12,45,052,16';
	sendtoclients($msg);
	$msg = 'GPGSV,8,4,32,13,31,013,01,14,42,133,13,15,79,084,45,16,75,164,43';
	sendtoclients($msg);
	$msg = 'GPGSV,8,5,32,17,23,112,-7,18,03,161,00,19,50,106,21,20,89,319,51';
	sendtoclients($msg);
	$msg = 'GPGSV,8,6,32,21,59,129,29,22,81,277,47,23,02,166,00,24,88,045,51';
	sendtoclients($msg);
	$msg = 'GPGSV,8,7,32,25,81,128,47,26,39,082,10,27,05,256,00,28,35,061,05';
	sendtoclients($msg);
	$msg = 'GPGSV,8,8,32,29,42,290,13,30,32,337,02,31,16,331,-15,32,28,193,-2';
	sendtoclients($msg);
}

sub todeg {
	my ($val) = @_;
	my $int_val = int($val);
	my $result = sprintf "%02d", $int_val;
	$val = $val - $int_val;
	$val = $val * 60;
	$int_val = int($val);
	$result = $result . sprintf "%02d", $int_val;
	$val = $val - $int_val;
	$val = $val * 10000;
	$int_val = int($val);
	$result = $result . "." . sprintf "%04d", $int_val;
	return $result;
}

sub todot {
	my ($val) = @_;
	my $int_val = int($val);
	my $result = sprintf "%03d", $int_val;
	$val = $val - $int_val;
	$val = $val * 10;
	$int_val = int($val);
	$result = $result . sprintf ".%1d", $int_val;
	return $result;
}


sub checksum {
	my ($string) = @_;
	my $v = 0;
	$v ^= $_ for unpack 'C*', $string;
	sprintf '%02X', $v;
}

sub do_client {
        my ($ns) = @_;

        my $buf = <$ns>;
        if ($buf) {
		chomp $buf;
		if ($buf =~ /^C(.*)$/) {
			my @cmds = split(' ', $1);
			while (my $cmd = shift @cmds) {
				if ($cmd =~ /^s([+-]?[\d.]+)$/) {
					$speed = do_abs_or_rel($speed, $1);
					if ($speed < 0) {
						$speed = 0;
					}
				} elsif ($cmd =~ /^h([+-]?[\d.]+)$/) {
					$heading = do_abs_or_rel($heading, $1);
					if ($heading < 0) {
						$heading += 360;
					} elsif ($heading > 359) { 
						$heading -= 360;
					}
					$auto_pt = 0;
				} elsif ($cmd =~ /^wp$/) {
					if ($next_pt) {
						$to_pt = 1;
						$auto_pt = 1;
					}
				} elsif ($cmd =~ /^nwp$/) {
					$pt = $next_pt;
					$next_pt = $iter->();
					if ($next_pt) {
						$to_pt = 1;
						$auto_pt = 1;
					}
				} else {
					printf STDERR "unkown cmd: " . $cmd . "\n";
				}
			}
		}
		show_prompt();
        } else {
		if ($opt_t && $pty == $ns) {
			printf STDERR "tty closed closed\n";
		} else {
			printf STDERR "client " . $ns->peerhost() . " closed\n";
		}
                $read_set->remove($ns);
                @clients = grep { $_ != $ns } @clients;
                close ($ns);
        }
}

sub do_abs_or_rel {
	my ($old_val, $buf) = @_;
	if ($buf =~ /^([\d.]+)$/ ) {
		return $1;
	}
	if ($buf =~ /^([+-][\d.]+)$/ ) {
		return $old_val + $1;
	}
	return $old_val;
}


#send a message to all network clients
sub sendtoclients {
        my ($cmd) = @_;

	my $sum = checksum($cmd);

	$cmd = '$' . $cmd . "*" . $sum . "\n";

	#print $cmd;

        foreach my $c (@clients) {
		if ($opt_t && $pty == $c) {
			print $c $cmd . "\r\n";
		} else {
			print $c $cmd . "\n";
		}
        }
}

sub usage {
	print STDERR "usage: [-s speed_factor] [-r rate] [-l speed] [-p port]  [-t] <gpx file>\n";
	exit 1;
}

#$GPGGA,165220,4551.0000,N,00154.2093,W,1,04,5.6,0.6,M,34.5,M,,*5C
#$GPRMC,165220,A,4551.0000,N,00154.2093,W,52.4,90.0,110915,5,E,A*1A
#$GPGSV,8,1,32,01,83,091,48,02,82,308,47,03,41,261,11,04,81,281,47*79
#$GPGSV,8,2,32,05,26,093,-4,06,06,045,-24,07,02,140,00,08,53,225,23*40
#$GPGSV,8,3,32,09,37,129,07,10,52,053,22,11,35,056,05,12,45,052,16*71
#$GPGSV,8,4,32,13,31,013,01,14,42,133,13,15,79,084,45,16,75,164,43*71
#$GPGSV,8,5,32,17,23,112,-7,18,03,161,00,19,50,106,21,20,89,319,51*63
#$GPGSV,8,6,32,21,59,129,29,22,81,277,47,23,02,166,00,24,88,045,51*71
#$GPGSV,8,7,32,25,81,128,47,26,39,082,10,27,05,256,00,28,35,061,05*7B
#$GPGSV,8,8,32,29,42,290,13,30,32,337,02,31,16,331,-15,32,28,193,-2*49
