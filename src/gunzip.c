/* gunzip.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2008 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See http://www.gnu.org/ for details.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <zlib.h>

#include "apk_defines.h"
#include "apk_io.h"

struct apk_gzip_istream {
	struct apk_istream is;
	struct apk_bstream *bs;
	z_stream zs;
	int z_err;

	EVP_MD_CTX mdctx;
	void *mdblock;

	apk_multipart_cb cb;
	void *cbctx;
};

static size_t gz_read(void *stream, void *ptr, size_t size)
{
	struct apk_gzip_istream *gis =
		container_of(stream, struct apk_gzip_istream, is);

	if (gis->z_err == Z_DATA_ERROR || gis->z_err == Z_ERRNO)
		return -1;
	if (gis->z_err == Z_STREAM_END)
		return 0;

	if (ptr == NULL)
		return apk_istream_skip(&gis->is, size);

	gis->zs.avail_out = size;
	gis->zs.next_out  = ptr;

	while (gis->zs.avail_out != 0 && gis->z_err == Z_OK) {
		if (gis->zs.avail_in == 0) {
			apk_blob_t blob;

			if (gis->cb != NULL && gis->mdblock != NULL) {
				/* Digest the inflated bytes */
				EVP_DigestUpdate(&gis->mdctx, gis->mdblock,
						 (void *)gis->zs.next_in - gis->mdblock);
			}
			blob = gis->bs->read(gis->bs, APK_BLOB_NULL);
			gis->mdblock = blob.ptr;
			gis->zs.avail_in = blob.len;
			gis->zs.next_in = (void *) gis->mdblock;
			if (gis->zs.avail_in < 0) {
				gis->z_err = Z_DATA_ERROR;
				return size - gis->zs.avail_out;
			} else if (gis->zs.avail_in == 0) {
				if (gis->cb != NULL)
					gis->cb(gis->cbctx, &gis->mdctx,
						APK_MPART_END);
				gis->z_err = Z_STREAM_END;
				return size - gis->zs.avail_out;
			}
		}

		gis->z_err = inflate(&gis->zs, Z_NO_FLUSH);
		if (gis->z_err == Z_STREAM_END) {
			/* Digest the inflated bytes */
			if (gis->cb != NULL) {
				EVP_DigestUpdate(&gis->mdctx, gis->mdblock,
						 (void *)gis->zs.next_in - gis->mdblock);
				gis->mdblock = gis->zs.next_in;
				gis->cb(gis->cbctx, &gis->mdctx,
					APK_MPART_BOUNDARY);
			}
			inflateEnd(&gis->zs);

			if (inflateInit2(&gis->zs, 15+32) != Z_OK)
				return -1;
			gis->z_err = Z_OK;
		}
	}

	if (gis->z_err != Z_OK && gis->z_err != Z_STREAM_END)
		return -1;

	return size - gis->zs.avail_out;
}

static void gz_close(void *stream)
{
	struct apk_gzip_istream *gis =
		container_of(stream, struct apk_gzip_istream, is);

	if (gis->cb != NULL)
		EVP_MD_CTX_cleanup(&gis->mdctx);
	inflateEnd(&gis->zs);
	gis->bs->close(gis->bs, NULL);
	free(gis);
}

struct apk_istream *apk_bstream_gunzip_mpart(struct apk_bstream *bs,
					     apk_multipart_cb cb, void *ctx)
{
	struct apk_gzip_istream *gis;

	if (bs == NULL)
		return NULL;

	gis = malloc(sizeof(struct apk_gzip_istream));
	if (gis == NULL)
		return NULL;

	*gis = (struct apk_gzip_istream) {
		.is.read = gz_read,
		.is.close = gz_close,
		.bs = bs,
		.z_err = 0,
		.cb = cb,
		.cbctx = ctx,
	};

	if (inflateInit2(&gis->zs, 15+32) != Z_OK) {
		free(gis);
		return NULL;
	}

	if (gis->cb != NULL) {
		EVP_MD_CTX_init(&gis->mdctx);
		cb(ctx, &gis->mdctx, APK_MPART_BEGIN);
	}

	return &gis->is;
}

