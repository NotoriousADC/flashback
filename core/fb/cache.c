#include "cache.h"
#include <sys\sys.h>
#include <stdlib.h>

UINT32 __CACHE_Header = 0;

void CACHE_Add(CACHE* cache, PGADDR page)
{
  if(cache->head != cache->size) {
    cache->contents[cache->head] = page;
    cache->head++;
  }
}

CACHE* CACHE_Create(UINT32 size, UINT32 range)
{
  CACHE* cache = malloc(sizeof(CACHE));
  cache->contents = malloc(sizeof(PGADDR)*size);
  cache->range = range;
  cache->size = size;
  cache->head = 0;
  return cache;
}

void CACHE_Reset(CACHE* cache)
{
  cache->head = 0;
}

BOOL CACHE_Contains(CACHE* cache, PGADDR page)
{
  UINT32 low = 0;
  UINT32 high = cache->head - 1;
  UINT32 mid;
  PGADDR comp;
  //Second condition handles underflow
  while(low < high && high < cache->head)
  {
    mid = (low + high)/2;
    comp = cache->contents[mid];
    if(page >= comp && page < comp + cache->range)
    {
      return TRUE;
    }
    else if(page < comp)
    {
      high = mid - 1;
    }
    else
    {
      low = mid + 1;
    }
  }
  
  if(page >= cache->contents[low] && page < cache->contents[low] + cache->range)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

CACHE* CACHE_Clear(CACHE* cache)
{
  free(cache->contents);
  free(cache);
  return 0;
}