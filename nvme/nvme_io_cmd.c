//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos OpenSSD.
//
// Cosmos OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos OpenSSD
// Design Name: Cosmos Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"

#include "../lru_buffer.h"
#include "../trans_buffer.h"
#include "../memory_map.h"
#include "../low_level_scheduler.h"

unsigned int requests;

void handle_nvme_io_trans(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
  // ----------------------------------------------------------------------
  // Parse NVMe Command
  // ----------------------------------------------------------------------
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	if(writeInfo12.FUA == 1)
		xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x7) == 0 && (nvmeIOCmd->PRP2[0] & 0x7) == 0);

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;

	/*
	 * Need to separate the requested sector into both a TableID and a RequestID.
	 *
	 * Let's assume tables are larger than 1K sectors (4MB), and 4M aligned. This way
	 * we can assume (reqSect / 1K)*1K = TableSeq and reqSect % 1K = RequestID.
	 */
	unsigned int tableSLBA = (hostCmd.curSect / 1000) * 1000;
	unsigned int requestID = hostCmd.curSect % 1000;

	unsigned int entryIdx = AllocateTransBufEntry(tableSLBA, requestID);
	ASSERT(entryIdx != 0xffff);

	unsigned int devAddr = TRANS_CONFIG_ADDR + entryIdx * TRANS_CONFIG_SIZE;
	unsigned int dmaIndex = 0;
	unsigned int sectorOffset = 0;
	while(sectorOffset < hostCmd.reqSect)
	{
		set_auto_rx_dma(cmdSlotTag, dmaIndex, devAddr);
		sectorOffset++;
		if (++dmaIndex >= 256) dmaIndex = 0;
		devAddr += SECTOR_SIZE_FTL;
	}

	transMap->bufEntry[entryIdx].rxDmaExe = 1;
	transMap->bufEntry[entryIdx].rxDmaTail = g_hostDmaStatus.fifoTail.autoDmaRx;
	transMap->bufEntry[entryIdx].rxDmaOverFlowCnt = g_hostDmaAssistStatus.autoDmaRxOverFlowCnt;

	XTime_GetTime(&transMap->bufEntry[entryIdx].configWriteRequested);

	PushToTransReqQueue(entryIdx);

	reservedReq = 1;
}

void handle_nvme_io_read_trans(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;


	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x7) == 0 && (nvmeIOCmd->PRP2[0] & 0x7) == 0); //error

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;

	/*
	 * Need to separate the requested sector into both a TableID and a RequestID.
	 *
	 * Let's assume tables are larger than 1K sectors (4MB), and 4M aligned. This way
	 * we can assume (reqSect / 1K)*1K = TableSeq and reqSect % 1K = RequestID.
	 */
	//unsigned int tableSLBA = (hostCmd.curSect / 1000) * 1000;
	unsigned int requestID = hostCmd.curSect % 1000;

	// ----------------------------------------------------------------------
	// Return result pages.
	// ----------------------------------------------------------------------
	int entryIdx = findTransBufEntry(requestID);
	ASSERT(entryIdx >= 0);

	XTime xtime = 0;
	XTime_GetTime(&xtime);
	int sector;
	for (sector = transMap->bufEntry[entryIdx].nlbRequested;
		 sector < transMap->bufEntry[entryIdx].nlbRequested + hostCmd.reqSect;
		 sector++)
	{
		transMap->bufEntry[entryIdx].sectorRequested[sector] = xtime;
	}

	PushToTransReadReqQueue(entryIdx, hostCmd.cmdSlotTag, hostCmd.reqSect);

	reservedReq = 1;
}

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;


	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	if (readInfo12.reserved0 == 1)
	{
		handle_nvme_io_read_trans(cmdSlotTag, nvmeIOCmd);
		return;
	}

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x7) == 0 && (nvmeIOCmd->PRP2[0] & 0x7) == 0); //error

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;

	LRUBufRead(&hostCmd);
}

void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	if (writeInfo12.reserved0 == 1)
	{
		handle_nvme_io_trans(cmdSlotTag, nvmeIOCmd);
		return;
	}

	if(writeInfo12.FUA == 1)
		xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x7) == 0 && (nvmeIOCmd->PRP2[0] & 0x7) == 0);

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;

	LRUBufWrite(&hostCmd);
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;

	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case IO_NVM_FLUSH:
		{
			xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			EmptyReqQ();

			// Calculate/Print/Reset Stats
			if (transStats->requests > 0)
			{
				xil_printf("Average Request Latency (us): %ld\r\n",
						(long int)(transStats->requestLatency / transStats->requests));
				xil_printf("Average Config Write Latency (us): %ld\r\n",
						(long int)(transStats->configWriteLatency / transStats->requests));
				xil_printf("Average Config Process Latency (us): %ld\r\n",
						(long int)(transStats->configProcessLatency / transStats->requests));
				xil_printf("Average Request Bandwidth (B/s): %ld\r\n",
						(long int)(1000000 * (transStats->sectors * SECTOR_SIZE_FTL) /
						transStats->requestLatency));
				xil_printf("Average Flash Read Latency (Page-16KB) (us): %ld\r\n",
						(long int)(transStats->flashReadLatency / transStats->pages));
				xil_printf("Average Flash Read Bandwidth (B/s): %ld\r\n",
						(long int)(1000000 * (transStats->pages * PAGE_SIZE) /
						transStats->flashReadLatency));
				xil_printf("Average Translation Latency (Page-16KB) (us): %ld\r\n",
						(long int)(transStats->translationLatency / transStats->pages));
				xil_printf("Average Translation Bandwidth (B/s): %ld\r\n",
						(long int)(1000000 * (transStats->pages * PAGE_SIZE) /
						transStats->translationLatency));
				xil_printf("Total Read Latency (us): %ld\r\n",
						(long int)transStats->totalReadLatency);
				xil_printf("Average Return Latency (Sector-4KB) (us): %ld\r\n",
						(long int)(transStats->returnLatency / transStats->sectors));
				xil_printf("Average Return Bandwidth (B/s): %ld\r\n",
						(long int)(1000000 * (transStats->sectors * SECTOR_SIZE_FTL) /
						transStats->returnLatency));
				xil_printf("Embedding Cache Hitrate (%%): %ld\r\n",
						(long int)((transStats->cache_hits /
						 (transStats->cache_hits + transStats->cache_misses))*
						100));
			}
			transStats->requestLatency = 0;
			transStats->configWriteLatency = 0;
			transStats->configProcessLatency = 0;
			transStats->requests = 0;
			transStats->flashReadLatency = 0;
			transStats->translationLatency = 0;
			transStats->pages = 0;
			transStats->returnLatency = 0;
			transStats->sectors = 0;
			transStats->totalReadLatency = 0;
			transStats->cache_hits = 0;
			transStats->cache_misses = 0;
			break;
		}
		case IO_NVM_WRITE:
		{
			//xil_printf("IO Write Command\r\n");
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
			//xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_TRANS:
		{
			xil_printf("Command Deprecated: %X\r\n", opc);
			ASSERT(0);
			//xil_printf("IO Translate Command\r\n");
			handle_nvme_io_trans(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ_TRANS:
		{
			xil_printf("Command Deprecated: %X\r\n", opc);
			ASSERT(0);
			//xil_printf("IO Translate Read Command\r\n");
			handle_nvme_io_read_trans(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X\r\n", opc);
			ASSERT(0);
			break;
		}
	}
}

