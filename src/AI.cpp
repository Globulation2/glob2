/*
 * Globulation 2 AI support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "AI.h"


AI::AI()
{

}

AI::AI(SDL_RWops *stream)
{
	load(stream);
}

Order *AI::getOrder(void)
{
	return new NullOrder();
}

void AI::save(SDL_RWops *stream)
{

}
void AI::load(SDL_RWops *stream)
{

}
