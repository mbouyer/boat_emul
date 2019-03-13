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
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/ioctl.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netcan/can.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

static void
usage()
{
	printf("usage: %s <canif> <src port> <ip_dst> <dst port>\n",
	    getprogname());
	exit(1);
}

int main(int argc, char **argv) {
	int s_can, s_udp, d, src_port, dst_port;
	struct sockaddr_can s_cana;
	struct sockaddr_in srcaddr, dstaddr;
	struct can_frame cf;
	struct ifreq ifr;
	struct hostent *host;

	fd_set rset;
	int error;
	int size;
	int iarg;

	if (argc != 5) {
		usage();
	}

	if ((s_can = socket(AF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		err(1, "CAN socket");
	}

	strncpy(ifr.ifr_name, argv[1], IFNAMSIZ );
	if (ioctl(s_can, SIOCGIFINDEX, &ifr) < 0) {
		err(1, "SIOCGIFINDEX for %s", argv[1]);
	}
	s_cana.can_family = AF_CAN;       
	s_cana.can_ifindex = ifr.ifr_ifindex;
	if (bind(s_can, (struct sockaddr *)&s_cana, sizeof(s_cana)) < 0) {
		err(1, "bind CAN socket");
	}

	if ((src_port = atoi(argv[2])) <= 0)
		errx(EXIT_FAILURE, "bad port %s", argv[2]);

	if ((dst_port = atoi(argv[4])) <= 0)
		errx(EXIT_FAILURE, "bad port %s", argv[4]);

	if ((s_udp = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
		err(EXIT_FAILURE, "udp socket");
		exit(-1);
	}

	if ((src_port = atoi(argv[2])) <= 0)
		errx(EXIT_FAILURE, "bad port %s", argv[2]);

	memset(&srcaddr, 0, sizeof(srcaddr));
	srcaddr.sin_family = AF_INET;
	srcaddr.sin_addr.s_addr = INADDR_ANY;
	srcaddr.sin_port = ntohs(src_port);
	if (bind(s_udp, (struct sockaddr *)&srcaddr, sizeof(srcaddr)) == -1) {
		err(EXIT_FAILURE, "bind to %d", src_port);
		exit(-1);
	}

	memset(&dstaddr, 0, sizeof(dstaddr));
	dstaddr.sin_family = AF_INET;
	dstaddr.sin_port = ntohs(dst_port);
		

	error = inet_pton(AF_INET, argv[3], &dstaddr.sin_addr);
	if (error == -1) {
		err(EXIT_FAILURE, "inet_pton(%s)", argv[3]);
		exit(-1);
	}
	if (error == 0) {
		host = gethostbyname2(argv[3], AF_INET) ;
		if (host == NULL) {
			errx(EXIT_FAILURE, "%s: %s", argv[3], hstrerror(h_errno));
		}
		dstaddr.sin_addr.s_addr = * (u_long *)host->h_addr;
	}
		
	if (connect(s_udp, (struct sockaddr *)&dstaddr, sizeof(dstaddr)) == -1) {
		err(EXIT_FAILURE, "connect to %d", dst_port);
	}

	while (1) {
		FD_ZERO(&rset);
		FD_SET(s_can, &rset);
		FD_SET(s_udp, &rset);
		error = select(max(s_udp, s_can) + 1, &rset, NULL, NULL, NULL);

		if ( error == -1 && errno != EINTR) {
			err(EXIT_FAILURE, "select");
			exit(1);
		}
		if (FD_ISSET(s_udp, &rset)) {
			size = read(s_udp, &cf, sizeof(cf));
			if (size < 0) {
				warn("read UDP");
				continue;
			}
			size = write(s_can, &cf, sizeof(cf));
			if (size < 0) {
				warn("write CAN");
				continue;
			}
		}
		if (FD_ISSET(s_can, &rset)) {
			size = read(s_can, &cf, sizeof(cf));
			if (size < 0) {
				warn("read CAN");
				continue;
			}
			size = write(s_udp, &cf, sizeof(cf));
			if (size < 0) {
				warn("write UDP");
				continue;
			}
		}
	}
}
