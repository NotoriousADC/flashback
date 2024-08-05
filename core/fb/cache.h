#include <core\inc\cmn.h>
#include <core\inc\buf.h>
#include <sys\sys.h>
#ifndef FB_CACHE
#define FB_CACHE
struct CACHE{
  UINT32 size;
  UINT32 head;
  PGADDR* contents;
  UINT32 range;
};

typedef struct CACHE CACHE;

CACHE* CACHE_Create(UINT32 size, UINT32 range);

void CACHE_Add(CACHE* cache, PGADDR page);

BOOL CACHE_Contains(CACHE* cache, PGADDR page);

CACHE* CACHE_Clear(CACHE* cache);

void CACHE_Reset(CACHE* cache);

#endif