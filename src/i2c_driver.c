#include "i2c_driver.h"

i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	if (i2c_handle == NULL || dma_handle == NULL)
		return (I2C_ERROR);
	if (i2c_enable_clock(i2c_handle->i2c) != I2C_OK)
		return (I2C_UNAVAILABLE);
	if (i2c_GPIO_enable_clock(i2c_handle->sda_port, i2c_handle->scl_port) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->sda_port, i2c_handle->sda_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->scl_port, i2c_handle->scl_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_tim14_setup() != I2C_OK)
		return (I2C_TIM_CONFIG_ERROR);
	if (i2c_DMA_setup(dma_handle, i2c_handle->i2c) != I2C_OK)
		return (I2C_DMA_CONFIG_ERROR);
	if (i2c_setup(i2c_handle->i2c) != I2C_OK)
		return (I2C_UNAVAILABLE);
	return (I2C_OK);
}

void i2c_clear_bus_unavailable(i2c_handle_t *i2c_handle)
{
	/*
		Call this only after the bus has been physically recovered outside
		this driver's own bus recovery routine (e.g. manual intervention,
		slave power-cycled).

		IMPORTANT: this function ONLY clears the I2C_BUS_UNAVAILABLE state.
		It does NOT reconfigure any peripheral. The caller MUST call
		i2c_init() again after this before issuing i2c_mem_read(), since
		I2C_BUS_UNAVAILABLE can be caused either by an unrecoverable bus
		(SDA never released) or by a peripheral re-configuration failure
		inside i2c_bus_recovery() — in the latter case, I2C/DMA/TIM14
		registers may be left in a partial/unknown state.
	*/
	if ((i2c_handle != NULL) && (i2c_handle->state == I2C_BUS_UNAVAILABLE))
		i2c_handle->state = I2C_IDLE;
}
