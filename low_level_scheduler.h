//////////////////////////////////////////////////////////////////////////////////
// low_level_scheduler.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak	<jwkwak@enc.hanyang.ac.kr>
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
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos OpenSSD
// Design Name: Cosmos Firmware
// Module Name: Low Level Scheduler
// File Name: low_level_scheduler.h
//
// Version: v1.4.1
//
// Description:
//   - define parameters and data structure of the low level scheduler
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.4.1
//	 - LLSCommand_ReadLsbPage and LLSCommand_WriteLsbPage are added to access a page containing bad block table
//
// * v1.4.0
//	 - Completion table and error information table are divided according to HP-port which is connected by each flash channel
//
// * v1.3.0
//	 - Busy index and holding request index is deleted from way queue
//   - Header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//   - Way priority table is added for way scheduling
//   - Data structure of die status table is modified
//   - The name of functions for scheduling are modified
//
// * v1.2.0
//   - Data structure of req-queue is modified (DMA done check option and hold flag are deleted)
//
// * v1.1.0
//   - Low level scheduler commands is added  (DMA operation can be executed by low level scheduler)
//   - Busy index is added to way queue
//   - Row address for NAND is added to identify LUN
//   - Data structure of req-queue is modified
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef	Low_Level_Scheduler_H_
#define Low_Level_Scheduler_H_

#include "lru_buffer.h"
#include "init_ftl.h"
#include "trans_buffer.h"


#define REQ_QUEUE_DEPTH	256
#define SUB_REQ_QUEUE_DEPTH	(PAGE_NUM_PER_BLOCK * 2)
#define TRANS_REQ_QUEUE_DEPTH (TRANS_BUF_ENTRY_NUM)
#define TRANS_READ_REQ_QUEUE_DEPTH ((TRANS_BUF_ENTRY_NUM) * 16) // 16 = sectors per buffer / maximum data transfer sectors per command

//ECC error information
#define ERROR_INFO_NUM 11

//Low level scheduler commands
#define LLSCommand_ReadRawPage 100
#define LLSCommand_ReadLsbPage 101
#define LLSCommand_WriteLsbPage 102
#define LLSCommand_RxDMA 150
#define LLSCommand_TxDMA 151

//Status check option
#define NONE 0
#define STATUS_CHECK 1
#define CHECK_STATUS_REPORT 2

//die status
#define DS_IDLE		0
#define DS_EXE		1
#define DS_TR_FAIL	2
#define DS_TR_REEXE	3
#define DS_FAIL		4
#define DS_REEXE	5
#define DS_SUB_EXE		11
#define DS_SUB_TR_FAIL	12
#define DS_SUB_TR_REEXE	13
#define DS_SUB_FAIL		14
#define DS_SUB_REEXE	15

//request status
#define RS_RUNNING	0
#define RS_DONE		1
#define RS_FAIL		2
#define RS_WARNING	3

//queue select
#define REQ_QUEUE	0
#define SUB_REQ_QUEUE	1

//Error info
#define EI_FAIL		0
#define EI_PASS		1
#define EI_WARNING	2

//LUN
#define LUN_0_BASE_ADDR	0x00000000
#define LUN_1_BASE_ADDR	0x00200000

struct reqEntry {
	unsigned int rowAddr;
	unsigned int devAddr;
	unsigned int pageDataBuf;
	unsigned int spareDataBuf;
	unsigned int cmdSlotTag : 16;
	unsigned int startDmaIndex : 16;
	unsigned int statusOption	:	8;
	unsigned int subReqSect	:	8;
	unsigned int bufferEntry : 16;
	unsigned int request : 16;
	unsigned int translate : 1;
	unsigned int transBufferEntry : 8;
	unsigned int reserved : 23;
	unsigned int transPageIdx;
};

struct transReqEntry {
	unsigned int entryIdx;

	unsigned int nextPage;

	unsigned int nextSector;
	unsigned int firstSector;
	unsigned int nlb;
	unsigned int cmdSlotTag;

	unsigned int prev : 16;
	unsigned int next : 16;
};

struct reqArray {
	struct reqEntry reqEntry[REQ_QUEUE_DEPTH][CHANNEL_NUM][WAY_NUM];
};

struct transReqArray {
	struct transReqEntry transReqEntry[TRANS_REQ_QUEUE_DEPTH];
};

struct transReadReqArray {
	struct transReqEntry transReqEntry[TRANS_READ_REQ_QUEUE_DEPTH];
};

struct transPageReqEntry {
	unsigned int transBufferEntry;
	unsigned int pageDataBuf;
	unsigned int transPageIdx;
	unsigned int valid;
};

struct transPageReqArray {
	struct transPageReqEntry transPageReqEntry[CHANNEL_NUM][WAY_NUM];
};

struct subReqEntry {
	unsigned int request;
	unsigned int rowAddr;
	unsigned int pageDataBuf;
	unsigned int spareDataBuf;
	unsigned int statusOption;
};

struct subReqArray {
	struct subReqEntry reqEntry[SUB_REQ_QUEUE_DEPTH][CHANNEL_NUM][WAY_NUM];
};

struct transRqPointerEntry {
	unsigned int head : 16;
	unsigned int tail : 16;
	unsigned int current : 16;
	unsigned int reserved : 16;

	unsigned int availhead : 16;
	unsigned int availtail : 16;
};

struct rqPointerEntry {
	unsigned int front;
	unsigned int rear;
};

struct rqPointerArray
{
	struct rqPointerEntry rqPointerEntry[CHANNEL_NUM][WAY_NUM];
};

struct completeArray {
	unsigned int completeEntry[CHANNEL_NUM_PER_HP_PORT][WAY_NUM];
};


struct errorInfoArray {
	unsigned int errorInfoEntry[CHANNEL_NUM_PER_HP_PORT][WAY_NUM][ERROR_INFO_NUM];
};


struct dieStatusEntry {
	unsigned int dieStatus	:	8;
	unsigned int queueSelect 	:	2;
	unsigned int reqQueueEmpty 	:	1;
	unsigned int subReqQueueEmpty	:	1;
	unsigned int prevWay	:	4;
	unsigned int nextWay 	:	4;
	unsigned int reserved	:	12;
};

struct dieStatusArray {
	struct dieStatusEntry dieStatusEntry[CHANNEL_NUM][WAY_NUM];
};

struct newBadBlockArray {
	unsigned int newBadBlockEntry[REQ_QUEUE_DEPTH][CHANNEL_NUM][WAY_NUM];
};

struct retryLimitArray {
	int retryLimitEntry[CHANNEL_NUM][WAY_NUM];
};

struct wayPriorityEntry {
	unsigned int idleHead	:	4;
	unsigned int idleTail 	:	4;
	unsigned int statusReportHead	:	4;
	unsigned int statusReportTail 	:	4;
	unsigned int nvmeDmaHead	:	4;
	unsigned int nvmeDmaTail	:	4;
	unsigned int nandTriggerHead	:	4;
	unsigned int nandTriggerTail	:	4;
	unsigned int nandTrigNTransHead	:	4;
	unsigned int nandTrigNTransTail	:	4;
	unsigned int nandTransferHead	:	4;
	unsigned int nandTransferTail	:	4;
	unsigned int nandEraseHead	:	4;
	unsigned int nandEraseTail	:	4;
	unsigned int nandStatusHead	:	4;
	unsigned int nandStatusTail	:	4;
};

struct wayPriorityArray {
	struct wayPriorityEntry wayPriorityEntry[CHANNEL_NUM];
};

int CheckReqQueueAvailability(unsigned int chNo, unsigned int wayNo, unsigned int openSlots);
void PushToReqQueue(P_LOW_LEVEL_REQ_INFO lowLevelCmd);
int PushToReqQueueNonBlocking(P_LOW_LEVEL_REQ_INFO lowLevelCmd, unsigned int openSlots);
int PopFromReqQueue(int chNo, int wayNo);
int CheckReqStatusAsync(int chNo, int wayNo);
int CheckReqErrorInfo(int chNo, int wayNo);

void PushToTransReqQueue(unsigned int entryIdx);
void PushToTransReadReqQueue(unsigned int entryIdx, unsigned int cmdSlotTag, unsigned int nlb);
int PopFromTransReqQueue();
int PopFromTransReadReqQueue();

void PushToSubReqQueue(int chNo, int wayNo, unsigned int request, unsigned int rowAddress, unsigned int pageDataBuf, unsigned int spareDataBuf);
int PopFromSubReqQueue(int chNo, int wayNo);
int CheckSubReqStatusAsync(int chNo, int wayNo);
int CheckSubReqErrorInfo(int chNo, int wayNo);

void ExeLowLevelReq(int firstQueue);
void EmptyReqQ();
void EmptySubReqQ();
void EmptyLowLevelQ(int firstQueue);

extern struct reqArray* reqQueue;
extern struct rqPointerArray* rqPointer;

extern struct transReqArray* transReqQueue;
extern struct transReadReqArray* transReadReqQueue;
extern struct transRqPointerEntry* transRqPointer;
extern struct transRqPointerEntry* transReadRqPointer;
extern struct transPageReqArray* transPageReqQueue;

extern struct subReqArray*  subReqQueue;
extern struct rqPointerArray* srqPointer;
extern struct completeArray* completeTable0;
extern struct errorInfoArray* errorInfoTable0;
extern struct completeArray* completeTable1;
extern struct errorInfoArray* errorInfoTable1;
extern struct dieStatusArray* dieStatusTable;
extern struct newBadBlockArray* newBadBlockTable;
extern struct retryLimitArray* retryLimitTable;

extern struct wayPriorityArray* wayPriorityTable;

extern unsigned int reservedReq;
extern unsigned int badBlockUpdate;

#endif /* Low_Level_Scheduler_H_ */
