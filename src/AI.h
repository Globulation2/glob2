/*

 * Globulation 2 AI support

 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon

 */



#ifndef __AI_H

#define __AI_H



#include "GAG.h"

#include "Order.h"



class AI

{

public:

	AI();

	AI(SDL_RWops *stream);



	// get order from AI, return NullOrder if

	Order *getOrder(void);



	void save(SDL_RWops *stream);

	void load(SDL_RWops *stream);

};



#endif

 

