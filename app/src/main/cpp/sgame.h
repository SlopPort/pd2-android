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
#include <string>
// Graphics of a unit (type, guid; guid 0 with type 0 = the player): COF folder, token, mode, weapon class, 16 layer codes.
bool sgame_unit_look(int nType, int nGuid, std::string& category, std::string& token, std::string& mode, std::string& wclass, std::string* armor);
// Hero life (whole points, STAT_HITPOINTS >> 8); -1 without a game.
int sgame_player_life();
// Diagnostics: {level, tohit, mindamage, maxdamage, armorclass, life} of a unit (type 0 guid 0 = player).
void sgame_unit_combat_stats(int nType, int nGuid, int* out);
// Fixed game seed for repeatable tests (-1 = random).
void sgame_set_seed(int nSeed);

// Left skill on a unit (packet 0x06).
void sgame_attack(int nType, int nGuid);

// items.txt flippy DC6 of an item on the ground, "" otherwise.
#include <string>
std::string sgame_ground_item_file(int nGuid);

// Hero {life, max life, mana, max mana}.
void sgame_player_vitals(int* out);

// Interact with a unit (packet 0x13).
void sgame_interact(int nType, int nGuid);

// Diagnostics: {gold, inventory item count}.
void sgame_player_goods(int* out);

// After death: revive in town (packet 0x41).
void sgame_resurrect();
// Test only: set the hero's current life.
void sgame_debug_set_life(int nLife);

// One item of the hero's inventory (see sgame_inventory).
struct InvItem { std::string file; int page, bodyLoc, x, y, w, h, guid, mode; };
#include <vector>
void sgame_inventory(std::vector<InvItem>& out);
// Inventory panel layout (800x600) from inventory.txt; out has 72 ints (see sgame.cpp).
bool sgame_inventory_layout(int* out);

// Item moves (0x17/0x18/0x19/0x1A/0x1C/0x1D), see sgame.cpp.
void sgame_item_command(int nCommand, int nGuid, int a, int b, int c);
