#include "i2c_driver.h"

static i2c_status_t i2c_tim14_setup(i2c_handle_t *i2c_handle)
{
	RCC->APB1ENR |= (0x1 << 8); // Feed clock

	TIM14->PSC = (APB1_TIM_CLK_HZ / TIMER_TICK_HZ) - 1; // divide down to TIMER_TICK_HZ
	TIM14->ARR = TIMEOUT_CLK_CNT;                       // timeout = TIMEOUT_CLK_CNT / TIMER_TICK_HZ seconds

	TIM14->CR1 |= (0x1 << 7); // ARPE
	TIM14->CR1 |= (0x1 << 2); // URS: interrupt request at counter overflow only
	TIM14->EGR |= 0x1;        // force an update to load PSC/ARR into shadow registers immediately
	TIM14->SR &= ~0x1;        // clear any spurious UIF set by the EGR update above
	TIM14->DIER |= 0x1;       // update interrupt enable

	NVIC->ISER[1] |= (0x1 << (45 - 32));

	return (I2C_OK);
}

static i2c_status_t i2c_DMA_setup(dma_handle_t *dma, I2C_TypeDef *i2c)
{
	/*
		NOTE:
		I2C is connected to DMA1 through AHB/APB1 bridge, correct stream and channel
		selection is a user task.
	*/
	DMA_Stream_TypeDef *rx_stream_dir;

	if (dma->rx_stream > 7 || dma->rx_channel > 7)
		return (I2C_ERROR);
	rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma->rx_stream));
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
	
	// Enable memory increment only for transfers greater than 1 data unit
	if (dma->rx_nb_transfers > 1)
		rx_stream_dir->CR |= (0x1 << 10);

	// Enable NVIC IRQ. Position for DMA1_Stream0 is 11 inside the vector table
	if (dma->rx_stream > 6)
		NVIC->ISER[1] |= (0x1 << 15);
	else
		NVIC->ISER[0] |= (0x1 << (11 + dma->rx_stream));
	
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

static i2c_status_t i2c_setup(I2C_TypeDef *i2c)
{
	if (i2c != I2C1 && i2c != I2C2 && i2c != I2C3)
		return (I2C_ERROR);
	if (APB1_TIM_CLK_HZ > MAX_APB1_CLK_HZ || APB1_TIM_CLK_HZ < MIN_APB1_CLK_HZ)
		return (I2C_ERROR);
	i2c->CR1 &= ~(0x1); // Disable peripheral
	i2c->CR1 |= (1 << 10); // Enable ACK
	i2c->CR2 &= ~(0x3F);
	i2c->CR2 |= (APB1_TIM_CLK_HZ / 1000000UL);
	i2c->CCR &= ~(0x0FFF);
	i2c->CCR |= (0x0FFF & (APB1_TIM_CLK_HZ / (2 * I2C_SCL_FREQ_HZ)));
	i2c->TRISE &= ~(0x3F);
	i2c->TRISE |= (0x3F & ((uint8_t)(MAX_RISE_SM * APB1_TIM_CLK_HZ) + 1));

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

i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	if (i2c_enable_clock(i2c_handle->i2c) != I2C_OK)
		return (I2C_UNAVAILABLE);
	if (i2c_GPIO_enable_clock(i2c_handle->sda_port, i2c_handle->scl_port) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->sda_port, i2c_handle->sda_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_GPIO_AF(i2c_handle->scl_port, i2c_handle->scl_pin) != I2C_OK)
		return (I2C_GPIO_CONFIG_ERROR);
	if (i2c_tim14_setup(i2c_handle) != I2C_OK)
		return (I2C_TIM_CONFIG_ERROR);
	if (i2c_DMA_setup(dma_handle, i2c_handle->i2c) != I2C_OK)
		return (I2C_DMA_CONFIG_ERROR);
	if (i2c_setup(i2c_handle->i2c) != I2C_OK)
		return (I2C_UNAVAILABLE);
	return (I2C_OK);
}

void i2c_start_init(i2c_handle_t *i2c_handle)
{
	i2c_handle->state = I2C_TX_SLAVE_ADDRESS;
	i2c_handle->err_flag = I2C_ERROR_CLEAR;
	TIM14->CR1 |= 0x1; // Enable TIMEOUT
	i2c_handle->i2c->CR1 |= (0x1 << 8);
}

void i2c_start(i2c_handle_t *i2c_handle)
{
	/*
		Start the transmission request
	*/
	i2c_handle->curr_retrys = 0;
	i2c_start_init(i2c_handle);
}

void i2c_stop_timer(void)
{
	TIM14->CR1 &= ~(0x1);				  // Stop timer
	TIM14->EGR |= 0x1;					  // Re-initialize the CNT.
	TIM14->SR &= ~0x1;
}

void i2c_stop(i2c_handle_t *i2c_handle)
{
	i2c_handle->i2c->CR1 |= (0x1 << 9);   // STOP
	i2c_handle->i2c->CR2 &= ~(0x1 << 11); // DMAEN = 0
	i2c_handle->i2c->CR2 &= ~(0x1 << 12); // LAST = 0
	i2c_handle->i2c->CR1 |= (0x1 << 10);  // ACK = 1
	i2c_handle->i2c->CR2 |= (0x1 << 10);  // ITBUFEN = 1
}
void i2c_mem_read(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Main public entry point.
	*/
	if (dma_handle->rx_buffer == 0 || dma_handle->rx_nb_transfers == 0)
		return ;

	// Refuse to start a new transaction while one is already in flight,
	// or bus is busy.
	if (i2c_handle->state != I2C_IDLE || (i2c_handle->i2c->SR2 & 0x2))
		return ;

	i2c_start(i2c_handle); // arms timer, sets state, issues START

	// transaction INITIATED, not complete — async, caller polls state
}

i2c_status_t i2c_bus_recovery(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
{
	/*
		Retake SCL port as GPIO to manually produce CLOCK_RECOVERY_CYCLES
		Then release the bus and arm I2C again
	*/
	i2c_status_t recovered;

	recovered = I2C_ERROR_BUS_UNRECOVERABLE;
	DMA_Stream_TypeDef *rx_stream_dir = (DMA_Stream_TypeDef *)(DMA1_Stream0_BASE + (0x18UL * dma_handle->rx_stream));
	rx_stream_dir->CR &= ~0x1; // disable stream — do this before reclaiming SCL/SDA
	i2c_handle->i2c->CR1 &= ~(0x1); // Disable I2C peripheral to take control

	i2c_handle->scl_port->MODER &= ~(0x3 << (i2c_handle->scl_pin * 2)); // General purpose output mode
	i2c_handle->scl_port->MODER |= (0x1 << (i2c_handle->scl_pin * 2)); // General purpose output mode
	i2c_handle->scl_port->OTYPER &= ~(0x1 << i2c_handle->scl_pin); // Output push-pull
	i2c_handle->scl_port->OSPEEDR &= ~(0x3 << (i2c_handle->scl_pin * 2)); // Fast speed
	i2c_handle->scl_port->OSPEEDR |= (0x2 << (i2c_handle->scl_pin * 2)); // Fast speed
	i2c_handle->scl_port->PUPDR &= ~(0x3 << (i2c_handle->scl_pin * 2)); // No pull-up, pull-down
	i2c_handle->scl_port->ODR |= (0x1 << i2c_handle->scl_pin); // Initial state HIGH

	// Claim sda port as Input so we can monitor if SDA comes back to high
	i2c_handle->sda_port->MODER &= ~(0x3 << (i2c_handle->sda_pin * 2)); // Input Mode
	i2c_handle->sda_port->OTYPER &= ~(0x1 << i2c_handle->sda_pin); // Push-pull
	i2c_handle->sda_port->PUPDR &= ~(0x3 << (i2c_handle->sda_pin * 2));
	i2c_handle->sda_port->PUPDR |= (0x1 << (i2c_handle->sda_pin * 2)); // Pull up

	/*
		Use TIM14 to toggle ODR value and generate a precise clock signal.
	*/
	TIM14->PSC = (uint16_t)(APB1_TIM_CLK_HZ / (2U * I2C_SCL_FREQ_HZ)) - 1; // Generate a 200 kHz pre-scaler
	TIM14->ARR = 0xFFFF;	// Clear any possible auto-reloads
	TIM14->EGR |= 0x1;	// force an update to load PSC/ARR into shadow registers immediately
	TIM14->SR &= ~0x1;	// clear any spurious UIF set by the EGR update above

	TIM14->DIER &= ~0x1;  // no interrupt — we're polling CNT directly
	TIM14->CR1 |= 0x1;   // free-running

	for (uint8_t i = 0; i < I2C_CLOCK_RECOVERY_CYCLES; i++)
	{
		i2c_handle->scl_port->ODR &= ~(1 << i2c_handle->scl_pin);
		TIM14->CNT = 0;
		while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);

		i2c_handle->scl_port->ODR |= (1 << i2c_handle->scl_pin);
		TIM14->CNT = 0;
		while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);

		if (i2c_handle->sda_port->IDR & (1 << i2c_handle->sda_pin))
			break; // SDA released early — no need to keep clocking
	}

	// If bus is recovered, issue a STOP sequence
	if (i2c_handle->sda_port->IDR & (1 << i2c_handle->sda_pin))
	{
		recovered = I2C_OK;
		i2c_handle->sda_port->MODER |= (0x1 << (i2c_handle->sda_pin * 2)); // SDA as output, briefly
		i2c_handle->sda_port->OTYPER &= ~(0x1 << i2c_handle->sda_pin); // Output push-pull
		i2c_handle->sda_port->PUPDR &= ~(0x3 << (i2c_handle->sda_pin * 2)); // No pull-up, pull-down
		i2c_handle->sda_port->ODR &= ~(1 << i2c_handle->sda_pin); // SDA low
		// SCL High already
		TIM14->CNT = 0;
		while (TIM14->CNT < I2C_HALF_PERIOD_RECOVERY_TICKS);
		i2c_handle->sda_port->ODR |= (1 << i2c_handle->sda_pin); // SDA high while SCL high = STOP
	}
	/*
		After timer is stopped, re-init SCL, SDA port, I2C peripheral and TIM14
	*/
	TIM14->CR1 &= ~(0x1);   // Stop timer
	if (i2c_GPIO_AF(i2c_handle->scl_port, i2c_handle->scl_pin) != I2C_OK)
		return (I2C_ERROR);

	if (i2c_GPIO_AF(i2c_handle->sda_port, i2c_handle->sda_pin) != I2C_OK)
		return (I2C_ERROR);

	if (i2c_tim14_setup(i2c_handle) != I2C_OK)
		return (I2C_ERROR);

	if (i2c_DMA_setup(dma_handle, i2c_handle->i2c) != I2C_OK)
		return (I2C_DMA_CONFIG_ERROR);

	if (i2c_setup(i2c_handle->i2c) != I2C_OK)
		return (I2C_ERROR);
	
	return (recovered);
}
