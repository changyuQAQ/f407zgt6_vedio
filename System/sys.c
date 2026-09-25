#include "sys.h"

void NVIC_Configuration(void)
{

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);	
	
	NVIC_InitTypeDef NVIC_InitStruct;

	
	// SDIO Interrupt ENABLE
	NVIC_InitStruct.NVIC_IRQChannel = SDIO_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;//sd¶ÁÈ¡
	NVIC_Init(&NVIC_InitStruct);
	// DMA2 STREAMx Interrupt ENABLE
	NVIC_InitStruct.NVIC_IRQChannel = DMA2_Stream3_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_Init(&NVIC_InitStruct);//dma´«Êä
}
