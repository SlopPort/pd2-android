#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// One direction of a DCC: all frames painted into the same box (palette indices, 0 = transparent).
struct DccDirection {
	int boxX = 0, boxY = 0, width = 0, height = 0; // box relative to the unit's foot point
	std::vector<std::vector<uint8_t>> frames;
};

bool dcc_decode_direction(const uint8_t* data, size_t size, int dir, DccDirection& out);

struct CofLayer {
	uint8_t component, shadow, selectable, transparent, drawEffect;
	char weaponClass[4];
};

struct Cof {
	int layers = 0, frames = 0, dirs = 0;
	uint16_t animRate = 0;
	std::vector<CofLayer> layer;
	std::vector<uint8_t> priority; // [dir][frame][order] -> component
};

bool cof_parse(const uint8_t* data, size_t size, Cof& out);

// DC6 (UI, item and automap graphics): every frame of every direction, palette indices (0 = transparent).
struct Dc6Frame {
	int width = 0, height = 0, offsetX = 0, offsetY = 0; // offsets: frame's bottom-left relative to its anchor
	std::vector<uint8_t> pixels;                          // rows top to bottom
};
bool dc6_decode(const uint8_t* data, size_t size, std::vector<Dc6Frame>& out);
