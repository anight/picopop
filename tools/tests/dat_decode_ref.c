/*
 * Decode a DAT with SDLPoP's own reader, for datfile_check.py to compare against.
 *
 * tools/assets/datfile.py is a port of that reader. Checking a port against the
 * thing it was ported from is worth something only if the two are genuinely
 * separate programs, which they are here: this links the real functions out of
 * SDLPoP/src/seg009.c - decompr_img(), its four decompressors, calc_stride() and
 * conv_to_8bpp() - and runs them on bytes it read itself.
 *
 * Linking one file out of a game is not obvious, so: seg009.c is compiled with
 * -ffunction-sections and linked with --gc-sections, and nothing below refers to
 * anything but the decoder, so the rest of the game and everything it needs goes
 * away at link time. See the datfile target in the Makefile.
 *
 * The container is walked here rather than taken from Python, so the index table
 * and the per-resource checksum byte are checked too and not merely assumed.
 * Layout, from upstream SDLPoP's types.h:
 *
 *     header  u32 table_offset, u16 table_size
 *     table   u16 count, then count entries of u16 id, u32 offset, u16 size
 *
 * `offset` points at a checksum byte; the resource is the `size` bytes after it.
 *
 * Writes a stream on stdout:
 *
 *     "DATD", u32 count
 *     per resource: u32 id, u32 rawlen, rawlen bytes,
 *                   u32 width, u32 height, u32 pixlen, pixlen bytes
 *
 * width, height and pixlen are zero for a resource this was not asked to decode.
 *
 * Which resources to decode is given on the command line rather than decided
 * here, because these decompressors have no bounds of their own: the game only
 * ever calls them on resources it already knows are images, and fed anything
 * else they write outside the destination. LEVELS.DAT res2005 is one - a level
 * whose first six bytes read as a 13364x1 4bpp header, which sends
 * decompress_lzg_ud() off the end of the buffer. So the caller says what is an
 * image and this decodes exactly that; the container walk below is what remains
 * genuinely independent of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;

/* The image header, and the only part of SDLPoP's types.h this needs. */
#pragma pack(push, 1)
typedef struct {
	unsigned short height, width, flags;
	byte data[];
} image_data_type;
#pragma pack(pop)

/* Out of seg009.c, by the link described above. */
int   calc_stride(const image_data_type *image_data);
void  decompr_img(byte *dest, const image_data_type *source, int decomp_size,
                  int cmeth, int stride);
byte *conv_to_8bpp(byte *in_data, int width, int height, int stride, int depth);

/*
 * What this will ask the C decoder to do.
 *
 * Its counters are `short`: decompress_rle_ud() and decompress_lzg_ud() hold the
 * remaining height in one, and all four hold the remaining length in one. A
 * header claiming more than a short can count - and a resource that is not an
 * image can claim 65535x65535 - makes those counters wrap and the up-down
 * writers walk outside the buffer. So the reference decodes only what its own
 * decoder can represent.
 *
 * This costs no coverage: the largest image in the game's data is a 320x200
 * full-screen picture at 4bpp, 32000 bytes decompressed and 200 rows tall.
 */
#define MAX_DECODE_BYTES 32767
#define MAX_DECODE_ROWS  32767

static unsigned rd16(const byte *p) { return p[0] | (p[1] << 8); }
static unsigned rd32(const byte *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) |
	       ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void put32(unsigned v)
{
	byte b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff };
	fwrite(b, 1, 4, stdout);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.DAT> [id-to-decode...]\n", argv[0]);
		return 2;
	}

	FILE *fp = fopen(argv[1], "rb");
	if (fp == NULL) { perror(argv[1]); return 1; }
	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	byte *blob = malloc((size_t)len);
	if (blob == NULL || fread(blob, 1, (size_t)len, fp) != (size_t)len) {
		fprintf(stderr, "%s: cannot read\n", argv[1]);
		return 1;
	}
	fclose(fp);

	if (len < 6) { fprintf(stderr, "%s: too short\n", argv[1]); return 1; }
	unsigned table_offset = rd32(blob);
	unsigned table_size   = rd16(blob + 4);
	if ((long)(table_offset + table_size) > len || table_size < 2) {
		fprintf(stderr, "%s: bad index table\n", argv[1]);
		return 1;
	}
	unsigned count = rd16(blob + table_offset);
	if (2 + count * 8u > table_size) {
		fprintf(stderr, "%s: index table holds fewer than %u entries\n",
		        argv[1], count);
		return 1;
	}

	fwrite("DATD", 1, 4, stdout);
	put32(count);

	for (unsigned i = 0; i < count; ++i) {
		const byte *e = blob + table_offset + 2 + i * 8;
		unsigned id     = rd16(e);
		unsigned offset = rd32(e + 2);
		unsigned size   = rd16(e + 6);

		if ((long)(offset + 1 + size) > len) {
			fprintf(stderr, "%s: res%u runs past the end\n", argv[1], id);
			return 1;
		}
		const byte *raw = blob + offset + 1;   /* past the checksum byte */

		put32(id);
		put32(size);
		fwrite(raw, 1, size, stdout);

		int wanted = 0;
		for (int a = 2; a < argc; ++a)
			if ((unsigned)atoi(argv[a]) == id) { wanted = 1; break; }

		int decoded = 0;
		if (wanted && size >= 6) {
			const image_data_type *im = (const image_data_type *)raw;
			unsigned height = im->height ? im->height : 1;
			unsigned width  = im->width;
			unsigned flags  = im->flags;
			unsigned depth  = ((flags >> 12) & 7) + 1;
			unsigned cmeth  = (flags >> 8) & 0x0F;
			int stride = calc_stride(im);
			long dest_size = (long)stride * (long)height;

			if (cmeth <= 4 && width > 0 && dest_size > 0 &&
			    dest_size <= MAX_DECODE_BYTES && height <= MAX_DECODE_ROWS) {
				/* Asked for, and within what the C can represent. */
				/*
				 * Decode out of a zero-padded copy. The decompressors
				 * read their source until the destination is full, with
				 * no bound of their own, and a truncated resource would
				 * otherwise run off the end of the file image. Every
				 * output byte costs at most two input bytes across the
				 * four methods, so this keeps every read inside the copy.
				 */
				size_t padded = (size_t)size + 4 * (size_t)dest_size + 64;
				byte *src = calloc(1, padded);
				memcpy(src, raw, size);

				byte *packed = calloc(1, (size_t)dest_size);
				decompr_img(packed, (const image_data_type *)src,
				            (int)dest_size, (int)cmeth, stride);
				byte *pix = conv_to_8bpp(packed, (int)width, (int)height,
				                         stride, (int)depth);
				put32(width);
				put32(height);
				put32(width * height);
				fwrite(pix, 1, (size_t)width * height, stdout);
				free(pix);
				free(packed);
				free(src);
				decoded = 1;
			}
		}
		if (!decoded) { put32(0); put32(0); put32(0); }
	}

	free(blob);
	return fflush(stdout) == 0 ? 0 : 1;
}
