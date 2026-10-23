// DCC and COF decoding for unit graphics (written for this port from the format descriptions).
//
// DCC: per direction a bit stream (LSB first) with frame headers and five sub-streams; the
// image is coded in 4x4 cells shared by all frames of the direction. Stage 1 decodes, per frame
// cell, which of up to 4 colours the cell uses (or that it is unchanged); stage 2 paints the
// cells with per-pixel colour indices.
// COF: layer list (component, weapon class) and for every direction and frame the layer order.
#include "dcc.h"
#include <cstring>
#include <algorithm>

namespace {

struct BitReader {
	const uint8_t* d; size_t size; size_t bit; size_t end;
	BitReader(const uint8_t* data, size_t sz, size_t startBit, size_t endBit) : d(data), size(sz), bit(startBit), end(endBit) {}
	uint32_t read(int n) {
		uint32_t v = 0;
		for (int i = 0; i < n; ++i, ++bit) {
			if (bit >= end || (bit >> 3) >= size) return v; // past the stream: zero bits
			v |= (uint32_t)((d[bit >> 3] >> (bit & 7)) & 1) << i;
		}
		return v;
	}
	int32_t readSigned(int n) {
		if (n == 0) return 0;
		uint32_t v = read(n);
		if (v & (1u << (n - 1))) v |= ~0u << n;
		return (int32_t)v;
	}
};

const int kWidthTable[16] = {0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32};

struct FrameHeader {
	uint32_t variable0, width, height, optionalBytes, codedBytes, bottomUp;
	int32_t xOffset, yOffset;
	// Placement in the direction box and the cell split.
	int boxX, boxY, cellX0, cellY0, cellsW, cellsH;
	std::vector<int> cellW, cellH;
};

struct PixelEntry { uint8_t val[4]; int frame; int cellIndex; };

// Splits a span starting at offset 'start' (inside the direction's 4-pixel grid) into cells.
void splitCells(int start, int size, std::vector<int>& out) {
	out.clear();
	int first = 4 - (start % 4);
	if (size - first <= 1) { out.push_back(size); return; }
	int rest = size - first - 1;
	int n = 2 + rest / 4;
	if (rest % 4 == 0) --n;
	out.push_back(first);
	for (int i = 1; i < n - 1; ++i) out.push_back(4);
	out.push_back(size - first - 4 * (n - 2));
}

} // namespace

bool dcc_decode_direction(const uint8_t* data, size_t size, int dir, DccDirection& out) {
	out = DccDirection();
	if (size < 15 || data[0] != 0x74) return false;
	int nDirs = data[2];
	uint32_t framesPerDir; memcpy(&framesPerDir, data + 3, 4);
	if (dir < 0 || dir >= nDirs || framesPerDir == 0 || framesPerDir > 256) return false;
	uint32_t dirOffset; memcpy(&dirOffset, data + 15 + 4 * dir, 4);
	uint32_t dirEnd = (uint32_t)size;
	if (dir + 1 < nDirs) memcpy(&dirEnd, data + 15 + 4 * (dir + 1), 4);
	if (dirOffset >= size || dirEnd > size || dirEnd <= dirOffset) return false;
	const uint8_t* d = data + dirOffset;
	size_t dsize = dirEnd - dirOffset;
	BitReader br(d, dsize, 0, dsize * 8);

	br.read(32); // outsize coded
	uint32_t compression = br.read(2);
	int bVar0 = kWidthTable[br.read(4)], bWidth = kWidthTable[br.read(4)], bHeight = kWidthTable[br.read(4)];
	int bX = kWidthTable[br.read(4)], bY = kWidthTable[br.read(4)], bOpt = kWidthTable[br.read(4)], bCoded = kWidthTable[br.read(4)];

	int nFrames = (int)framesPerDir;
	std::vector<FrameHeader> fr(nFrames);
	int boxX0 = INT32_MAX, boxY0 = INT32_MAX, boxX1 = INT32_MIN, boxY1 = INT32_MIN;
	for (int f = 0; f < nFrames; ++f) {
		FrameHeader& h = fr[f];
		h.variable0 = br.read(bVar0); h.width = br.read(bWidth); h.height = br.read(bHeight);
		h.xOffset = br.readSigned(bX); h.yOffset = br.readSigned(bY);
		h.optionalBytes = br.read(bOpt); h.codedBytes = br.read(bCoded); h.bottomUp = br.read(1);
		if (h.width > 1024 || h.height > 1024) return false;
		boxX0 = std::min(boxX0, h.xOffset); boxX1 = std::max(boxX1, h.xOffset + (int)h.width);
		boxY0 = std::min(boxY0, h.yOffset - (int)h.height + 1); boxY1 = std::max(boxY1, h.yOffset + 1);
	}
	// Optional data (unused by the game drawing) is byte aligned after the headers.
	size_t optionalTotal = 0;
	for (auto& h : fr) optionalTotal += h.optionalBytes;
	if (optionalTotal) { br.bit = (br.bit + 7) & ~(size_t)7; br.bit += optionalTotal * 8; }

	uint32_t equalCellsSize = 0, pixelMaskSize = 0, encodingTypeSize = 0, rawPixelSize = 0;
	if (compression & 2) equalCellsSize = br.read(20);
	pixelMaskSize = br.read(20);
	if (compression & 1) { encodingTypeSize = br.read(20); rawPixelSize = br.read(20); }
	uint8_t pixelValues[256]; int nValues = 0;
	for (int i = 0; i < 256; ++i) if (br.read(1)) pixelValues[nValues++] = (uint8_t)i;

	size_t pos = br.bit;
	BitReader equalCells(d, dsize, pos, pos + equalCellsSize); pos += equalCellsSize;
	BitReader pixelMask(d, dsize, pos, pos + pixelMaskSize); pos += pixelMaskSize;
	BitReader encodingType(d, dsize, pos, pos + encodingTypeSize); pos += encodingTypeSize;
	BitReader rawPixel(d, dsize, pos, pos + rawPixelSize); pos += rawPixelSize;
	BitReader codes(d, dsize, pos, dsize * 8);

	int boxW = boxX1 - boxX0, boxH = boxY1 - boxY0;
	if (boxW <= 0 || boxH <= 0 || boxW > 2048 || boxH > 2048) return false;
	int dirCellsW = 1 + (boxW - 1) / 4, dirCellsH = 1 + (boxH - 1) / 4;
	for (auto& h : fr) {
		h.boxX = h.xOffset - boxX0;
		h.boxY = h.yOffset - (int)h.height + 1 - boxY0;
		h.cellX0 = h.boxX / 4; h.cellY0 = h.boxY / 4;
		splitCells(h.boxX, (int)h.width, h.cellW);
		splitCells(h.boxY, (int)h.height, h.cellH);
		h.cellsW = (int)h.cellW.size(); h.cellsH = (int)h.cellH.size();
	}

	// Stage 1: pixel buffer entries.
	std::vector<PixelEntry> entries;
	entries.reserve(4096);
	std::vector<int> lastEntry((size_t)dirCellsW * dirCellsH, -1);
	for (int f = 0; f < nFrames; ++f) {
		FrameHeader& h = fr[f];
		if (h.width == 0 || h.height == 0) continue;
		for (int cy = 0; cy < h.cellsH; ++cy)
			for (int cx = 0; cx < h.cellsW; ++cx) {
				int bufIdx = (h.cellY0 + cy) * dirCellsW + h.cellX0 + cx;
				if (bufIdx < 0 || bufIdx >= (int)lastEntry.size()) continue;
				int prev = lastEntry[bufIdx];
				uint32_t mask = 0x0F;
				bool sameAsBefore = false;
				if (prev >= 0) {
					if (equalCellsSize && equalCells.read(1)) sameAsBefore = true;
					else mask = pixelMask.read(4);
				}
				if (sameAsBefore) continue;
				int nPix = __builtin_popcount(mask);
				uint8_t read[4] = {0, 0, 0, 0};
				int decoded = 0;
				uint32_t last = 0;
				// The encoding bit exists only for cells that decode at least one colour.
				bool raw = nPix && encodingTypeSize && encodingType.read(1);
				for (int i = 0; i < nPix; ++i) {
					uint32_t v;
					if (raw) v = rawPixel.read(8);
					else { v = last; uint32_t step; do { step = codes.read(4); v += step; } while (step == 15); }
					if (v == last) break; // repeating the previous value ends the list
					read[decoded++] = (uint8_t)v;
					last = v;
				}
				PixelEntry e; e.frame = f; e.cellIndex = cy * h.cellsW + cx;
				int idx = decoded - 1;
				for (int i = 0; i < 4; ++i) {
					if (mask & (1u << i)) e.val[i] = idx >= 0 ? read[idx--] : 0;
					else e.val[i] = prev >= 0 ? entries[prev].val[i] : 0;
				}
				lastEntry[bufIdx] = (int)entries.size();
				entries.push_back(e);
			}
	}
	for (auto& e : entries) for (int i = 0; i < 4; ++i) e.val[i] = e.val[i] < nValues ? pixelValues[e.val[i]] : 0;

	// Stage 2: paint frames; unchanged cells copy the cell from the previous frame.
	std::vector<uint8_t> canvas((size_t)boxW * boxH, 0), previous((size_t)boxW * boxH, 0);
	std::vector<int> prevCellW((size_t)dirCellsW * dirCellsH, -1), prevCellH((size_t)dirCellsW * dirCellsH, -1);
	size_t entryPos = 0;
	out.boxX = boxX0; out.boxY = boxY0; out.width = boxW; out.height = boxH;
	out.frames.resize(nFrames);
	for (int f = 0; f < nFrames; ++f) {
		FrameHeader& h = fr[f];
		std::fill(canvas.begin(), canvas.end(), 0);
		if (h.width && h.height) {
			int py = h.boxY;
			for (int cy = 0; cy < h.cellsH; ++cy) {
				int px = h.boxX;
				for (int cx = 0; cx < h.cellsW; ++cx) {
					int w = h.cellW[cx], hh = h.cellH[cy];
					int bufIdx = (h.cellY0 + cy) * dirCellsW + h.cellX0 + cx;
					bool has = entryPos < entries.size() && entries[entryPos].frame == f && entries[entryPos].cellIndex == cy * h.cellsW + cx;
					if (has) {
						const PixelEntry& e = entries[entryPos++];
						int bits = e.val[0] == e.val[1] ? 0 : e.val[1] == e.val[2] ? 1 : 2;
						for (int y = 0; y < hh; ++y)
							for (int x = 0; x < w; ++x) {
								uint8_t c = bits == 0 ? e.val[0] : e.val[codes.read(bits)];
								canvas[(size_t)(py + y) * boxW + px + x] = c;
							}
					} else if (bufIdx >= 0 && bufIdx < (int)prevCellW.size() && prevCellW[bufIdx] == w && prevCellH[bufIdx] == hh) {
						for (int y = 0; y < hh; ++y)
							for (int x = 0; x < w; ++x)
								canvas[(size_t)(py + y) * boxW + px + x] = previous[(size_t)(py + y) * boxW + px + x];
					}
					if (bufIdx >= 0 && bufIdx < (int)prevCellW.size()) { prevCellW[bufIdx] = w; prevCellH[bufIdx] = hh; }
					px += w;
				}
				py += h.cellH[cy];
			}
		}
		// Keep the painted cells as the reference for the next frame.
		for (int cy = 0, py = h.boxY; cy < h.cellsH; py += h.cellH[cy], ++cy)
			for (int cx = 0, px = h.boxX; cx < h.cellsW; px += h.cellW[cx], ++cx)
				for (int y = 0; y < h.cellH[cy]; ++y)
					memcpy(&previous[(size_t)(py + y) * boxW + px], &canvas[(size_t)(py + y) * boxW + px], h.cellW[cx]);
		out.frames[f] = canvas;
	}
	return true;
}

bool cof_parse(const uint8_t* d, size_t size, Cof& out) {
	out = Cof();
	if (size < 28) return false;
	out.layers = d[0]; out.frames = d[1]; out.dirs = d[2];
	memcpy(&out.animRate, d + 24, 2);
	size_t p = 28;
	if (p + (size_t)out.layers * 9 > size) return false;
	for (int i = 0; i < out.layers; ++i, p += 9) {
		CofLayer l;
		l.component = d[p]; l.shadow = d[p + 1]; l.selectable = d[p + 2]; l.transparent = d[p + 3]; l.drawEffect = d[p + 4];
		memcpy(l.weaponClass, d + p + 5, 3); l.weaponClass[3] = 0;
		out.layer.push_back(l);
	}
	p += out.frames; // key frame markers
	size_t n = (size_t)out.dirs * out.frames * out.layers;
	if (p + n > size) return false;
	out.priority.assign(d + p, d + p + n);
	return true;
}

// DC6: 24-byte header (version 6), frame pointer table, then per frame a 32-byte header
// (flip, width, height, offsetX, offsetY, unknown, next block, length) and run-length data:
// 0x80 ends a row, a byte with the top bit set skips (byte & 0x7F) transparent pixels, any other
// byte n copies the next n pixels. Rows are stored bottom-up unless the flip flag is set.
bool dc6_decode(const uint8_t* d, size_t size, std::vector<Dc6Frame>& out) {
	out.clear();
	if (size < 24) return false;
	auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; };
	if (u32(0) != 6) return false;
	uint32_t dirs = u32(16), frames = u32(20);
	if (dirs * frames > 4096 || 24 + 4ull * dirs * frames > size) return false;
	for (uint32_t i = 0; i < dirs * frames; ++i) {
		uint32_t p = u32(24 + 4 * i);
		if (p + 32 > size) return false;
		Dc6Frame f;
		uint32_t flip = u32(p);
		f.width = (int)u32(p + 4); f.height = (int)u32(p + 8);
		f.offsetX = (int32_t)u32(p + 12); f.offsetY = (int32_t)u32(p + 16);
		uint32_t length = u32(p + 28);
		if (f.width < 0 || f.height < 0 || f.width > 4096 || f.height > 4096 || p + 32 + length > size) return false;
		f.pixels.assign((size_t)f.width * f.height, 0);
		const uint8_t* s = d + p + 32; const uint8_t* end = s + length;
		int x = 0, y = flip ? 0 : f.height - 1;
		while (s < end && y >= 0 && y < f.height) {
			uint8_t b = *s++;
			if (b == 0x80) { x = 0; y += flip ? 1 : -1; }
			else if (b & 0x80) x += b & 0x7F;
			else for (int k = 0; k < b && s < end; ++k, ++x) if (x < f.width) f.pixels[(size_t)y * f.width + x] = *s++; else ++s;
		}
		out.push_back(std::move(f));
	}
	return true;
}
