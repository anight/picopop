#! /usr/bin/env python3
"""
Read a Prince of Persia DAT file: the container, and the image codec.

This is a port of the reader in SDLPoP/src/seg009.c - open_dat() for the
container, and decompr_img() with its four decompressors and conv_to_8bpp() for
the images. That C is the authority; `make -C tools/tests datfile` decodes every
image resource both ways and compares them pixel for pixel.

The container:

    header      u32 table_offset, u16 table_size
    table       u16 count, then `count` entries of
                u16 id, u32 offset, u16 size

`offset` points at a one-byte checksum; the resource is the `size` bytes that
follow it. Nothing here verifies the checksum, which is what the game does too.

An image resource:

    u16 height, u16 width, u16 flags, then the compressed data

    depth = ((flags >> 12) & 7) + 1      bits per pixel, 1..8
    cmeth = (flags >> 8) & 0x0F          0 raw, 1 RLE-LR, 2 RLE-UD,
                                         3 LZG-LR, 4 LZG-UD
    stride = (depth * width + 7) // 8

A sprite-set palette (shpl) is 100 bytes: a count, three bytes this reader
ignores, then sixteen 6-bit RGB triples.
"""

import struct

# Two places where this port is deliberately not bit-identical to the C, both
# of them cases the C reaches only on input it was never given:
#
#   The RLE count is a signed byte. At 0x80 the C negates it to itself, and the
#   loop then runs on a value that only terminates because the remaining length
#   bounds it. Here 0x80 means a run of 128, which is what the encoding plainly
#   intends. No resource in the game's data uses it, which the comparison test
#   demonstrates rather than assumes.
#
#   The C counts remaining bytes in a `short`. A decompressed size above 32767
#   would go negative there and here it would not. The largest image in the data
#   is well inside that, again demonstrated rather than assumed.


# An image bigger than this is not an image. The format's fields allow
# 65535x65535 at 8bpp, which is 4 GB, so a resource whose first six bytes happen
# not to be an image header can otherwise send the decoder away for minutes
# before producing anything. The game's largest is a 320x200 full-screen picture;
# this leaves two orders of magnitude of room above it and still rejects noise
# immediately.
MAX_IMAGE_BYTES = 1 << 20


class DatError(Exception):
    pass


def _rle_lr(src, dest_length):
    """RLE, left to right: the decompressed bytes in order."""
    out = bytearray(dest_length)
    si = 0
    di = 0
    while di < dest_length:
        count = src[si]
        si += 1
        if count < 0x80:                      # copy count+1 literal bytes
            n = min(count + 1, dest_length - di)
            out[di:di + n] = src[si:si + n]
            si += count + 1
            di += n
        else:                                 # repeat one byte
            n = min(0x100 - count, dest_length - di)
            out[di:di + n] = bytes([src[si]]) * n
            si += 1
            di += n
    return out


def _rle_ud(src, dest_length, stride, height):
    """RLE, top to bottom: the same stream, written down columns."""
    out = bytearray(dest_length)
    si = 0
    di = 0
    rem_height = height
    rem = dest_length
    end = dest_length - 1

    def step():
        nonlocal di, rem_height
        di += stride
        rem_height -= 1
        if rem_height == 0:
            di -= end
            rem_height = height

    while rem:
        count = src[si]
        si += 1
        if count < 0x80:
            n = count + 1
            while n and rem:
                out[di] = src[si]
                si += 1
                step()
                rem -= 1
                n -= 1
        else:
            v = src[si]
            si += 1
            n = 0x100 - count
            while n and rem:
                out[di] = v
                step()
                rem -= 1
                n -= 1
    return out


def _lzg_lr(src, dest_length):
    """LZG, left to right: literals and back-references into a 1 KB window."""
    out = bytearray(dest_length)
    window = bytearray(0x400)
    wp = 0x400 - 0x42
    si = 0
    di = 0
    mask = 0

    while di < dest_length:
        mask >>= 1
        if (mask & 0xFF00) == 0:
            mask = src[si] | 0xFF00
            si += 1
        if mask & 1:
            v = src[si]
            si += 1
            window[wp] = v
            out[di] = v
            wp = (wp + 1) & 0x3FF
            di += 1
        else:
            info = (src[si] << 8) | src[si + 1]
            si += 2
            cp = info & 0x3FF
            n = (info >> 10) + 3
            while n and di < dest_length:
                v = window[cp]
                window[wp] = v
                out[di] = v
                cp = (cp + 1) & 0x3FF
                wp = (wp + 1) & 0x3FF
                di += 1
                n -= 1
    return out


def _lzg_ud(src, dest_length, stride, height):
    """LZG, top to bottom: the same stream, written down columns."""
    out = bytearray(dest_length)
    window = bytearray(0x400)
    wp = 0x400 - 0x42
    si = 0
    di = 0
    mask = 0
    rem_height = height
    rem = dest_length
    end = dest_length - 1

    def step():
        nonlocal di, rem_height
        di += stride
        rem_height -= 1
        if rem_height == 0:
            di -= end
            rem_height = height

    while rem:
        mask >>= 1
        if (mask & 0xFF00) == 0:
            mask = src[si] | 0xFF00
            si += 1
        if mask & 1:
            v = src[si]
            si += 1
            window[wp] = v
            out[di] = v
            wp = (wp + 1) & 0x3FF
            step()
            rem -= 1
        else:
            info = (src[si] << 8) | src[si + 1]
            si += 2
            cp = info & 0x3FF
            n = (info >> 10) + 3
            while rem and n:
                v = window[cp]
                window[wp] = v
                out[di] = v
                cp = (cp + 1) & 0x3FF
                wp = (wp + 1) & 0x3FF
                step()
                rem -= 1
                n -= 1
    return out


def _to_8bpp(packed, width, height, stride, depth):
    """Unpack depth-bit pixels into one byte each, row by row."""
    out = bytearray(width * height)
    per_byte = 8 // depth
    mask = (1 << depth) - 1
    for y in range(height):
        ip = y * stride
        op = y * width
        x = 0
        for _ in range(stride):
            if x >= width:
                break
            v = packed[ip]
            ip += 1
            shift = 8
            for _ in range(per_byte):
                if x >= width:
                    break
                shift -= depth
                out[op] = (v >> shift) & mask
                op += 1
                x += 1
    return out


def image_header(data):
    """(width, height, depth, cmeth, stride) for a resource, without decoding."""
    if len(data) < 6:
        raise DatError("too short to be an image")
    height, width, flags = struct.unpack_from("<HHH", data, 0)
    if height == 0:
        height = 1
    depth = ((flags >> 12) & 7) + 1
    cmeth = (flags >> 8) & 0x0F
    stride = (depth * width + 7) // 8
    return width, height, depth, cmeth, stride


def decode_image(data):
    """Decode an image resource into (width, height, bytes of 8bpp indices).

    The indices are into the resource's sprite set palette, not into any global
    one; applying a palette is the caller's business.
    """
    width, height, depth, cmeth, stride = image_header(data)
    dest_size = stride * height
    if dest_size > MAX_IMAGE_BYTES:
        raise DatError(f"header claims {width}x{height} at {depth}bpp, "
                       f"{dest_size} bytes - not an image")
    body = data[6:]

    if cmeth == 0:
        packed = bytearray(dest_size)
        n = min(dest_size, len(body))
        packed[:n] = body[:n]
    elif cmeth == 1:
        packed = _rle_lr(body, dest_size)
    elif cmeth == 2:
        packed = _rle_ud(body, dest_size, stride, height)
    elif cmeth == 3:
        packed = _lzg_lr(body, dest_size)
    elif cmeth == 4:
        packed = _lzg_ud(body, dest_size, stride, height)
    else:
        # decompr_img's switch has no default, so the destination is left as it
        # was allocated: zeroed.
        packed = bytearray(dest_size)

    return width, height, _to_8bpp(packed, width, height, stride, depth)


def looks_like_image(data):
    """Whether a resource plausibly is an image, by its header alone.

    Used to find the images no sprite set lists. The test is structural rather
    than statistical: the compression method and depth must be ones the codec
    defines, the dimensions must be within what the game's art uses, and a
    decode must consume the resource without running off either end.
    """
    try:
        width, height, depth, cmeth, stride = image_header(data)
    except DatError:
        return False
    if cmeth > 4 or not 1 <= depth <= 8:
        return False
    if not 1 <= width <= 320 or not 1 <= height <= 200:
        return False
    if stride * height == 0:
        return False
    # An uncompressed image is exactly its pixels, so the resource has to be
    # the header plus that and nothing else. Without this a level resource
    # whose first six bytes happen to read as a small 1bpp image passes, the
    # raw path having no decode that can fail.
    if cmeth == 0 and len(data) - 6 != stride * height:
        return False
    try:
        decode_image(data)
    except (IndexError, DatError):
        return False
    return True


def looks_like_shpl(data):
    """Whether a resource plausibly is a sprite-set header.

    A hundred bytes is not enough on its own: two resources in the game's data
    are that length and are not palettes. VGA levels are six bits, so a real
    shpl has none above 63, and theirs run to 255.
    """
    if len(data) != 100:
        return False
    return all(data[4 + i] <= 63 for i in range(16 * 3))


def read_shpl(data):
    """A sprite-set header: (n_images, [16 × (r, g, b)]) in 6-bit VGA levels."""
    if len(data) != 100:
        raise DatError(f"shpl is {len(data)} bytes, expected 100")
    n_images = data[0]
    # Skip row_bits (2 bytes) and n_colors (1); the game overrides row_bits.
    palette = [(data[4 + i * 3], data[5 + i * 3], data[6 + i * 3])
               for i in range(16)]
    return n_images, palette


class Dat:
    """The resources of one DAT file, by id."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self._blob = f.read()

        if len(self._blob) < 6:
            raise DatError(f"{path}: too short to be a DAT")

        table_offset, table_size = struct.unpack_from("<IH", self._blob, 0)
        if table_offset + table_size > len(self._blob):
            raise DatError(f"{path}: index table runs past the end of the file")

        count, = struct.unpack_from("<H", self._blob, table_offset)
        if 2 + count * 8 > table_size:
            raise DatError(f"{path}: index table holds fewer than {count} entries")

        self._res = {}
        self.ids = []
        for i in range(count):
            rid, offset, size = struct.unpack_from(
                "<HIH", self._blob, table_offset + 2 + i * 8)
            if offset + 1 + size > len(self._blob):
                raise DatError(f"{path}: res{rid} runs past the end of the file")
            # A duplicate id shadows the earlier one, which is what walking the
            # table in order and overwriting gives.
            if rid not in self._res:
                self.ids.append(rid)
            self._res[rid] = (offset, size)
        self.ids.sort()

    def __contains__(self, rid):
        return rid in self._res

    def __len__(self):
        return len(self._res)

    def raw(self, rid):
        """The resource exactly as the file holds it, past its checksum byte."""
        offset, size = self._res[rid]
        return self._blob[offset + 1:offset + 1 + size]

    def checksum(self, rid):
        """The stored checksum byte. The game does not verify it either."""
        offset, _ = self._res[rid]
        return self._blob[offset]
