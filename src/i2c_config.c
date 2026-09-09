#include "i2c_driver.h"

i2c_status_t i2c_tim14_setup(void)
{
	RCC->APB1ENR |= (UINT32_C(1) << 8U); // Feed clock

	TIM14->PSC = (uint16_t)((APB1_TIM_CLK_HZ / TIMER_TICK_HZ) - UINT32_C(1)); // divide down to TIMER_TICK_HZ
	TIM14->ARR = TIMEOUT_CLK_CNT;                       // timeout = TIMEOUT_CLK_CNT / TIMER_TICK_HZ seconds

	TIM14->CR1 |= (UINT32_C(1) << 7U); // ARPE
	TIM14->CR1 |= (UINT32_C(1) << 2U); // URS: interrupt request at counter overflow only
	TIM14->EGR |= UINT32_C(1);        // force an update to load PSC/ARR into shadow registers immediately
	TIM14->SR &= ~UINT32_C(1);        // clear any spurious UIF set by the EGR update above
	TIM14->DIER |= UINT32_C(1);       // update interrupt enable

	NVIC->ISER[1] |= (UINT32_C(1) << 13U);

	return (I2C_OK);
}

i2c_status_t i2c_DMA_setup(dma_handle_t *dma, I2C_TypeDef *i2c)
{
	/*
		NOTE:
		I2C is connected to DMA1 through AHB/APB1 bridge, correct stream and channel
		selection is a user task.
	*/
	if ((dma->rx_stream_n > UINT8_C(7)) || (dma->rx_channel_n > UINT8_C(7)))
		return (I2C_ERROR);
	RCC->AHB1ENR |= (0x1 << 21);

	// RX - Stream Configuration Procedure
	dma->rx_stream->CR &= ~UINT32_C(1); // Disable stream
	while ((dma->rx_stream->CR & UINT32_C(1)) != UINT32_C(0))
		; // Wait for the hardware to finish disabling the stream
	dma->rx_stream->PAR = (uint32_t)(uintptr_t)&i2c->DR; // Peripheral address
	dma->rx_stream->M0AR = (uint32_t)(uintptr_t)dma->rx_buffer; // Memory address
	dma->rx_stream->NDTR = dma->rx_nb_transfers; // number of transfers
	dma->rx_stream->CR &= ~(UINT32_C(0x7) << 25U); // CHSEL clear
	dma->rx_stream->CR |= ((uint32_t)dma->rx_channel_n << 25U); // CHSEL
	dma->rx_stream->CR &= ~(UINT32_C(0x3) << 13U); // Memory data size 8 bits
	dma->rx_stream->CR &= ~(UINT32_C(0x3) << 11U); // Peripheral data size 8 bits
	dma->rx_stream->CR &= ~(UINT32_C(0x3) << 6U); // Peripheral to memory
	dma->rx_stream->CR |= UINT32_C(0x14); // TCIE and TEIE

	// Memory increment is set at i2c_arm_rx_dma()

	// Enable NVIC IRQ. Position for DMA1_Stream0 is 11 inside the vector table
	if (dma->rx_stream_n > UINT8_C(6))
		NVIC->ISER[1] |= (UINT32_C(1) << 16U);
	else
		NVIC->ISER[0] |= (UINT32_C(1) << (11U + (uint32_t)dma->rx_stream_n));

	// Enabling of DMA rx happens at start condition
	return (I2C_OK);
}

i2c_status_t i2c_enable_clock(I2C_TypeDef *i2c)
{
	uint32_t clock_bit = UINT32_C(0);
	if (i2c == I2C1)
		clock_bit = UINT32_C(1) << 21U; // I2C1
	else if (i2c == I2C2)
		clock_bit = UINT32_C(1) << 22U; // I2C2
	else if (i2c == I2C3)
		clock_bit = UINT32_C(1) << 23U; // I2C3
	else
		return (I2C_ERROR);
	
	RCC->APB1ENR |= clock_bit;
	return (I2C_OK);
}

i2c_status_t i2c_GPIO_enable_clock(GPIO_TypeDef *sda_port, GPIO_TypeDef *scl_port)
{
	uint32_t clock_bit = UINT32_C(0);

	/*
		NOTE:
		As this driver is made specifically for the STM32F446RE it only accepts ports A
		to C. Covering evry valid I2C1/2/3 SDA and SCL pin combination.

		SDA port: B, C.
		SCL port: A, B.

		As derived from the Alternate Functions Table on the Reference Manual 0390.
	*/
	// SDA Port
	if (sda_port == GPIOC)
		clock_bit = UINT32_C(1) << 2U;
	else if (sda_port == GPIOB)
		clock_bit = UINT32_C(1) << 1U;
	else
		return (I2C_ERROR);
	RCC->AHB1ENR |= clock_bit;

	//SCL Port
	if (scl_port == GPIOA)
		clock_bit = UINT32_C(1);
	else if (scl_port == GPIOB)
		clock_bit = UINT32_C(1) << 1U;
	else
		return (I2C_ERROR);
	RCC->AHB1ENR |= clock_bit;

	return (I2C_OK);
}

i2c_status_t i2c_GPIO_AF(GPIO_TypeDef *port, uint16_t pin)
{
	/*
		NOTE:
		The pin set-up function does not implement validation for the right
		port and pin number combination. It is up to the user to supply the
		correct combination.
	*/
	if (pin > UINT16_C(15))
		return (I2C_ERROR);
	// Alternate mode
	port->MODER &= ~(UINT32_C(0x3) << ((uint32_t)pin * 2U));
	port->MODER |= (UINT32_C(0x2) << ((uint32_t)pin * 2U));
	// Open Drain
	port->OTYPER |= (UINT32_C(1) << (uint32_t)pin);
	// No pull-up, pull-down -- EXTERNAL PULL-UP RESISTOR NEEDED
	port->PUPDR &= ~(UINT32_C(0x3) << ((uint32_t)pin * 2U));
	// Fast Speed
	port->OSPEEDR &= ~(UINT32_C(0x3) << ((uint32_t)pin * 2U));
	port->OSPEEDR |= (UINT32_C(0x2) << ((uint32_t)pin * 2U));
	// AF4
	port->AFR[pin / 8U] &= ~(UINT32_C(0xf) << (((uint32_t)pin % 8U) * 4U));
	port->AFR[pin / 8U] |= (UINT32_C(0x4) << (((uint32_t)pin % 8U) * 4U));

	return (I2C_OK);
}

i2c_status_t i2c_setup(I2C_TypeDef *i2c)
{
	if ((i2c != I2C1) && (i2c != I2C2) && (i2c != I2C3))
		return (I2C_ERROR);
	if (APB1_PERIPH_CLK_HZ > MAX_APB1_CLK_HZ || APB1_PERIPH_CLK_HZ < MIN_APB1_CLK_HZ)
		return (I2C_ERROR);

	uint32_t reset_bit = UINT32_C(0);
	if (i2c == I2C1)
		reset_bit = UINT32_C(1) << 21U; // I2C1
	else if (i2c == I2C2)
		reset_bit = UINT32_C(1) << 22U; // I2C2
	else if (i2c == I2C3)
		reset_bit = UINT32_C(1) << 23U; // I2C3
	
	RCC->APB1RSTR |= reset_bit; // Safe reset. SR2 BUSY bit set to 0
	RCC->APB1RSTR &= ~reset_bit;
	i2c->CR1 &= ~(0x1); // Disable peripheral
	i2c->CR2 &= ~(0x1 << 11); // DMAEN=0
	i2c->CR2 &= ~(0x1 << 12); // LAST=0
	i2c->CR1 |= (UINT32_C(1) << 10U); // Enable ACK
	i2c->CR2 &= ~(0x3F);
	i2c->CR2 |= ((APB1_PERIPH_CLK_HZ / 1000000UL) & 0x3F);
	i2c->CCR &= ~(0x0FFF);
	i2c->CCR |= (0x0FFF & (APB1_PERIPH_CLK_HZ / (2 * I2C_SCL_FREQ_HZ)));
	i2c->TRISE &= ~(0x3F);
	i2c->TRISE |= (0x3F & ((uint8_t)(MAX_RISE_SM * APB1_PERIPH_CLK_HZ) + 1));

	i2c->CR2 |= (UINT32_C(0x7) << 8U); // Enable ITERR, ITEVT, ITBUF
	/*
		NVIC vector positions.
		I2C1: 31, 32
		I2C2: 33, 34
		I2C3: 72, 73
	*/
	if (i2c == I2C1)
	{
		NVIC->ISER[0] |= (UINT32_C(1) << 31U);
		NVIC->ISER[1] |= UINT32_C(1);
	}
	else if (i2c == I2C2)
		NVIC->ISER[1] |= (UINT32_C(0x3) << 1U);
	else
		NVIC->ISER[3] |= (UINT32_C(0x3) << 10U);

	i2c->CR1 |= UINT32_C(1); // Enable peripheral
	return (I2C_OK);
}
