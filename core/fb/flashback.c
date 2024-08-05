#include <core\inc\cmn.h>
#include <core\inc\ubi.h>
#include "cache.h"
#include "flashback.h"
#include <core\inc\ubi.h>
#include <core\inc\ftl.h>
#include <stdlib.h>
#include <math.h> //ceil

typedef unsigned long long int UINT64;

CACHE* permissionPages;
CACHE* currentQueue;
BOOL setup = FALSE;
PGADDR currentHead = 0;
char systemType;
PGADDR lbaStart;
UINT16 inode_size, inodes_per_page, group_count, search_size;
UINT32 start, stride, fbcflag, fbclen;
UINT32 FB_CACHESIZE = 80;
BOOL ext3 = FALSE;
UINT8* UUID;

void FB_PrintWord(PGADDR pg, UINT32 addr)
{
  UINT8 data_buffer[MPP_SIZE];
  PGADDR block, page;
  STATUS ret = PMT_Search(pg, &block, &page);
  if (ret == STATUS_SUCCESS) {
     ret = UBI_Read(block, page, data_buffer, NULL);
  }
  uart_printf("%d-%d-%d-%d\n", data_buffer[addr], data_buffer[addr + 1], data_buffer[addr + 2], data_buffer[addr + 3]);
}

BOOL FB_isPermission(PGADDR addr, UINT8* buf)
{
  if(!setup) return FALSE;
  
  if(CACHE_Contains(permissionPages, addr))
  {
     CACHE_Add(currentQueue, addr);
     return TRUE;
  }
  return FALSE;
}
     
void FB_Setup(UINT8* data_buffer)
{
  currentQueue = CACHE_Create(FB_CACHESIZE, 0);
  //Partition code. We need to check this to figure out whether it's using MBR or GPT.
    UINT8 type = data_buffer[0x1C2];
    
    //uart_printf("system type: %d\n", type);
    
    if(type == 0x83) {
      systemType = 'E';
      start = 3;
      fbcflag = 26;
      fbclen = 2;
    }
    else if(type == 0x07)
    {
      systemType = 'N';
    }
    
    
    //Grab the LBA starting section. Annoyingly, this is little-endian.
    lbaStart = (data_buffer[0x1c6]) |
    (data_buffer[0x1c7] << 8) |
    (data_buffer[0x1c8] << 16) |
    (data_buffer[0x1c9] << 24);
    
    //Convert our LBA from step 1 into a page address
    PGADDR initPage = (int)(lbaStart * ((double)SECTOR_SIZE/PAGE_SIZE));
    
  if(systemType == 'E') {
    
    //Now add 1024 bytes' worth of page size to our setup, as we're looking for a section starting at byte 1024.
    initPage += 1024/PAGE_SIZE;
    
    PGADDR block, page;
    
    //Read the superblock
    STATUS ret = PMT_Search(initPage, &block, &page);
    UINT8 data_buffer[MPP_SIZE];
    if (ret == STATUS_SUCCESS) {
      ret = UBI_Read(block, page, data_buffer, NULL);
    }
    
    UINT32 initialOffset = 1024 % PAGE_SIZE;
    
    //Read some variables from the superblock.
    UINT32 block_size = (data_buffer[initialOffset + 24]) |
    (data_buffer[initialOffset + 25] << 8) |
    (data_buffer[initialOffset + 26] << 16) |
    (data_buffer[initialOffset + 27] << 24);
    
    search_size = (data_buffer[initialOffset + 40]) |
    (data_buffer[initialOffset + 41] << 8) |
    (data_buffer[initialOffset + 42] << 16) |
    (data_buffer[initialOffset + 43] << 24);
    
    UINT32 totalInodes = (data_buffer[initialOffset + 0]) |
    (data_buffer[initialOffset + 1] << 8) |
    (data_buffer[initialOffset + 2] << 16) |
    (data_buffer[initialOffset + 3] << 24);
    
    //If the data in bytes 76-79 is greater than 0, we need to check for inode size
    if(data_buffer[initialOffset + 76] > 0 || data_buffer[initialOffset + 77] > 0 
       || data_buffer[initialOffset + 78] > 0 || data_buffer[initialOffset + 79] > 0)
    {
      inode_size = data_buffer[initialOffset + 88] | data_buffer[initialOffset + 89] << 8;
      stride = inode_size;
      uart_printf("inode_size: %d\n", inode_size);
    }
    
    //This field is only used after EXT2, so if it has a value we can say that we're on a new version.
    if(data_buffer[initialOffset + 252] != 0)
    {
      ext3 = TRUE;
    }
    
    inodes_per_page = (int) ceil((double)PAGE_SIZE / inode_size);
    
    //Initialize the inodePageIDs array. We'll be filling this now.
    //inodePageIDs = malloc(sizeof(PGADDR) * (totalInodes/search_size));
    
    group_count = (int) ceil((double)totalInodes / search_size);

    permissionPages = CACHE_Create(group_count, search_size / inodes_per_page - 1);
    
    if(ext3)
    {
      UUID = malloc(sizeof(UINT8) * 768);
    }
    
    for(int j = 0; j < 768; j++)
    {
      UUID[j] = data_buffer[initialOffset + 256 + j];
    }
    
    //Part 3: The block group descriptors.
    /*Begin by finding the offset for the first block after the superblock.
    The first descriptor begins at byte 2048 of the partition OR the second block, whichever is greater.
    Note that our initialPage assumes we're starting at byte 1024, so we only need go
    1024 bytes further.
    */
    //Block Group Descriptor Initial Byte
    UINT32 bgdIB;
    if(block_size)
    {
      bgdIB = (1024 << block_size) * 2;
    }
    else
    {
      bgdIB = 2048; 
    }
    
    //Calculate the starting page and offset of the group descriptor table.
    PGADDR curPage = initPage + (bgdIB / PAGE_SIZE);
    UINT32 curOffset = bgdIB % PAGE_SIZE;
    UINT32 curTable;
    
    //Read the first page if applicable
    if(curPage != initPage)
    {
      ret = PMT_Search(curPage, &block, &page);
        if (ret == STATUS_SUCCESS) {
          ret = UBI_Read(block, page, data_buffer, NULL);
      }
    }
    
    PGADDR pageTrans;
    
    //inodePageIDs = malloc(group_count * sizeof(PGADDR));
    
    //Grab the start block for each inode table and convert to pages.
    for(int i = 0; i < group_count; i++)
    {
      //Begin by grabbing the current table.
      curTable = data_buffer[curOffset + 8] | data_buffer[curOffset + 9] << 8
        | data_buffer[curOffset + 10] << 16 | data_buffer[curOffset + 11] << 24;
      
      pageTrans = initPage + (int)(curTable * ((1024 << block_size)/ (double) PAGE_SIZE));
      
      CACHE_Add(permissionPages, pageTrans);
      uart_printf("Found group at %d\n", pageTrans);
      
      curOffset += 32;
      
      //If we've exited the current page.
      if(curOffset >= PAGE_SIZE)
      {
        curPage++;
        
        ret = PMT_Search(curPage, &block, &page);
        if (ret == STATUS_SUCCESS) {
          ret = UBI_Read(block, page, data_buffer, NULL);
        }

        
        curOffset %= PAGE_SIZE;
      }
    }
    
    setup = TRUE;
  }    
}     

void FB_Rollback()
{
  PHY_BLOCK block, block2;
  PAGE_OFF page, page2;
  STATUS ret;
  UINT8 buf1[MPP_SIZE];
  UINT8 buf2[MPP_SIZE];
  SPARE spare;
  uart_printf("Rolling back a cache. Size: %d\n", currentQueue->head);
  
  buf1[0] = 0x00;
  //Because for some unfathomable reason, the only way to time this crap is polling from the OS.
  FTL_Write(235001, buf1);
  
  //Commenting the isRollingBack line generates its own test data and I can't be bothered to do better.
  isRollingBack = TRUE;
  for(int i = 0; i < currentQueue->head; i++)
  {
    ret = PMT_Search(currentQueue->contents[i], &block, &page);
    if(ret == STATUS_SUCCESS)
    {
      ret = UBI_Read(block, page, &buf1, spare);
      if(ret == STATUS_FAILURE || spare[2] == 0)
      {
        continue;
      }
      page2 = spare[2] % 64;
      block2 = spare[2] / 64;
      ret = UBI_Read(block2, page2, &buf2, NULL);
      UINT8 type1, type2;
      
      uart_printf("Investigating %d. Current page: %d, original page: %d\n", currentQueue->contents[i], block*64+page, spare[2]);
      
      if(systemType == 'E')
      {
        for(int j = 0; j < MPP_SIZE; j+= inode_size)
        {
          type1 = buf1[j + 1];
          type2 = buf2[j + 1];
          if(type1 >= 0x10 && type1 < 0xD0 && type2 >= 0x10 && type2 < 0xD0)
          {
            buf1[j] = buf2[j];
            buf1[j + 1] = buf2[j + 1];
            uart_printf("Accepting. Current: %d, original: %d\n", buf1[j + 1] << 8 | buf1[j], buf2[j + 1] << 8 | buf2[j]);
          }  
        }
      }
      FTL_Write(currentQueue->contents[i], buf1);
    }
  }
  CACHE_Reset(currentQueue);
  buf1[0] = 0xFF;
  //Because for some unfathomable reason, the only way to time this crap is polling from the OS.
  FTL_Write(235001, buf1);
  isRollingBack = FALSE;
  currentHead++;
}

void FB_Clear()
{
  CACHE_Clear(currentQueue);
  currentHead += 2;
}