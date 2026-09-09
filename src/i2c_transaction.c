#include "i2c_driver.h"

void i2c_start_init(i2c_handle_t *i2c_handle)
{
	i2c_handle->state = I2C_TX_SLAVE_ADDRESS;
	TIM14->CR1 |= UINT32_C(1); // Enable TIMEOUT
	i2c_handle->i2c->CR1 |= (UINT32_C(1) << 8U); // issue START
}

void i2c_start(i2c_handle_t *i2c_handle)
{
	/*
		Start the transmission request
	*/
	i2c_handle->curr_retrys = 0;
	i2c_handle->err_flag = I2C_ERROR_CLEAR;
	i2c_start_init(i2c_handle);
}

void i2c_stop_timer(void)
{
	TIM14->CR1 &= ~UINT32_C(1);                // Stop timer
	TIM14->EGR |= UINT32_C(1);                   // Re-initialize the CNT.
	TIM14->SR &= ~UINT32_C(1);
}

void i2c_stop(i2c_handle_t *i2c_handle)
{
	i2c_handle->i2c->CR1 |= (UINT32_C(1) << 9U);   // STOP
	i2c_handle->i2c->CR2 &= ~(UINT32_C(1) << 11U); // DMAEN = 0
	i2c_handle->i2c->CR2 &= ~(UINT32_C(1) << 12U); // LAST = 0
	i2c_handle->i2c->CR1 |= (UINT32_C(1) << 10U);  // ACK = 1
	i2c_handle->i2c->CR2 |= (UINT32_C(1) << 10U);  // ITBUFEN = 1
}

void i2c_mem_read(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Main public entry point.
	*/
	if ((dma_handle->rx_buffer == NULL) || (dma_handle->rx_nb_transfers == UINT16_C(0)))
		return ;

	// Refuse to start a new transaction while one is already in flight,
	// or bus is busy.
	if ((i2c_handle->state != I2C_IDLE) || ((i2c_handle->i2c->SR2 & UINT32_C(0x2)) != UINT32_C(0)))
		return ;

	i2c_arm_rx_dma(i2c_handle, dma_handle);
	i2c_start(i2c_handle); // arms timer, sets state, issues START

	// transaction INITIATED, not complete — async, caller polls state
}

void i2c_arm_rx_dma(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	// Differentiate between 1 byte transfer and multi-byte transfer
	// Is the reception single-byte or multi-byte
	if (dma_handle->rx_nb_transfers < 2)
	{
		i2c_handle->i2c->CR1 &= ~(UINT32_C(1) << 10U); // ACK = 0
		i2c_handle->i2c->CR2 &= ~(UINT32_C(1) << 12U); // LAST = 0
		dma_handle->rx_stream->CR &= ~(UINT32_C(1) << 10U);      // No Memory increment
	}
	else
	{
		i2c_handle->i2c->CR1 |= (UINT32_C(1) << 10U);  // ACK = 1 (defensive)
		i2c_handle->i2c->CR2 |= (UINT32_C(1) << 12U);  // LAST = 1
		dma_handle->rx_stream->CR |= (UINT32_C(1) << 10U);       // Memory increment
	}

	dma_handle->rx_stream->NDTR = dma_handle->rx_nb_transfers;
	dma_handle->rx_stream->M0AR = (uint32_t)(uintptr_t)dma_handle->rx_buffer;
}
