Credit to OpenNFM for the initial FTL software this project is based on.

This is code meant for an LPC-H3131 board to be compiled using IAR embedded workbench. No further results can be guaranteed. At current, a compiled binary can be found in /prj/iar/debug/exe if you want to flash directly to the drive.

This firmware is meant to run on EXT2/EXT3 and track and undo changes to permissions. It is untested in terms of WLI, so its effects on drive health are unknown for long term use. In the short term, you should see <10% reduction in IO throughput relative to unmodified openNFM.

This firmware accepts commands via a write to page 235000 of the drive. This can be done in linux using the pread and pwrite commands over /dev/sdb (or sdc, or sdd, whichever drive the LPC mounts as). The first byte of the page is set for the command ID. They are as follows:

1: Set up the drive, using a size of [second byte of the page] for the number of writes which are stored in cache. Note that overfilling this will result in future writes being discarded.

2: Undo any permission writes present in the cache and clear it.

3: Clear the cache.
