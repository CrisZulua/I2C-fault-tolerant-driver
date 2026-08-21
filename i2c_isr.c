#include "i2c_driver.h"

void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle, uint8_t slave_adrs, uint8_t slave_reg)
{
	uint32_t sr1 = i2c_handle->i2c->SR1;

	switch (i2c_handle->next_step)
	{
		case I2C_TX_SLAVE_ADDRESS:
			// SB -> now send address, controller now waiting for a write in DR
			if (sr1 & 0x1)
			{
				i2c_handle->i2c->DR = (slave_adrs << 1) | 0;
				i2c_handle->next_step = I2C_TX_WRITE_REG;
			}
			break;
		case I2C_TX_WRITE_REG:
			// ADDR Address sended. clear flag and reading SR2. Write slave register
			if (sr1 & 0x2)
			{
				(void)i2c_handle->i2c->SR2;
				i2c_handle->i2c->DR = slave_reg;
				i2c_handle->next_step = I2C_RX_RSTART;
			}
			break;
		case I2C_RX_RSTART:
			/*
				TXE -> ACK pulse received
				The new start condition will clear TXE
			*/
			if (sr1 & 0x80)
			{
				i2c_handle->i2c->CR1 |= (0x1 << 8); // Start
				i2c_handle->next_step = I2C_RX_SLAVE_ADDRESS;
			}
		case I2C_RX_SLAVE_ADDRESS:
			// SB -> adress slave but this time to read
			if (sr1 & 0x1)
				i2c_handle->i2c->DR = (slave_adrs << 1) | 1;

			// ADDR -> Clear flag and arm RX DMA. In RX MODE there is no TXE.
			if (sr1 & 0x2)
			{
				i2c_handle->next_step = I2C_RX_WAITING;
				// If number of data transfers is 1, disable ACK and STOP at the DMA TCI
				i2c_arm_rx_dma(i2c_handle, dma_handle);
				(void)i2c_handle->i2c->SR2;
			}
		default:
			break;
	}

	
}
