// stands in for reaper's libSwell.so and hands out the host's fake_ functions
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// a page of this library's file mapped shared and read only, which no one may make writable
static void* lockedPage(void)
{
    Dl_info self;
    if (!dladdr((void*)lockedPage, &self))
        return NULL;
    int file = open(self.dli_fname, O_RDONLY);
    void* page = file < 0 ? MAP_FAILED : mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_EXEC, MAP_SHARED, file, 0);
    if (file >= 0)
        close(file);
    return page == MAP_FAILED ? NULL : page;
}

__attribute__((visibility("default"))) void* SWELLAPI_GetFunc(const char* name)
{
    const char* missing = getenv("FAKE_SWELL_MISSING");
    if (missing && !strcmp(missing, name))
        return NULL;
    const char* locked = getenv("FAKE_SWELL_LOCKED");
    if (locked && !strcmp(locked, name))
        return lockedPage();
    char symbol[128];
    snprintf(symbol, sizeof(symbol), "fake_%s", name);
    return dlsym(RTLD_DEFAULT, symbol);
}
