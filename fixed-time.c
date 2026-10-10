#include <time.h>
#include <stdlib.h>
time_t time(time_t *out) { time_t t = (time_t)strtol(getenv("TEST_TIME_SEED"),0,10); if(out)*out=t; return t; }
