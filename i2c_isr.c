#include "i2c_driver.h"

void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle, uint8_t to_address)
{
	if (i2c_handle->i2c->SR1 & 0x1)
	{
		// SB now send address, controller now waitng for a write in DR
		i2c_handle->i2c->DR = (to_address << 1);
	}
	if (i2c_handle->i2c->SR1 & 0x2)
	{
		// ADDR Address sended. clear flag y reading SR2
		(void)i2c_handle->i2c->SR2;
	}
}
