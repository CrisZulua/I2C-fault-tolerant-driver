#include "i2c_driver.h"

static void i2c_bus_recovery_GPIO_pins_config(i2c_handle_t *i2c_handle)
{
	i2c_handle->scl_port->MODER &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->scl_pin * 2U)); // General purpose output mode
	i2c_handle->scl_port->MODER |= (UINT32_C(1) << ((uint32_t)i2c_handle->scl_pin * 2U)); // General purpose output mode
	i2c_handle->scl_port->OTYPER &= ~(UINT32_C(1) << (uint32_t)i2c_handle->scl_pin); // Output push-pull
	i2c_handle->scl_port->OSPEEDR &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->scl_pin * 2U)); // Fast speed
	i2c_handle->scl_port->OSPEEDR |= (UINT32_C(0x2) << ((uint32_t)i2c_handle->scl_pin * 2U)); // Fast speed
	i2c_handle->scl_port->PUPDR &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->scl_pin * 2U)); // No pull-up, pull-down
	i2c_handle->scl_port->ODR |= (UINT32_C(1) << (uint32_t)i2c_handle->scl_pin); // Initial state HIGH

	// Claim sda port as Input so we can monitor if SDA comes back to high
	i2c_handle->sda_port->MODER &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->sda_pin * 2U)); // Input Mode
	i2c_handle->sda_port->OTYPER &= ~(UINT32_C(1) << (uint32_t)i2c_handle->sda_pin); // Push-pull
	i2c_handle->sda_port->PUPDR &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->sda_pin * 2U));
	i2c_handle->sda_port->PUPDR |= (UINT32_C(1) << ((uint32_t)i2c_handle->sda_pin * 2U)); // Pull up
}

static void i2c_bus_recovery_TIM_config(void)
{
	/*
		Use TIM14 to toggle ODR value and generate a precise clock signal.
	*/
	TIM14->PSC = (uint16_t)((APB1_TIM_CLK_HZ / (UINT32_C(2) * I2C_SCL_FREQ_HZ)) - UINT32_C(1)); // Generate a 200 kHz pre-scaler
	TIM14->ARR = UINT32_C(0xFFFF);    // Clear any possible auto-reloads
	TIM14->EGR |= UINT32_C(1);      // force an update to load PSC/ARR into shadow registers immediately
	TIM14->SR &= ~UINT32_C(1);      // clear any spurious UIF set by the EGR update above

	TIM14->DIER &= ~UINT32_C(1);  // no interrupt — we're polling CNT directly
}

static i2c_status_t i2c_bus_recovery_procedure(i2c_handle_t *i2c_handle)
{
	i2c_status_t recovered;

	recovered = I2C_ERROR;
	i2c_handle->err_flag = I2C_ERROR_BUS_STUCK;
	// Try the manual SCL toggle at least 2 times before giving up.
	for (uint8_t attempt = UINT8_C(0); attempt < UINT8_C(2); attempt++)
	{
		for (uint32_t cycle = UINT32_C(0); cycle < I2C_CLOCK_RECOVERY_CYCLES; cycle++)
		{
			i2c_handle->scl_port->ODR &= ~(UINT32_C(1) << (uint32_t)i2c_handle->scl_pin);
			TIM14->CNT = UINT32_C(0);
			while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);

			i2c_handle->scl_port->ODR |= (UINT32_C(1) << (uint32_t)i2c_handle->scl_pin);
			TIM14->CNT = UINT32_C(0);
			while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);

			if ((i2c_handle->sda_port->IDR & (UINT32_C(1) << (uint32_t)i2c_handle->sda_pin)) != UINT32_C(0))
				break; // SDA released early — no need to keep clocking
		}
		if ((i2c_handle->sda_port->IDR & (UINT32_C(1) << (uint32_t)i2c_handle->sda_pin)) != UINT32_C(0))
			break; // SDA released early — no need to keep clocking
	}

	// If bus is recovered, issue a STOP sequence
	if ((i2c_handle->sda_port->IDR & (UINT32_C(1) << (uint32_t)i2c_handle->sda_pin)) != UINT32_C(0))
	{
		recovered = I2C_OK;
		i2c_handle->err_flag = I2C_ERROR_CLEAR;
		i2c_handle->sda_port->MODER |= (UINT32_C(1) << ((uint32_t)i2c_handle->sda_pin * 2U)); // SDA as output, briefly
		i2c_handle->sda_port->OTYPER &= ~(UINT32_C(1) << (uint32_t)i2c_handle->sda_pin); // Output push-pull
		i2c_handle->sda_port->PUPDR &= ~(UINT32_C(0x3) << ((uint32_t)i2c_handle->sda_pin * 2U)); // No pull-up, pull-down
		i2c_handle->sda_port->ODR &= ~(UINT32_C(1) << (uint32_t)i2c_handle->sda_pin); // SDA low
		// SCL High already
		TIM14->CNT = UINT32_C(0);
		while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);
		i2c_handle->sda_port->ODR |= (UINT32_C(1) << (uint32_t)i2c_handle->sda_pin); // SDA high while SCL high = STOP
	}

	return (recovered);
}

static i2c_status_t i2c_bus_recovery_peripheral_config(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		After success or failure on bus recovery, try to re-config the peripherals.
	*/
	if (i2c_GPIO_AF(i2c_handle->scl_port, i2c_handle->scl_pin) != I2C_OK)
		return (I2C_ERROR);

	if (i2c_GPIO_AF(i2c_handle->sda_port, i2c_handle->sda_pin) != I2C_OK)
		return (I2C_ERROR);

	if (i2c_tim14_setup() != I2C_OK)
		return (I2C_ERROR);

	if (i2c_DMA_setup(dma_handle, i2c_handle->i2c) != I2C_OK)
		return (I2C_DMA_CONFIG_ERROR);

	if (i2c_setup(i2c_handle->i2c) != I2C_OK)
		return (I2C_ERROR);
	return (I2C_OK);
}

i2c_status_t i2c_bus_recovery(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Retake SCL port as GPIO to manually produce CLOCK_RECOVERY_CYCLES
		Then release the bus and arm I2C again
	*/
	i2c_status_t recovered;
	i2c_status_t periph_config;

	dma_handle->rx_stream->CR &= ~0x1; // disable stream — do this before reclaiming SCL/SDA
	i2c_handle->i2c->CR1 &= ~(0x1); // Disable I2C peripheral to take control

	i2c_bus_recovery_GPIO_pins_config(i2c_handle);

	i2c_bus_recovery_TIM_config();
	TIM14->CR1 |= UINT32_C(1);   // free-running

	recovered = i2c_bus_recovery_procedure(i2c_handle);
	/*
		After timer is stopped, re-init SCL, SDA port, I2C peripheral and TIM14
	*/
	TIM14->CR1 &= ~UINT32_C(1);   // Stop timer
	periph_config = i2c_bus_recovery_peripheral_config(i2c_handle, dma_handle);
	// state will be handle by the outcome of this routine. I2C_IDLE or I2C_BUS_UNAVAILABLE
	// err_flag will keep the last error trigger or CLEAR if bus was recovered

	return ((recovered == I2C_OK && periph_config == I2C_OK) ? I2C_OK : I2C_ERROR);
}
