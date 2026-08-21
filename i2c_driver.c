#include "i2c_driver.h"

static i2c_status_t i2c_DMA_setup(dma_handle_t *dma, I2C_TypeDef *i2c)
{
	/*
		NOTE:
		I2C is connected to DMA1 through AHB/APB1 bridge, correct stream and channel
		selection is a user task.
	*/
	DMA_Stream_TypeDef *rx_stream_dir;
	DMA_Stream_TypeDef *tx_stream_dir;

	if (dma->rx_stream > 7 || dma->rx_channel > 7 || dma->tx_stream > 7 || dma->tx_channel > 7)
		return (I2C_ERROR);
	rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma->rx_stream));
	tx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma->tx_stream));
	RCC->AHB1ENR |= (0x1 << 21);

	// Stream Configuration Procedure
	// RX
	rx_stream_dir->CR &= ~0x1; // Disable stream and wait for it
	while(rx_stream_dir->CR & 0x1);
	rx_stream_dir->PAR = (uint32_t)&i2c->DR; // Peripheral address
	rx_stream_dir->M0AR = dma->rx_buffer; // Memory address
	rx_stream_dir->NDTR = dma->rx_nb_transfers; // number of transfers
	rx_stream_dir->CR &= ~(0x7 << 25); // CHSEL clear
	rx_stream_dir->CR |= (dma->rx_channel << 25); // CHSEL
	rx_stream_dir->CR &= ~(0x3 << 13); // Memory data size 8 bits
	rx_stream_dir->CR &= ~(0x3 << 11); // Peripheral data size 8 bits
	rx_stream_dir->CR &= ~(0x3 << 6); // Peripheral to memory
	rx_stream_dir->CR |= (0x14); // TCIE and TEIE
	
	// TX
	tx_stream_dir->CR &= ~0x1; // Disable stream and wait for it
	while(tx_stream_dir->CR & 0x1);
	tx_stream_dir->PAR = (uint32_t)&i2c->DR; // Peripheral address
	tx_stream_dir->M0AR = dma->tx_buffer; // Memory address
	tx_stream_dir->NDTR = dma->tx_nb_transfers; // number of transfers
	tx_stream_dir->CR &= ~(0x7 << 25); // CHSEL clear
	tx_stream_dir->CR |= (dma->tx_channel << 25); // CHSEL
	tx_stream_dir->CR &= ~(0x3 << 13); // Memory data size 8 bits
	tx_stream_dir->CR &= ~(0x3 << 11); // Peripheral data size 8 bits
	tx_stream_dir->CR &= ~(0x3 << 6); // Memory to Peripheral clear
	tx_stream_dir->CR |= (0x1 << 6); // Memory to Peripheral
	tx_stream_dir->CR |= (0x14); // TCIE and TEIE
	
	// Enable memory increment only for transfers greater than 1 data unit
	if (dma->rx_nb_transfers > 1)
		rx_stream_dir->CR |= (0x1 << 10);
	if (dma->tx_nb_transfers > 1)
		tx_stream_dir->CR |= (0x1 << 10);

	// Enable NVIC IRQ. Position for DMA1_Stream0 is 11 inside the vector table
	if (dma->rx_stream > 6)
		NVIC->ISER[1] |= (0x1 << 15);
	else
		NVIC->ISER[0] |= (0x1 << (11 + dma->rx_stream));
	
	if (dma->tx_stream > 6)
		NVIC->ISER[1] |= (0x1 << 15);
	else
		NVIC->ISER[0] |= (0x1 << (11 + dma->tx_stream));
	// Enabling of DMA tx and rx happens at start condition
	return (I2C_OK);
}

static i2c_status_t i2c_enable_clock(I2C_TypeDef *i2c)
{
	uint32_t clock_bit = 0;
	if (i2c == I2C1)
		clock_bit = 1 << 21; // I2C1
	else if (i2c == I2C2)
		clock_bit = 1 << 22; // I2C2
	else if (i2c == I2C3)
		clock_bit = 1 << 23; // I2C3
	else
		return (I2C_ERROR);
	
	RCC->APB1ENR |= clock_bit;
	return (I2C_OK);
}

static i2c_status_t i2c_GPIO_enable_clock(GPIO_TypeDef *sda_port, GPIO_TypeDef *scl_port)
{
	uint32_t clock_bit = 0;

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
		clock_bit = 1 << 2;
	else if (sda_port == GPIOB)
		clock_bit = 1 << 1;
	else
		return (I2C_ERROR);
	RCC->AHB1ENR |= clock_bit;

	//SCL Port
	if (scl_port == GPIOA)
		clock_bit = 1;
	else if (scl_port == GPIOB)
		clock_bit = 1 << 1;
	else
		return (I2C_ERROR);
	RCC->AHB1ENR |= clock_bit;

	return (I2C_OK);
}

static i2c_status_t i2c_GPIO_AF(GPIO_TypeDef *port, uint16_t pin)
{
	/*
		NOTE:
		The pin set-up function does not implement validation for the right
		port and pin number combination. It is up to the user to supply the
		correct combination.
	*/
	if (pin > 15)
		return (I2C_ERROR);
	// Alternate mode
	port->MODER &= ~(0x3 << (pin * 2));
	port->MODER |= (0x2 << (pin * 2));
	// Open Drain
	port->OTYPER |= (0x1 << pin);
	// Pull-up resistor
	port->PUPDR &= ~(0x3 << (pin * 2));
	port->PUPDR |= (0x1 << (pin * 2));
	// AF4
	port->AFR[pin / 8] &= ~(0xf << ((pin % 8) * 4));
	port->AFR[pin / 8] |= (0x4 << ((pin % 8) * 4));

	return (I2C_OK);
}

static i2c_status_t i2c_setup(I2C_TypeDef *i2c, uint8_t APB1_freq_MHz)
{
	uint32_t apb1_hz = 0;

	if (i2c != I2C1 && i2c != I2C2 && i2c != I2C3)
		return (I2C_ERROR);
	if (APB1_freq_MHz > 50 || APB1_freq_MHz < 2)
		return (I2C_ERROR);
	i2c->CR1 &= ~(0x1); // Disable peripheral
	i2c->CR1 |= (1 << 10); // Enable ACK
	i2c->CR2 &= ~(0x3F);
	apb1_hz = APB1_freq_MHz * 1000000UL;
	i2c->CR2 |= APB1_freq_MHz;
	i2c->CCR &= ~(0x0FFF);
	i2c->CCR |= (0x0FFF & (apb1_hz / (2 * F_SCL)));
	i2c->TRISE &= ~(0x3F);
	i2c->TRISE |= (0x3F & ((uint8_t)(MAX_RISE_SM * apb1_hz) + 1));

	i2c->CR2 |= (0x7 << 8); // Enable ITERR, ITEVT, ITBUF
	/*
		NVIC vector positions.
		I2C1: 31, 32
		I2C2: 33, 34
		I2C3: 72, 73
	*/
	if (i2c == I2C1)
	{
		NVIC->ISER[0] |= (0x1 << 31);
		NVIC->ISER[1] |= (0x1);
	}
	else if (i2c == I2C2)
		NVIC->ISER[1] |= (0x3 << 1);
	else
		NVIC->ISER[3] |= (0x3 << 10);

	i2c->CR1 |= 0x1; // Enable peripheral
	return (I2C_OK);
}

i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle,uint8_t APB1_freq_MHz)
{
	if (i2c_enable_clock(i2c_handle->i2c) != I2C_OK)
		return (I2C_UNAVAILABLE);
	if (i2c_GPIO_enable_clock(i2c_handle->sda_port, i2c_handle->scl_port) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->sda_port, i2c_handle->sda_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->scl_port, i2c_handle->scl_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_DMA_setup(dma_handle, i2c_handle->i2c) != I2C_OK)
		return (I2C_DMA_CONFIG_ERROR);
	if (i2c_setup(i2c_handle->i2c, APB1_freq_MHz) != I2C_OK)
		return (I2C_UNAVAILABLE);
	return (I2C_OK);
}

i2c_status_t i2c_start(i2c_handle_t *i2c_handle)
{
	/*
		Start the transmission request
	*/
	i2c_handle->next_step = I2C_TX_SLAVE_ADDRESS;
	i2c_handle->i2c->CR1 |= (0x1 << 8);
	return (I2C_OK);
}
