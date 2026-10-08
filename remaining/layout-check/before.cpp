#include "Map-before.h"
#include <cstddef>
extern "C" const size_t layout[] = {sizeof(Map), offsetof(Map, resourceStocks), offsetof(Map, materialSourceCounts), offsetof(Map, idleGradientBuffers)};
