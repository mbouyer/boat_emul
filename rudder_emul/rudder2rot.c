/* $Id: rudder2rot.c,v 1.5 2019/03/12 15:59:07 bouyer Exp $ */
/*
 * Copyright (c) 2019 Manuel Bouyer
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <math.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#ifdef __NetBSD__
#include <netcan/can.h>
#else
#include <linux/can.h>
#include <linux/can/raw.h>
#endif

/*
 * based on equations from Pieter Geerkens:
 * https://gamedev.stackexchange.com/questions/92747/2d-boat-controlling-physics
 * with improvements by me.
 */
const double Mz=5000; /*  The Turning Moment of the ship about the steering axis */
const double L2=3; /* L/2 The distance of the rudder from the turning axis */
double v; /*  The current linear velocity of the ship, in m/s */
double w = 0; /*  The current rotational (yaw) velocity of the ship about the Z-axis */
const double p0=0; /* The constant, linear and quadratic coefficients */
const double p1=200; /* respectively of angular friction (ie resistance to */
const double p2=1000; /* turning) for the water */
const double A=0.5; /* area of the rudder */
const double d=1000; /* density of water (in kg/m^3!) */

static void
update_rot(double sec, double theta)
{
	double Tr, Tw, T;
	double v_r, v_tot;
	double alpha;

	if (v == 0) {
		w = 0;
		return;
	}

	// printf("sec %f theta %f", sec, theta);
	/* compute speed vector at rudder */
	/* radial speed */
	v_r = v * w;
	/* resultant speed */
	v_tot = sqrt(v_r * v_r + v * v);
	/* speed angle */
	alpha = atan(v_r / v);
	//printf("w %f theta %f vr %f alpha %f\n", w, theta, v_r, alpha);
	/* Turning torque from rudder */
	Tr = sin(theta - alpha) * A * v_tot * d * L2;
	/* Friction torque from water */
	Tw = p1 * fabs(w) + p2 * w * w;
	if (w > 0)
		Tw = -Tw;
	// printf(" w %f Tr %f Tw %f\n", w, Tr, Tw);
	w = w + (Tr + Tw) / Mz * sec;
	// printf(" w %f\n", w);
}

#define PRIVATE_COMMAND_STATUS 61846UL
struct private_command_status {       
        int16_t heading; /* heading to follow, rad * 10000 */
        union command_errors {
                struct err_bits {     
                        char    no_capteur_data : 1;
                        char    no_rudder_data  : 1;
                        char    output_overload : 1;
                        char    output_error : 1;
                } bits;
                unsigned char byte;   
        } command_errors;
        unsigned char auto_mode;      
#define AUTO_OFF        0x0000
#define AUTO_STANDBY    0x0001
#define AUTO_HEAD       0x0002
        char rudder; /* rudder angle report, in % */
        char params_slot;
};


int s;

static void
usage()
{
	fprintf(stderr, "usage: %s <interface> <speed factor>\n", getprogname());
	exit(1);
}

int
main(int argc, const char *argv[])
{
	struct ifreq ifr;
	struct sockaddr_can sa;
	struct can_frame cf;
	struct can_filter cfi;
	int r;
	double rudderb = 0, rudderp = 0;
	struct timeval tv_p, tv_now;
	char buf[10];

	if (argc != 3) {
		usage();
	}

	v = strtod(argv[2], NULL);
	v = v * 1852.0 / 3600.0;

	if ((s = socket(AF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		err(1, "CAN socket");
	}
	strncpy(ifr.ifr_name, argv[1], IFNAMSIZ );
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		err(1, "SIOCGIFINDEX for %s", argv[1]);
	}
	sa.can_family = AF_CAN;
	sa.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		err(1, "bind socket");
	}

	cfi.can_id = ((PRIVATE_COMMAND_STATUS) << 8) | CAN_EFF_FLAG;
	cfi.can_mask = (0x1ffff << 8) | CAN_EFF_FLAG;
        if (setsockopt(s,
	    SOL_CAN_RAW, CAN_RAW_FILTER, &cfi, sizeof(cfi)) < 0) {
                err(1, "setsockopt(CAN_RAW_FILTER)");
        }
	if (gettimeofday(&tv_p, NULL) <0) {
		err(1, "gettimeofday");
	}

	while (1) {
		struct timeval tv_t;
		double s_diff;
		fd_set read_set;
		int sret;

#if 1
		tv_t.tv_sec = 0;
		tv_t.tv_usec = 100000;
#else
		tv_t.tv_sec = 2;
		tv_t.tv_usec = 0;
#endif
		FD_ZERO(&read_set);
		FD_SET(s, &read_set);
		FD_SET(0, &read_set);
		sret = select(s + 1, &read_set, NULL, NULL, &tv_t);
		switch(sret) {
		case -1:
			err(1, "select");
			break;
		case 0:
			break;
		default:
			if (FD_ISSET(s, &read_set)) {
				int rudder;
				r = read(s, &cf, sizeof(cf));
				if (r < 0) {
					err(1, "read from socket");
				}
				if (r == 0)
					break;
				if (cf.can_dlc < sizeof(struct private_command_status)) {
					fprintf(stderr, "short frame %d\n", cf.can_dlc);
					continue;
				}
				rudder = (char)cf.data[4];
				/* rudder in %, 100%=30deg */
				rudderb = -rudder * 0.52359878 / 100;
			}
			if (FD_ISSET(0, &read_set)) {	
				if (fgets(buf, sizeof(buf), stdin))
					rudderp = strtod(buf, NULL);
			}
			break;
		}

		if (gettimeofday(&tv_now, NULL) <0) {
			err(1, "gettimeofday");
		}
		s_diff = (tv_now.tv_sec - tv_p.tv_sec) +
		    (tv_now.tv_usec - tv_p.tv_usec) / 1000000.0;
		update_rot(s_diff, rudderb + rudderp);
		printf("%f\n", w);
		fflush(stdout);
		tv_p = tv_now;
	}
	exit(0);
}
