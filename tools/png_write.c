/*
 * A minimal indexed-PNG writer, so the host backend can dump frames with no
 * library dependency at all - not even zlib.
 *
 * The trick is that a PNG's IDAT is a zlib stream, and a zlib stream is allowed
 * to consist entirely of *stored* (uncompressed) deflate blocks. That costs
 * five bytes per 64 KB and removes the only dependency this build would
 * otherwise have. A frame is 320x200, so the files are about 64 KB each.
 *
 * Indexed rather than RGB on purpose: the game's output *is* 8bpp indexed plus
 * a 256-entry CLUT, and writing it as colour type 3 preserves exactly that.
 * A wrong palette row and a wrong pixel index stay distinguishable in the file.
 */
#include <stdio.h>
#include <string.h>

#include "png_write.h"

static unsigned crc_table[256];
static int      crc_table_ready;

static void crc_init(void)
{
	for (unsigned n = 0; n < 256; ++n) {
		unsigned c = n;
		for (int k = 0; k < 8; ++k)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
		crc_table[n] = c;
	}
	crc_table_ready = 1;
}

static unsigned crc_update(unsigned crc, const unsigned char *buf, size_t len)
{
	if (!crc_table_ready)
		crc_init();
	for (size_t i = 0; i < len; ++i)
		crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

static void be32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

/* One PNG chunk: length, type, payload, CRC over type+payload. */
static int chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
	unsigned char hdr[8], tail[4];
	be32(hdr, (unsigned)len);
	memcpy(hdr + 4, type, 4);
	if (fwrite(hdr, 1, 8, f) != 8)
		return -1;
	if (len && fwrite(data, 1, len, f) != len)
		return -1;

	unsigned crc = crc_update(0xFFFFFFFFu, hdr + 4, 4);
	if (len)
		crc = crc_update(crc, data, len);
	be32(tail, crc ^ 0xFFFFFFFFu);
	return fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int png_write_indexed(const char *path, const unsigned char *pixels,
                      int w, int h, int pitch,
                      const unsigned char palette[256][3])
{
	if (w <= 0 || h <= 0)
		return -1;

	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return -1;

	static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
	if (fwrite(sig, 1, 8, f) != 8)
		goto fail;

	unsigned char ihdr[13];
	be32(ihdr + 0, (unsigned)w);
	be32(ihdr + 4, (unsigned)h);
	ihdr[8]  = 8;   /* bit depth   */
	ihdr[9]  = 3;   /* colour type: indexed */
	ihdr[10] = 0;   /* deflate     */
	ihdr[11] = 0;   /* filter      */
	ihdr[12] = 0;   /* no interlace */
	if (chunk(f, "IHDR", ihdr, sizeof(ihdr)) != 0)
		goto fail;

	unsigned char plte[256 * 3];
	memcpy(plte, palette, sizeof(plte));
	if (chunk(f, "PLTE", plte, sizeof(plte)) != 0)
		goto fail;

	/*
	 * The raw stream: one filter byte (0 = None) per row, then the row.
	 * Wrapped in a zlib stream made of stored deflate blocks.
	 */
	size_t raw_len = (size_t)h * (size_t)(w + 1);
	size_t nblocks = (raw_len + 65534) / 65535;
	size_t idat_len = 2 + nblocks * 5 + raw_len + 4;

	unsigned char *idat = (unsigned char *)malloc(idat_len);
	if (idat == NULL)
		goto fail;

	size_t o = 0;
	idat[o++] = 0x78;   /* CMF: deflate, 32K window */
	idat[o++] = 0x01;   /* FLG: no dict, check bits make 0x7801 % 31 == 0 */

	unsigned a = 1, b = 0;   /* adler32 over the *raw* bytes */
	size_t written = 0;
	for (size_t blk = 0; blk < nblocks; ++blk) {
		size_t remaining = raw_len - written;
		size_t len = remaining > 65535 ? 65535 : remaining;

		idat[o++] = (blk + 1 == nblocks) ? 1 : 0;   /* BFINAL, BTYPE=stored */
		idat[o++] = (unsigned char)(len & 0xFF);
		idat[o++] = (unsigned char)(len >> 8);
		idat[o++] = (unsigned char)(~len & 0xFF);
		idat[o++] = (unsigned char)((~len >> 8) & 0xFF);

		for (size_t i = 0; i < len; ++i, ++written) {
			/* Walk the raw stream lazily: it is filter bytes interleaved
			 * with rows, and materialising it separately would double the
			 * memory for no gain. */
			size_t row = written / (size_t)(w + 1);
			size_t col = written % (size_t)(w + 1);
			unsigned char v = (col == 0) ? 0
			                             : pixels[row * (size_t)pitch + (col - 1)];
			idat[o++] = v;
			a = (a + v) % 65521;
			b = (b + a) % 65521;
		}
	}
	be32(idat + o, (b << 16) | a);
	o += 4;

	int rc = chunk(f, "IDAT", idat, o);
	free(idat);
	if (rc != 0)
		goto fail;

	if (chunk(f, "IEND", NULL, 0) != 0)
		goto fail;

	fclose(f);
	return 0;

fail:
	fclose(f);
	return -1;
}
