#include "i2c_driver.h"

static void i2c_arm_rx_dma(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	// Differentiate between 1 byte transfer and multi-byte transfer
	DMA_Stream_TypeDef *rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma_handle->rx_stream));

	// Is the reception single-byte or multi-byte
	if (dma_handle->rx_nb_transfers < 2)
	{
		i2c_handle->i2c->CR1 &= ~(0x1 << 10); // ACK = 0
		i2c_handle->i2c->CR2 &= ~(0x1 << 12); // LAST = 0
		rx_stream_dir->CR &= ~(0x1 << 10);	  // No Memory increment
	}
	else
	{
		i2c_handle->i2c->CR1 |= (0x1 << 10);  // ACK = 1 (defensive)
		i2c_handle->i2c->CR2 |= (0x1 << 12);  // LAST = 1
		rx_stream_dir->CR |= (0x1 << 10);	  // Memory increment
	}
	
	// Disable ITBUFEN as rm0390 suggest. No TxE or RxNE interrupts generated. DMA takes control.
	i2c_handle->i2c->CR2 &= ~(0x1 << 10);
	rx_stream_dir->NDTR = dma_handle->rx_nb_transfers;
	rx_stream_dir->M0AR = (uint32_t)dma_handle->rx_buffer;
	rx_stream_dir->CR |= 0x1; // Enable DMA
}

void i2c_dma_rx_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	const uint8_t dma_flag_base[4] = {0, 6, 16, 22};

	DMA_Stream_TypeDef *rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma_handle->rx_stream));
	volatile uint32_t *dma_isr  = (volatile uint32_t *)(DMA1_BASE + 0x4 * (dma_handle->rx_stream / 4));
	volatile uint32_t *dma_ifcr = (volatile uint32_t *)(DMA1_BASE + 0x8 + 0x4 * (dma_handle->rx_stream / 4));

	uint8_t base = dma_flag_base[dma_handle->rx_stream % 4];
	uint32_t teif_mask = (0x1 << (base + 3));
	uint32_t tcif_mask = (0x1 << (base + 5));

	rx_stream_dir->CR &= ~0x1;  // disable stream, TCI or TEI

	if (*dma_isr & teif_mask)
	{
		*dma_ifcr = (0x3D << base); // clears FEIF|DMEIF|TEIF|HTIF|TCIF for this stream
		i2c_handle->err_flag = I2C_ERROR_DMA;
		return;
	}

	// Transfer complete, no errors — happy path.
	if (*dma_isr & tcif_mask)
	{
		*dma_ifcr = tcif_mask; // write 1 to clear TCIF

		TIM14->CR1 &= ~(0x1);  // Stop timer
		TIM14->EGR |= 0x1;  // Re-initialize the CNT.
		TIM14->SR &= ~0x1;
		i2c_handle->state = I2C_IDLE; // IDLE signals a transaction completed and the bus is free
		i2c_stop(i2c_handle);
	}
}

void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	uint32_t sr1 = i2c_handle->i2c->SR1;

	switch (i2c_handle->state)
	{
		case I2C_TX_SLAVE_ADDRESS:
			// SB -> controller now waiting for a write in DR, send slave address
			if (sr1 & 0x1)
			{
				i2c_handle->i2c->DR = (i2c_handle->slave_addr << 1) | 0; // Write
				i2c_handle->state = I2C_TX_WRITE_REG;
			}
			break;
		case I2C_TX_WRITE_REG:
			// ADDR Address sended. clear flag and reading SR2. Write slave register
			if (sr1 & 0x2)
				(void)i2c_handle->i2c->SR2;
			// If TXE = 1, DR and shift register empty -> write in DR clears TXE
			if (sr1 & 0x80)
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
			if (sr1 & 0x4)
			{
				i2c_handle->i2c->CR1 |= (0x1 << 8); // Start
				i2c_handle->state = I2C_RX_SLAVE_ADDRESS;
			}
			break;
		case I2C_RX_SLAVE_ADDRESS:
			// SB -> adress slave but this time to read
			if (sr1 & 0x1)
			{
				// Enable DMAEN before ADDR event. After sending the address no RxNE happens
				i2c_handle->i2c->CR2 |= (0x1 << 11);
				i2c_handle->i2c->DR = (i2c_handle->slave_addr << 1) | 1;
			}

			// ADDR -> Clear flag and arm RX DMA. In RX MODE there is no TXE.
			if (sr1 & 0x2)
			{
				i2c_handle->state = I2C_RX_ACTIVE;
				// If number of data transfers is 1, disable ACK and STOP at the DMA TCI
				i2c_arm_rx_dma(i2c_handle, dma_handle);
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
	DMA_Stream_TypeDef *rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma_handle->rx_stream));
	rx_stream_dir->CR &= ~0x1; // disable stream
	// No need to wait for DMA disable as no configuration is being changeds

	if (i2c_handle->i2c->SR1 & (0x1 << 10))
	{
		/*
			AF - Adress problem, this only triggers at controller transmitter mode, thats the only configuration
			being used. Dont self resolve. STOP the transaction.
		*/
		i2c_handle->i2c->SR1 &= ~(0x1 << 10); // Clear flag
		i2c_handle->err_flag = I2C_ERROR_AF;
		i2c_stop(i2c_handle);
	}
	if (i2c_handle->i2c->SR1 & (0x1 << 9))
	{
		/*
			ARLO - This driver supports single controller/master transactions, for this reason
			an arbitration lost maybe a glitched or malfunctioning channel.
		*/
		i2c_handle->i2c->SR1 &= ~(0x1 << 9); // Clear flag
		i2c_handle->err_flag = I2C_ERROR_ARLO;
		i2c_stop(i2c_handle);
	}
	if (i2c_handle->i2c->SR1 & (0x1 << 8))
	{
		/*
			BERR - Protocol-level anomalie. Abort the current transaction as data send may be corrupted.
			Re-start the transaction for max_retrys.
		*/
		i2c_handle->i2c->SR1 &= ~(0x1 << 8);
		if (i2c_handle->curr_retrys < i2c_handle->max_retrys)
		{
			i2c_handle->curr_retrys++;
			i2c_start_init(i2c_handle);
		}
		else
		{
			i2c_handle->err_flag = I2C_ERROR_BERR;
			i2c_stop(i2c_handle);
		}
	}
}   	

void i2c_tim_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Each case is empty for now, but one can implement a log system
		and trigger a log write inside each case.
	*/
	TIM14->SR &= ~(0x1);
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
