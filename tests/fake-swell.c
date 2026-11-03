// stands in for reaper's libSwell.so and hands out the host's fake_ functions
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((visibility("default"))) void* SWELLAPI_GetFunc(const char* name)
{
    const char* missing = getenv("FAKE_SWELL_MISSING");
    if (missing && !strcmp(missing, name))
        return NULL;
    char symbol[128];
    snprintf(symbol, sizeof(symbol), "fake_%s", name);
    return dlsym(RTLD_DEFAULT, symbol);
}
