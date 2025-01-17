/*********************************************************
 * Module name: mtd_api.c
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
 *    Memory Technology Device. Translate block and
 *    sector offset to physical chip/row/column address.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "mtd_nand.h"

UINT32 erase_counts[4096] = {0};

#if (SIM_TEST == FALSE)
#include "arm_comm.h"
//extern void Dly_us(Int32U Dly);
#endif

#define TRANS_PHY_BLOCK(b)             \
((((b)&(TOTAL_DIE_COUNT-1))<<BLOCK_PER_PLANE_SHIFT) | \
  ((b)>>TOTAL_DIE_SHIFT))
#define MTD_REAL_BLOCK(b, p)           \
(((TRANS_PHY_BLOCK((b)))<<PLANE_PER_DIE_SHIFT)+(p))
#define MTD_ROW_ADDRESS(block, plane, page)    \
(((MTD_REAL_BLOCK((block), (plane)))<<PAGE_PER_BLOCK_SHIFT)+(page))

#define MTD_DIE_NUM(b)        ((b)&(DIE_PER_CHIP-1))
#define MTD_CHIP_NUM(b)       (((b)>>DIE_PER_CHIP_SHIFT)&(CHIP_COUNT-1))
#define MTD_MAX_RETRY_TIMES   (3)

static STATUS mtd_readstatus(PHY_BLOCK block, UINT8* status_byte);

/* TODO: exploit other NAND feature 
 * - copy back for reclaim, read/write pages in the same plane/die
 * - cache read/write
 * - de-select CE when free
 * - ONFI2/3 ...
 */

void MTD_Init() {
  NAND_Init();
  /* reset all nand chips */
  MTD_Reset();
}

/* reset all flash chips */
void MTD_Reset() {
  NAND_CHIP chip;

  for (chip = 0; chip < CHIP_COUNT; chip++) {
    NAND_SelectChip(chip);
    NAND_SendCMD(CMD_RESET);
  }
}

STATUS MTD_ReadID(NAND_CHIP chip, UINT8 id_data[], UINT8 bytes) {
  NAND_COL default_col = 0;
  NAND_ROW default_row = 0;
  STATUS ret = STATUS_SUCCESS;

  NAND_SelectChip(chip);
  NAND_SendCMD(CMD_READ_ID);
  /* read ID only need to send one cycle row address */
  NAND_SendAddr(default_col, default_row, 0, 1);
  NAND_ReceiveBytes(id_data, bytes);

  return ret;
}

STATUS MTD_Read(PHY_BLOCK block, PAGE_OFF page, void* buffer, SPARE spare) {
  STATUS ret = STATUS_SUCCESS;
  BOOL ecc_corrected;
  UINT8 ecc_error_count;
  UINT8 retry_times = 0;
  NAND_ROW row_addr = 0;
  NAND_CHIP chip_addr = 0;

  /* check status and wait ready of the DIE to read, avoid RWW issue */
  (void) MTD_WaitReady(block);

  while (retry_times < MTD_MAX_RETRY_TIMES) {
    if (ret == STATUS_SUCCESS) {
      row_addr = (NAND_ROW) MTD_ROW_ADDRESS(block, 0, page);
      chip_addr = (NAND_CHIP) MTD_CHIP_NUM(block);

      /* select and check ready */
      NAND_SelectChip(chip_addr);
      NAND_SendCMD(CMD_READ);
      NAND_SendAddr(0, row_addr, CFG_NAND_COL_CYCLE, CFG_NAND_ROW_CYCLE);
      NAND_SendCMD(CMD_READ_COMMIT);
      NAND_WaitRB(chip_addr);
    }

    if (ret == STATUS_SUCCESS) {
      /* receive page data */
      ret = NAND_ReceiveData(buffer, spare);
      if (ret != STATUS_SUCCESS) {
        /* TODO: check ecc handler algo */
        ecc_corrected = NAND_ECCStatus(&ecc_error_count);
        if (ecc_corrected == TRUE) {
          /* error is corrected */
          ret = STATUS_SUCCESS;
        } else {
          /* un-correctable, re-try before report error */
          ret = STATUS_FAILURE;
          break;
        }
      }

      /* send next page in MPP */
      if (buffer != NULL) {
        buffer = ((UINT8*) buffer) + PAGE_SIZE;
      }
    }

    if (ret == STATUS_FAILURE && retry_times < MTD_MAX_RETRY_TIMES - 1) {
      /* try again */
      retry_times++;
      ret = STATUS_SUCCESS;
      continue;
    } else {
      break;
    }
  }

  return ret;
}

STATUS MTD_Program(PHY_BLOCK block, PAGE_OFF page, void* buffer, SPARE spare) {
  NAND_ROW row_addr;
  NAND_CHIP chip_addr;
  STATUS ret = STATUS_SUCCESS;

  row_addr = (NAND_ROW) MTD_ROW_ADDRESS(block, 0, page);
  chip_addr = (NAND_CHIP) MTD_CHIP_NUM(block);

  NAND_SelectChip(chip_addr);
  NAND_SendCMD(CMD_PAGE_PROGRAM);
  NAND_SendAddr(0, row_addr, CFG_NAND_COL_CYCLE, CFG_NAND_ROW_CYCLE);
  NAND_SendData(buffer, spare);

  /* commit the whole write, multi-plane or one-plane write */
  NAND_SendCMD(CMD_PAGE_PROGRAM_COMMIT);

  return ret;
}

STATUS MTD_Erase(PHY_BLOCK block) {
  NAND_ROW row_addr;
  NAND_CHIP chip_addr = INVALID_CHIP;
  UINT8 retry_times = 0;
  STATUS ret = STATUS_SUCCESS;
  
  //Only apply if the block is good. We don't care about bad block erases.
  if(~(erase_counts[block]))
    erase_counts[block]++;

  while (retry_times < MTD_MAX_RETRY_TIMES) {
    row_addr = (NAND_ROW) MTD_ROW_ADDRESS(block, 0, 0);
    chip_addr = (NAND_CHIP) MTD_CHIP_NUM(block);

    NAND_SelectChip(chip_addr);
    NAND_SendCMD(CMD_BLOCK_ERASE);
    NAND_SendAddr(0, row_addr, 0, CFG_NAND_ROW_CYCLE);
    NAND_SendCMD(CMD_BLOCK_ERASE_COMMIT);

    ASSERT(chip_addr != INVALID_CHIP);
    NAND_WaitRB(chip_addr);

    /* check status */
    ret = MTD_ReadStatus(block);
    if (ret != STATUS_SUCCESS) {
      /* try again */
      retry_times++;
      continue;
    } else {
      break;
    }
  }

  return ret;
}

STATUS MTD_CheckBlock(PHY_BLOCK block) {
  UINT8 read_byte;
  NAND_ROW row_addr = MAX_UINT8;
  NAND_CHIP chip_addr;
  PAGE_OFF page;
  STATUS ret = STATUS_SUCCESS;

  /* only check the bad block byte in the first 2 pages */
  for (page = 0; page < 2; page++) {
    if (ret == STATUS_SUCCESS) {
      row_addr = (NAND_ROW) MTD_ROW_ADDRESS(block, 0, 0);
      chip_addr = (NAND_CHIP) MTD_CHIP_NUM(block);

      /* read the page */
      NAND_SelectChip(chip_addr);
      NAND_SendCMD(CMD_READ);
      NAND_SendAddr(0, row_addr, CFG_NAND_COL_CYCLE, CFG_NAND_ROW_CYCLE);
      NAND_SendCMD(CMD_READ_COMMIT);

      /* wait on read data ready */
      NAND_WaitRB(chip_addr);
    }

    if (ret == STATUS_SUCCESS) {
      ASSERT(row_addr != MAX_UINT8);

      /* read the first byte in spare area */
      NAND_SendCMD(CMD_RANDOM_DATA_OUT);
      NAND_SendAddr(PAGE_SIZE, row_addr, CFG_NAND_COL_CYCLE, 0);
      NAND_SendCMD(CMD_RANDOM_DATA_OUT_COMMIT);

      /* only read one byte */
      NAND_ReceiveBytes(&read_byte, 1);

      if (read_byte != 0xff) {
        /* this block is marked as BAD block */
        ret = STATUS_BADBLOCK;
      }
    }
  }

  return ret;
}

STATUS MTD_ReadStatus(PHY_BLOCK block) {
  UINT8 status_byte;
  STATUS ret = STATUS_SUCCESS;

  /* sort the block in die interleave way */
  ASSERT(block < CFG_LOG_BLOCK_COUNT);

  ret = mtd_readstatus(block, &status_byte);
  if (ret == STATUS_SUCCESS) {
    if (NAND_STATUS_BUSY(status_byte) == TRUE) {
      ret = STATUS_DIE_BUSY;
    } else if (NAND_STATUS_FAIL(status_byte) == TRUE) {
      ret = STATUS_BADBLOCK;
    } else {
      ret = STATUS_SUCCESS;
    }
  }

  return ret;
}

STATUS MTD_WaitReady(PHY_BLOCK block) {
  STATUS ret;

  /* sort the block in die interleave way */
  ASSERT(block < CFG_LOG_BLOCK_COUNT);

  do {
    ret = MTD_ReadStatus(block);
  } while (ret == STATUS_DIE_BUSY);

  return ret;
}

static STATUS mtd_readstatus(PHY_BLOCK block, UINT8* status_byte) {
  NAND_CHIP chip_addr;
  NAND_CMD die_status;
  STATUS ret = STATUS_SUCCESS;

  if (ret == STATUS_SUCCESS) {
    chip_addr = (NAND_CHIP) MTD_CHIP_NUM(block);
#if (DIE_PER_CHIP_SHIFT == 0)
    /* single die */
    die_status = CMD_READ_STATUS;
#else
    /* dual die in one ce, different cmd to poll status */
    if (MTD_DIE_NUM(block) == 0)
    {
      die_status = CMD_READ_STATUS_DIE1;
    }
    else
    {
      die_status = CMD_READ_STATUS_DIE2;
    }
#endif

    NAND_SelectChip(chip_addr);
    NAND_SendCMD(die_status);

    NAND_ReceiveBytes(status_byte, 1);
  }

  return ret;
}
