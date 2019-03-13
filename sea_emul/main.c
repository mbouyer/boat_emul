/* $Id: main.c,v 1.1 2019/03/12 23:03:16 bouyer Exp $ */
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

#include <sys/time.h>

static void
usage()
{
	fprintf(stderr, "usage: %s [-s a:p] [-q a:t1:t0] [-r a:t]\n", getprogname());
	exit(1);
}

struct perturb_descript
{
	enum type {
		t_sinus,
		t_square,
		t_random
	} type;
	double a;
	double p0, p1;
	double time;
	double val;
};

static void
doubletotimer(double v, struct timeval *tv)
{
	tv->tv_sec = v;
	v = v - tv->tv_sec;
	tv->tv_usec = v * 1000000;
}

static double
timetodouble(struct timeval *tv)
{
	double v;
	v = tv->tv_sec;
	v += tv->tv_usec / 1000000.0;
	return v;
}

static double
strsepandd(char **str, const char *sep)
{
	char *a;
	char *e;
	double d;
	a = strsep(str, sep);
	if (a == NULL) {
		usage();
	}
	d = strtod(a, &e);
	if (*e != '\0') {
		usage();
	}
	return d;
}

static double
drandom() {
	double v;
	v = random();
	v /= RANDOM_MAX;
	return v;
}


int
main(int argc, char * const argv[])
{
	struct timeval tv_p, tv_now;
	char buf[10];
	struct perturb_descript *pd;
	int c, i;
	int npd;
	char *a;
	double input_pert;
	double total_pert, new_pert;
	double last_print;

	if (argc < 2) {
		usage();
	}

	npd = (argc - 1); /* overkill, but whatever */
	pd = malloc(sizeof(*pd) * npd);
	if (pd == NULL) {
		err(1, "malloc");
	}
	memset(pd, 0, sizeof(*pd) * npd);

	i = 0;
	while ((c = getopt(argc, argv, "s:r:q:")) > 0) {
		switch(c) {
		case 's':
			pd[i].type = t_sinus;
			pd[i].a = strsepandd(&optarg, ":");
			pd[i].p0 = strsepandd(&optarg, ":");
			break;
		case 'q':
			pd[i].type = t_square;
			pd[i].a = strsepandd(&optarg, ":");
			pd[i].p0 = strsepandd(&optarg, ":");
			pd[i].p1 = strsepandd(&optarg, ":");
			break;
		case 'r':
			pd[i].type = t_random;
			pd[i].a = strsepandd(&optarg, ":");
			pd[i].p0 = strsepandd(&optarg, ":");
			break;
		default:
			usage();
		}
		i++;
	}
	npd = i;

	if (gettimeofday(&tv_p, NULL) <0) {
		err(1, "gettimeofday");
	}

	srandom(time(NULL));
	last_print = 0;

	while (1) {
		struct timeval tv_t, tv_diff;
		double timediff;
		double v;
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
		FD_SET(0, &read_set);
		sret = select(1, &read_set, NULL, NULL, &tv_t);
		switch(sret) {
		case -1:
			err(1, "select");
			break;
		case 0:
			break;
		default:
			if (FD_ISSET(0, &read_set)) {	
				if (fgets(buf, sizeof(buf), stdin))
					input_pert = strtod(buf, NULL);
			}
			break;
		}
		new_pert = input_pert;

		if (gettimeofday(&tv_now, NULL) <0) {
			err(1, "gettimeofday");
		}
		timersub(&tv_now, &tv_p, &tv_diff);
		timediff = timetodouble(&tv_diff);

		for (i = 0; i < npd; i++) {
			pd[i].time += timediff;
			switch(pd[i].type) {
			case t_sinus:
				if (pd[i].time > pd[i].p0)
					pd[i].time -= pd[i].p0;
				v = pd[i].time / pd[i].p0 * 6.2831854;
				pd[i].val = sin(v) * pd[i].a;
				break;
			case t_square:
				if (pd[i].val == pd[i].a) {
					if (pd[i].time > pd[i].p0) {
						pd[i].time -= pd[i].p0;
						pd[i].val = 0;
					}
				} else {
					if (pd[i].time > pd[i].p1) {
						pd[i].time -= pd[i].p1;
						pd[i].val = pd[i].a;
					}
				}
				break;
			case t_random:
				if (pd[i].time > pd[i].p1) {
					pd[i].time -= pd[i].p1;
					pd[i].p1 = pd[i].p0 * (drandom() + 0.5);
					pd[i].val = pd[i].a * (drandom() * 2 - 1);
				}
				break;
			}
			new_pert += pd[i].val;
		}
		if (fabs(total_pert - new_pert) > 0.001) {
			total_pert = new_pert;
			total_pert = new_pert;
			printf("%f\n", new_pert);
			fflush(stdout);
		}
		tv_p = tv_now;
	}
	exit(0);
}
