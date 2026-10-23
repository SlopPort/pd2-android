#pragma once
#include <vector>

struct D2DrlgActStrc;

bool sgame_create(const char* szName, int nClass, int nDifficulty);
void sgame_tick();
// The player has entered the game (server sent the "in game" state); commands are accepted from then on.
bool sgame_ready();
void sgame_move(int nX, int nY, bool bRun);
D2DrlgActStrc* sgame_act();
// {subtileX, subtileY, precisionX, precisionY, mode, direction, level, frame, frameCount}
void sgame_player_state(int* out);
// Units other than the player: 8 ints each {type, classId, guid, x, y, mode, direction, frame}.
int sgame_units(std::vector<int>& out);
