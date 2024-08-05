/*********************************************************
 * Module name: ftl_api.c
 *
 * Copyright 2010, 2011. All Rights Reserved, Crane Chu.
 *
 * This file is part of OpenNFM.
 *
 * OpenNFM is free software: you can redistribute it and/or 
 * modify it under the terms of the GNU General Public 
 * License as published by the Free Software Foundation, 
 * either version 3 of the License, or (at your option) any 
 * later version.
 * 
 * OpenNFM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied 
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE. See the GNU General Public License for more 
 * details.
 *
 * You should have received a copy of the GNU General Public 
 * License along with OpenNFM. If not, see 
 * <http://www.gnu.org/licenses/>.
 *
 * First written on 2010-01-01 by cranechu@gmail.com
 *
 * Module Description:
 *    FTL APIs.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\ftl.h>
#include <core\inc\ubi.h>
//#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "ftl_inc.h"
#include <core\inc\buf.h>
#include <core\fb\flashback.h>

/* Advanced Page Mapping FTL:
 * - Block Dirty Table: LOG_BLOCK 0, cache all
 * - ROOT Table: LOG_BLOCK 1, cache all. point to journal blocks.
 * - Page Mapping Table: LOG_BLOCK 2~N, cache x pages with LRU algo.
 * - DATA Journal: commit
 * - Init: read BDT, ROOT, PMT, Journal info, ...
 * - Reclaim
 * - Meta Data Page: in last page in PMT blocks and data blocks.
 * - choose journal block on erase and write, according to die index
 *
 * TODO: advanced features:
 * - sanitizing
 * - bg erase
 * - check wp/trim, ...
 */

STATUS FTL_Format() {
  STATUS ret;
    
  ret = UBI_Format();
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = DATA_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = HDI_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = PMT_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = BDT_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = ROOT_Format();
  }

  return ret;
}

STATUS FTL_Init() {
  STATUS ret;

  ret = UBI_Init();
  if (ret == STATUS_SUCCESS) {
    /* scan tables on UBI, and copy to RAM */
    ret = ROOT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = BDT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = PMT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = HDI_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = DATA_Replay(root_table.hot_journal);
  }

  if (ret == STATUS_SUCCESS) {
    ret = DATA_Replay(root_table.cold_journal);
  }

  if (ret == STATUS_SUCCESS) {
    /* handle reclaim PLR: start reclaim again. Some data should
     * be written in the same place, so just rewrite same data in the
     * same page regardless this page is written or not. */

    /* check if hot journal blocks are full */
    if (DATA_IsFull(TRUE) == TRUE) {
      ret = DATA_Reclaim(TRUE);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }

    /* check if cold journal blocks are full */
    if (DATA_IsFull(FALSE) == TRUE) {
      ret = DATA_Reclaim(FALSE);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }
  }

  return ret;
}

STATUS FTL_Write(PGADDR addr, void* buffer) {
  STATUS ret;
  BOOL is_hot = HDI_IsHotPage(addr);
  
  if(addr == 235000)
  {
    if(!setup)
    {
      if(((unsigned char*) buffer)[0] == 1)
      {
        UINT8 db[MPP_SIZE];
        FTL_Read(0, db);
        uart_printf("setting up.\n");
        if(((unsigned char*) buffer)[1] != 255 && ((unsigned char*) buffer)[1] != 0)
        {
          FB_CACHESIZE = ((unsigned char*) buffer)[1];
        }
        else if(((unsigned char*) buffer)[1] != 255)
        {
          FB_CACHESIZE = 80;
        }
        else
        {
          FB_CACHESIZE = 500;
        }
        FB_Setup(db);
        return STATUS_SUCCESS;
      }
    }
    else if(((unsigned char*) buffer)[0] == 2)
    {
        FB_Rollback();
    }
    else if(((unsigned char*) buffer)[0] == 3)
    {
      FB_Clear();
    }
    else if(((unsigned char*) buffer)[0] == 4)
    {
      PGADDR page = ((PGADDR*)buffer)[1] | ((PGADDR*)buffer)[2] << 8 | ((PGADDR*)buffer)[3] << 16 | ((PGADDR*)buffer)[4] << 24;
      PGADDR byte = ((PGADDR*)buffer)[5] | ((PGADDR*)buffer)[6] << 8 | ((PGADDR*)buffer)[7] << 16 | ((PGADDR*)buffer)[8] << 24;
      FB_PrintWord(page, byte);
    }
    else if(((unsigned char*) buffer)[0] == 5)
    {
      for(int i = 0; i < 4096; i++)
      {
        
        if(~(erase_counts[i])) //If erase_counts[i] != 0xFFFFFFFF
          uart_printf("%d, ", erase_counts[i]);
      }
    }
  }
    
  ret = DATA_Write(addr, buffer, is_hot);
  if (ret == STATUS_SUCCESS) {
    if (DATA_IsFull(is_hot) == TRUE) {
      ret = DATA_Reclaim(is_hot);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }
  }
  return ret;
}

STATUS FTL_Read(PGADDR addr, void* buffer) {
  LOG_BLOCK block;
  PAGE_OFF page;
  STATUS ret;

  ret = PMT_Search(addr, &block, &page);
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Read(block, page, buffer, NULL);
  }

  return ret;
}

STATUS FTL_Trim(PGADDR start, PGADDR end) {
  PGADDR addr;
  STATUS ret = STATUS_SUCCESS;

  for (addr = start; addr <= end; addr++) {
    ret = FTL_Write(addr, NULL);
    if (ret != STATUS_SUCCESS) {
      break;
    }
  }

  return ret;
}

STATUS FTL_SetWP(PGADDR laddr, BOOL enabled) {
  return STATUS_FAILURE;
}

BOOL FTL_CheckWP(PGADDR laddr) {
  return FALSE;
}

STATUS FTL_BgTasks() {
  return STATUS_SUCCESS;
}

PGADDR FTL_Capacity() {
  LOG_BLOCK block;

  block = UBI_Capacity;//3989
  block -= JOURNAL_BLOCK_COUNT; /* data hot journal *///1
  block -= JOURNAL_BLOCK_COUNT; /* data cold journal *///1
  block -= JOURNAL_BLOCK_COUNT; /* data reclaim journal *///1
  block -= PMT_BLOCK_COUNT; /* pmt blocks *///40
  block -= 2; /* bdt blocks */
  block -= 2; /* root blocks */
  block -= 2; /* hdi reserved */
  block -= block / 100 * OVER_PROVISION_RATE; /* over provision */
  
  uart_printf("%s: UBI_Capacity=%d\r\n",__func__,UBI_Capacity);
  uart_printf("%s: actual user capacity: block=%d\r\n",__func__,block);//3823

  /* last page in every block is reserved for meta data collection */
  return block * (PAGE_PER_PHY_BLOCK - 1);//471
}

STATUS FTL_Flush() {
  STATUS ret;

  ret = DATA_Commit();
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Flush();
  }

  if (ret == STATUS_SUCCESS) {
    ret = UBI_SWL();
  }

  return ret;
}
