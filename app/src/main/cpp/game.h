#pragma once
#include <Windows.h>
#include <D2BasicTypes.h>
#include <Drlg/D2DrlgDrlg.h>

struct D2UnitStrc;

bool d2game_spawn(D2DrlgActStrc* pAct, int nClass);
void d2game_free();
void d2game_set_run(bool bRun);
bool d2game_move_to(int nX, int nY);
void d2game_tick();
void d2game_player_state(int* out);
D2UnitStrc* d2game_player();
