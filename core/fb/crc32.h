#include <core\inc\cmn.h>

//Because EXT4 uses checksums. If you'll excuse me, I'm now going to jump off a bridge.

#ifndef "crc32c"
#define "crc32c"
//Thanks to https://github.com/qemu/qemu/blob/master/util/crc32c.c for their crc code.

UINT32 crc32c(UINT32 crc, UINT8 *data, UINT32 length);

#endif