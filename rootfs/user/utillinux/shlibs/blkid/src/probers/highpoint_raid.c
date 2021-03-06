/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * Inspired by libvolume_id by
 *     Kay Sievers <kay.sievers@vrfy.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "blkidP.h"

struct hpt45x_metadata {
	uint32_t	magic;
};

#define HPT45X_MAGIC_OK			0x5a7816f3
#define HPT45X_MAGIC_BAD		0x5a7816fd

static int probe_highpoint45x(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct hpt45x_metadata *hpt;
	uint64_t off;
	uint32_t magic;

	if (pr->size < 0x10000)
		return -1;

	off = ((pr->size / 0x200) - 11) * 0x200;
	hpt = (struct hpt45x_metadata *)
			blkid_probe_get_buffer(pr,
					off,
					sizeof(struct hpt45x_metadata));
	if (!hpt)
		return -1;
	magic = le32_to_cpu(hpt->magic);
	if (magic != HPT45X_MAGIC_OK && magic != HPT45X_MAGIC_BAD)
		return -1;
	return 0;
}

const struct blkid_idinfo highpoint45x_idinfo = {
	.name		= "highpoint_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_highpoint45x,
	.magics		= BLKID_NONE_MAGIC
};

const struct blkid_idinfo highpoint37x_idinfo = {
	.name		= "highpoint_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.magics		= {
		{ .magic = "\xf0\x16\x78\x5a", .len = 4, .kboff = 4 },
		{ .magic = "\xfd\x16\x78\x5a", .len = 4, .kboff = 4 },
		{ NULL }
	}
};


