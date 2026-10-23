#include "route.h"
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2Dungeon.h>
#include <D2Collision.h>
#include <Drlg/D2DrlgDrlg.h>
#include <cstdlib>
#include <deque>

std::vector<RoutePoint> test_plan_route(D2DrlgActStrc* pAct, int fromX, int fromY, int toX, int toY, int radius, int goalLevel) {
	const int side = 2 * radius + 1;
	const int x0 = fromX - radius, y0 = fromY - radius;
	std::vector<int> prev((size_t)side * side, -2); // -2 unvisited, -1 start
	auto walkable = [&](int x, int y) {
		D2ActiveRoomStrc* pRoom = DUNGEON_FindRoomBySubtileCoordinates(pAct, x, y);
		return pRoom && !COLLISION_CheckMask(pRoom, x, y, COLLIDE_MASK_PLAYER_PATH);
	};
	std::deque<int> open;
	int start = radius * side + radius;
	prev[start] = -1;
	open.push_back(start);
	int best = start;
	long bestDist = labs((long)(fromX - toX)) + labs((long)(fromY - toY));
	static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
	while (!open.empty()) {
		int cur = open.front(); open.pop_front();
		int cx = cur % side, cy = cur / side;
		long d = labs((long)(x0 + cx - toX)) + labs((long)(y0 + cy - toY));
		if (d < bestDist) { bestDist = d; best = cur; if (!d) break; }
		if (goalLevel > 0) {
			D2ActiveRoomStrc* pRoom = DUNGEON_FindRoomBySubtileCoordinates(pAct, x0 + cx, y0 + cy);
			if (pRoom && DUNGEON_GetLevelIdFromRoom(pRoom) == goalLevel) { best = cur; break; }
		}
		for (int k = 0; k < 8; ++k) {
			int nx = cx + dx[k], ny = cy + dy[k];
			if (nx < 0 || ny < 0 || nx >= side || ny >= side) continue;
			int n = ny * side + nx;
			if (prev[n] != -2) continue;
			// Diagonal steps must not cut a blocked corner.
			if (k >= 4 && (!walkable(x0 + cx + dx[k], y0 + cy) || !walkable(x0 + cx, y0 + cy + dy[k]))) continue;
			if (!walkable(x0 + nx, y0 + ny)) continue;
			prev[n] = cur;
			open.push_back(n);
		}
	}
	std::vector<RoutePoint> route;
	for (int c = best; c != -1 && c != start; c = prev[c]) route.push_back({x0 + c % side, y0 + c / side});
	std::vector<RoutePoint> forward(route.rbegin(), route.rend());
	return forward;
}
