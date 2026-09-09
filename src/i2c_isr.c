#include "i2c_driver.h"

void i2c_dma_rx_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	static const uint8_t dma_flag_base[4] = {UINT8_C(0), UINT8_C(6), UINT8_C(16), UINT8_C(22)};

	volatile uint32_t *dma_isr  = (volatile uint32_t *)(uintptr_t)(DMA1_BASE + (UINT32_C(0x4) * ((uint32_t)dma_handle->rx_stream_n / UINT32_C(4))));
	volatile uint32_t *dma_ifcr = (volatile uint32_t *)(uintptr_t)(DMA1_BASE + UINT32_C(0x8) + (UINT32_C(0x4) * ((uint32_t)dma_handle->rx_stream_n / UINT32_C(4))));

	uint8_t base = dma_flag_base[dma_handle->rx_stream_n % UINT8_C(4)];
	uint32_t teif_mask = (UINT32_C(1) << ((uint32_t)base + 3U));
	uint32_t tcif_mask = (UINT32_C(1) << ((uint32_t)base + 5U));

	dma_handle->rx_stream->CR &= ~UINT32_C(1);  // disable stream, TCI or TEI

	if (*dma_isr & teif_mask)
	{
		*dma_ifcr = (UINT32_C(0x3D) << (uint32_t)base); // clears FEIF|DMEIF|TEIF|HTIF|TCIF for this stream
		i2c_handle->err_flag = I2C_ERROR_DMA;
		return;
	}

	// Transfer complete, no errors — happy path.
	if (*dma_isr & tcif_mask)
	{
		*dma_ifcr = tcif_mask; // write 1 to clear TCIF

		// Check if the comm state is the last one. DMA can trigger even if BERR occurs
		if (i2c_handle->state == I2C_RX_ACTIVE)
		{
			TIM14->CR1 &= ~UINT32_C(1);  // Stop timer
			TIM14->EGR |= UINT32_C(1);  // Re-initialize the CNT.
			TIM14->SR &= ~UINT32_C(1);
			i2c_stop(i2c_handle);
			i2c_handle->state = I2C_IDLE; // IDLE signals a transaction completed and the bus is free
		}
	}
}

void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	uint32_t sr1 = i2c_handle->i2c->SR1;

	switch (i2c_handle->state)
	{
		case I2C_TX_SLAVE_ADDRESS:
			// SB -> controller now waiting for a write in DR, send slave address
			if ((sr1 & UINT32_C(1)) != UINT32_C(0))
			{
				i2c_handle->i2c->DR = (i2c_handle->slave_addr << 1) | 0; // Write
				i2c_handle->state = I2C_TX_WRITE_REG;
			}
			break;
		case I2C_TX_WRITE_REG:
			// ADDR Address sended. clear flag and reading SR2. Write slave register
			if ((sr1 & UINT32_C(0x2)) != UINT32_C(0))
				(void)i2c_handle->i2c->SR2;
			// If TXE = 1, DR and shift register empty -> write in DR clears TXE
			if ((sr1 & UINT32_C(0x80)) != UINT32_C(0))
			{
				i2c_handle->i2c->DR = i2c_handle->slave_reg;
				i2c_handle->state = I2C_RX_RSTART;
			}
			break;
		case I2C_RX_RSTART:
			/*
				BTF -> ACK pulse received
				The new start condition will clear BTF
			*/
			if ((sr1 & UINT32_C(0x4)) != UINT32_C(0))
			{
				i2c_handle->i2c->CR1 |= (0x1 << 8); // Start
				i2c_handle->state = I2C_RX_SLAVE_ADDRESS;
			}
			break;
		case I2C_RX_SLAVE_ADDRESS:
			// SB -> adress slave but this time to read
			if ((sr1 & UINT32_C(1)) != UINT32_C(0))
			{
				// Disable ITBUFEN as rm0390 suggest. No TxE or RxNE interrupts generated. DMA takes control.
				i2c_handle->i2c->CR2 &= ~(UINT32_C(1) << 10U);
				// Enable DMAEN before ADDR event. After sending the address no RxNE happens
				i2c_handle->i2c->CR2 |= (UINT32_C(1) << 11U);
				i2c_handle->i2c->DR = (i2c_handle->slave_addr << 1) | 1;
			}

			// ADDR -> Clear flag and arm RX DMA. In RX MODE there is no TXE.
			if ((sr1 & UINT32_C(0x2)) != UINT32_C(0))
			{
				i2c_handle->state = I2C_RX_ACTIVE;
				dma_handle->rx_stream->CR |= UINT32_C(1);
				(void)i2c_handle->i2c->SR2;
			}
			break;
		default:
			break;
	}
}

void i2c_er_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		I2C ERROR INTERRUPT
		AF (NACK), BERR, ARLO
		Do we check OVR just in case?
	*/
	dma_handle->rx_stream->CR &= ~UINT32_C(1); // disable stream
	// No need to wait for DMA disable as no configuration is being changeds

	if ((i2c_handle->i2c->SR1 & (UINT32_C(1) << 10U)) != UINT32_C(0))
	{
		/*
			AF - Adress problem, this only triggers at controller transmitter mode, thats the only configuration
			being used. Dont self resolve. STOP the transaction.
		*/
		i2c_handle->i2c->SR1 &= ~(UINT32_C(1) << 10U); // Clear flag
		i2c_handle->err_flag = I2C_ERROR_AF;
		i2c_stop(i2c_handle);
	}
	if ((i2c_handle->i2c->SR1 & (UINT32_C(1) << 9U)) != UINT32_C(0))
	{
		/*
			ARLO - This driver supports single controller/master transactions, for this reason
			an arbitration lost maybe a glitched or malfunctioning channel.
		*/
		i2c_handle->i2c->SR1 &= ~(UINT32_C(1) << 9U); // Clear flag
		i2c_handle->err_flag = I2C_ERROR_ARLO;
		i2c_stop(i2c_handle);
	}
	if ((i2c_handle->i2c->SR1 & (UINT32_C(1) << 8U)) != UINT32_C(0))
	{
		/*
			BERR - Protocol-level anomalie. Abort the current transaction as data send may be corrupted.
			Re-start the transaction for max_retrys.
		*/
		i2c_handle->i2c->SR1 &= ~(UINT32_C(1) << 8U);
		i2c_stop(i2c_handle);
		if (i2c_handle->curr_retrys < i2c_handle->max_retrys)
		{
			i2c_handle->curr_retrys++;
			i2c_start_init(i2c_handle);
		}
		else
			i2c_handle->err_flag = I2C_ERROR_BERR;
	}
}   	

void i2c_tim_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Each case is empty for now, but one can implement a log system
		and trigger a log write inside each case.
	*/
	TIM14->SR &= ~UINT32_C(1);
	i2c_stop_timer();
	i2c_handle->state = I2C_IDLE; // IDLE signals a transaction completed and the bus is free
	switch (i2c_handle->err_flag)
	{
		case I2C_ERROR_DMA:
			break;
		case I2C_ERROR_AF:
			break;
		case I2C_ERROR_ARLO:
			break;
		case I2C_ERROR_BERR:
			break;
		case I2C_ERROR_CLEAR:
			/*
				If this is the case, comms went wrong. Probably slave stuck holding SDA low.
				Enter bus recovery mode.
			*/
			if (i2c_bus_recovery(i2c_handle, dma_handle) != I2C_OK)
				i2c_handle->state = I2C_BUS_UNAVAILABLE;
			break;
		case I2C_ERROR_BUS_STUCK:
			break;
	}
}
