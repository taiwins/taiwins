/*
 * edid.c - taiwins server drm functions for reading edid
 *
 * Copyright (c) 2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "internal.h"

#define DISPLAY_DESCRIPTOR_MODEL 0xfc
#define DISPLAY_DESCRIPTOR_SERIAL 0xff

static inline void
parse_edid_string(char *dest, const uint8_t *bytes)
{
	char text[13] = {0};

	strncpy(text, (const char *)bytes, 12);
	text[12] = '\0';
	for (int i = 0; text[i] != '\0'; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			text[i] = '\0';
			break;
		}
	}
	if (strlen(text))
		strcpy(dest, text);
}

/* refer to https://en.wikipedia.org/wiki/Extended_Display_Identification_Data
 * for how the bits are structured.
 */
static void
parse_edid(char make[32], char model[32], char serial[16],
           const uint8_t *b, size_t s)
{
	uint32_t serial_number;
	uint16_t model_number, pnp_number;

	//parse monitor make, which are 3 letters for representing the
	//manifacture in byte 8-9 in "big-endian": 00001 -> A, 00010 -> B
	pnp_number = (b[8] << 8) + b[9];
	make[0] = 'A' + ((pnp_number >> 10) & 0x1F) - 1;
	make[1] = 'A' + ((pnp_number >> 5)  & 0x1F) - 1;
	make[2] = 'A' + (pnp_number & 0x1F) - 1;
	make[4] = '\0';

	//model number in "little endian".
	model_number = b[10] + (b[11] << 8);
	snprintf(model, 32, "0x%04X", model_number);
	//serial number
	serial_number = b[12] + (b[13] << 8) + (b[14] << 16) + (b[15] << 16);
	snprintf(serial, 16, "0x%08X", serial_number);

	//parsing detailed description block
	for (int i = 54; i < 108; i += 18) {
		if (b[i] != 0 || b[i+2] != 0)
			continue;
		if (b[i+3] == DISPLAY_DESCRIPTOR_MODEL)
			parse_edid_string(model, &b[i+5]);
		else if (b[i+3] == DISPLAY_DESCRIPTOR_SERIAL)
			parse_edid_string(serial, &b[i+5]);
	}
}

bool
drm_display_read_edid(int fd, drmModeConnector *conn, uint32_t prop_edid,
                      char make[32], char model[32], char serial[16])
{
	drmModePropertyBlobPtr blob = NULL;
	uint64_t blob_id = 0;

	tw_drm_get_property(fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR,
	                    "EDID", &blob_id);
	if (!blob_id)
		goto err;
	blob = drmModeGetPropertyBlob(fd, blob_id);
	if (!blob)
		goto err;
	if (!blob->data || !blob->length)
		goto err_data;

	parse_edid(make, model, serial, blob->data, blob->length);
	drmModeFreePropertyBlob(blob);
	return true;
err_data:
	drmModeFreePropertyBlob(blob);
err:
	sprintf(make, "<Unknown>");
	sprintf(model, "<Unknown>");
	sprintf(model, "<Unknown>");
	return false;
}
