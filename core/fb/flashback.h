#include <core\inc\cmn.h>
#include "cache.h"

#ifndef FLASHBACK
#define FLASHBACK

extern CACHE* permissionPages;
extern CACHE* currentQueue;

#define adjustBytes(a, b, c, d) (a) | (b << 8) | (c << 16) | (d << 24)

extern BOOL setup;

extern BOOL isRollingBack;

extern PGADDR currentHead;

extern char systemType;

extern PGADDR lbaStart;

extern UINT32 start, stride, fbcflag, fbclen;

extern UINT16 inode_size, inodes_per_page, group_count, search_size;

void FB_Setup();

void FB_Rollback();

BOOL FB_isPermission(PGADDR addr, UINT8* buf);

void FB_PrintWord(PGADDR pg, UINT32 addr);

void FB_Clear();

extern UINT32 FB_CACHESIZE;

#endif