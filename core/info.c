#include <stdlib.h>
#include "info.h"
void info_free(Doc *d) { free(d->info); d->info = NULL; }
