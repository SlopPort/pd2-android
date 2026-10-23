#pragma once
// Developer helper (not game logic): plans a walking route for the hero over D2's collision grid of the
// rooms the server currently has loaded, so tests can drive the hero to a far point with short
// walk commands the way a player would.
#include <vector>

struct D2DrlgActStrc;

struct RoutePoint { int x, y; };

// Breadth-first search on subtiles within `radius` of (fromX, fromY), walkable = loaded room and
// no COLLIDE_MASK_PLAYER_PATH bit. Returns the route to the reachable subtile closest to the
// target (empty if the hero cannot move at all).
// If goalLevel > 0, the first reachable subtile of that level ends the search.
std::vector<RoutePoint> test_plan_route(D2DrlgActStrc* pAct, int fromX, int fromY, int toX, int toY, int radius, int goalLevel = 0);
