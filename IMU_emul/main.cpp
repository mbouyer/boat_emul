/*
 * Copyright (c) 2019 Manuel Bouyer
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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
#include <pthread.h>
#include <iostream>
#include "NMEA2000.h"
#include "nmea2000_defs_tx.h"

static nmea2000 *n2kp;
static volatile double rot;
static double heading;

n2k_attitude_tx *n2k_attitudep;
n2k_rateofturn_tx *n2k_rateofturnp;

static void
usage(void)
{
	std::cerr << "usage: " << getprogname() << " <canif>" << std::endl;
	exit(1);
}

static void *
do_rot(void *p)
{
	heading = 1;
	uint8_t sid = 0;
	while (1) {
		heading = heading + rot * 0.1;
		if (heading < 3.1415927)
			heading += 6.2831853;
		if (heading >= 3.1415927)
			heading -= 6.2831853;

		n2k_attitudep->update(heading, 0, 0, sid);
		n2k_rateofturnp->update(rot, sid);
		n2kp->send_bypgn(NMEA2000_ATTITUDE);
		n2kp->send_bypgn(NMEA2000_RATEOFTURN);
		sid++;
		usleep(100000);
	}
}

int
main(int argc, const char *argv[])
{
	pthread_t rot_thread;
	char buf[80];
	char *e;
	double d;

	if (argc != 2) {
		usage();
	}
	n2kp = new nmea2000(argv[1]);
	n2kp->Init();
	n2k_attitudep = (n2k_attitude_tx *)n2kp->get_frametx(n2kp->get_tx_bypgn(NMEA2000_ATTITUDE));
	n2k_rateofturnp = (n2k_rateofturn_tx *)n2kp->get_frametx(n2kp->get_tx_bypgn(NMEA2000_RATEOFTURN));

	n2kp->tx_enable(n2kp->get_tx_bypgn(NMEA2000_ATTITUDE), true);
	n2kp->tx_enable(n2kp->get_tx_bypgn(NMEA2000_RATEOFTURN), true);
	rot = 0;
	heading = 0;
	if (pthread_create(&rot_thread, NULL, do_rot, NULL) != 0) {
		perror("rot_thread");
		exit(1);
	}
	while (fgets(buf, sizeof(buf) - 1, stdin) != NULL) {
		d = strtod(buf, &e);
		if ((*e == '\0' || *e == '\n') && e != buf)  {
			rot = d;
		}
	}
	exit(0);
}
