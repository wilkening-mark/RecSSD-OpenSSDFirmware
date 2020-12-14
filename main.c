//////////////////////////////////////////////////////////////////////////////////
// main.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Kibin Park <kbpark@enc.hanyang.ac.kr>
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
//			 Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// Project Name: Cosmos OpenSSD
// Design Name: Cosmos Firmware
// Module Name: Main
// File Name: main.c
//
// Version: v1.0.0
//
// Description:
//   - initializes caches, MMU, exception handler
//   - calls nvme_main function
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////



#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_mmu.h"
#include "xparameters_ps.h"
#include "xscugic_hw.h"
#include "xscugic.h"
#include "xil_printf.h"
#include "nvme/debug.h"

#include "nvme/nvme.h"
#include "nvme/nvme_main.h"
#include "nvme/host_lld.h"


XScuGic GicInstance;

int main()
{
	unsigned int u;

	XScuGic_Config *IntcConfig;

	Xil_ICacheDisable();
	Xil_DCacheDisable();
	Xil_DisableMMU();

	// Paging table set
	#define MB (1024*1024)
	for (u = 0; u < 4096; u++)
	{
		// Stack is allocated for 14MB at 1MB
		// No Heap usage -- 1KB allocation
		// Doesn't fit with code in A9 RAM
		// 16MB starts the ADMIN_CMD_BUFFER space
		// 256MB starts the IO_BUFFER space
		// 422MB starts the FTL data structure space
		// 900MB ends the available memory space
		if (u < 16)
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered
		else if (u < 422)
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & nonbuffered
		else if (u < 900)
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered
		else
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & nonbuffered

	}

	Xil_EnableMMU();
	Xil_ICacheEnable();
	Xil_DCacheEnable();
	xil_printf("[!] MMU has been enabled.\r\n");


	xil_printf("\r\n Hello COSMOS OpenSSD !!! \r\n");


	Xil_ExceptionInit();

	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	XScuGic_CfgInitialize(&GicInstance, IntcConfig, IntcConfig->CpuBaseAddress);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
								(Xil_ExceptionHandler)XScuGic_InterruptHandler,
								&GicInstance);

	XScuGic_Connect(&GicInstance, XPAR_FABRIC_NVMEHOSTCONTROLLER_0_DEV_IRQ_ASSERT_INTR,
					(Xil_ExceptionHandler)dev_irq_handler,
					(void *)0);

	XScuGic_Enable(&GicInstance, XPAR_FABRIC_NVMEHOSTCONTROLLER_0_DEV_IRQ_ASSERT_INTR);

	// Enable interrupts in the Processor.
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	Xil_ExceptionEnable();

	dev_irq_init();

	nvme_main();

	xil_printf("done\r\n");

	return 0;
}
