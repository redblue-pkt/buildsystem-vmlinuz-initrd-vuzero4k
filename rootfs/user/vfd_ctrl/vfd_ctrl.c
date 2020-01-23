/*
	vfd_ctrl for VU+ ZERO 4K

	Copyright (C) 2019 'BPanther'

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA
*/

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int proc_put(const char *path, const char *value, const int len)
{
	int ret, ret2;
	int pfd = open(path, O_WRONLY);
	if (pfd < 0)
		return pfd;
	ret = write(pfd, value, len);
	ret2 = close(pfd);
	if (ret2 < 0)
		return ret2;
	return ret;
}

int main(int argc, char **argv)
{
	if (!strcmp(argv[1], "on")) {
		const char * value = 'on';
		if (proc_put("/dev/ttyS1", value, sizeof(value)) < 0)
			perror("Error: LED on!");
	}
	else if (!strcmp(argv[1], "off")) {
		const char * value = 'off';
		if (proc_put("/dev/ttyS1", value, sizeof(value)) < 0)
			perror("Error: LED off!");
	}
	else if (!strcmp(argv[1], "blink")) {
		const char * value = 'blink';
		if (proc_put("/dev/ttyS1", value, sizeof(value)) < 0)
			perror("Error: LED blink!");
	} else {
		printf("vfd_ctrl <on|off|blink>\n");
	}
}
