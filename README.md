# I2C-fault-tolerant-driver

## Description
I2C peripheral agnostic driver specifically build for a STM32F446RE.

It allows for a I2C communication protocol between Master/Controller (STM32F446RE) and any I2C slave device. The driver is designed to be fault-tolerant, meaning it can handle errors and recover from them without crashing the system.

It includes a timeout feature for the current transaction, acting as the backbone for error detection and recovery. The driver is designed to request data from a 
slave device, and it will automatically handle any errors that may occur during the communication process.

The driver uses the CMSIS library.

> This is a work in progress and bugs may occur. Please report any issues you encounter.

## How to use
The I2C peripheral configuration uses a handle structure to store the 
configuration parameters, and the driver provides a set of functions to 
initialize, and use the I2C peripheral.

It supports the use of I2C1/2/3 peripherals as well as the use of DMA for data collection. The DMA in use is DMA1 storing its configuration in a handle structure.

The DMA stream and channel in use are configurable. Note that the correct assignment of the DMA stream and channel in combination with the I2C peripheral in use is a task that must be done by the user of the driver, as it is not handled by the driver itself.

The function ```i2c_init(i2c_handle, dam_handle)``` is used to initialize the I2C peripheral, DMA and timer for timeout feature. It will return an error code if the configuration is invalid.

The driver provides a public entry point to read data from a slave device, This function takes the I2C and DMA handle structures as input parameters (both should be updated with the correct buffer, number of transfers and state before calling this function). The function will return an error upon misconfiguration of the dma handle and if there is a communication happening at the moment.
```c
i2c_status_t i2c_mem_read(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle)
```

In order for the timeout to work properly, the user must modify the values of these macros in the i2c.h file:
```c
#define APB1_TIM_CLK_HZ   50000000UL  // TIM14 input clock
#define TIMER_TICK_HZ     10000UL     // 10 kHz -> 100 us per tick
#define TIMEOUT_CLK_CNT   300         // e.g. 300 * 100us = 30 ms timeout, tune to your bus
```

Example:
```C
i2c_handle_t i2c_handle = {
	.sda_port = GPIOB,
	.scl_port = GPIOB,
	.sda_pin = 7,
	.scl_pin = 6,
	.i2c = I2C1,
	.slave_address = 0x68,
	.slave_reg = 0x00,
	.state = I2C_IDLE,
	.max_retrys = 1
}

dma_handle_t dma_handle = {
	.rx_stream = 0,
	.rx_channel = 0,
	.rx_buffer = buffer,
	.rx_nb_transfers = 1,
}

if (i2c_init(&i2c_handle, &dma_handle) != I2C_OK)
{
	// Log I2C initialization error
}

i2c_mem_read(&i2c_handle, &dma_handle)
// Request initiated, completion at DMA TC interrupt
```

## Future work
1. Implement a bus recovery function that handles the case where the I2C bus is stuck due to a slave device holding the SDA line low. This function will attempt to reset the bus and recover from this error condition.

2. Test the driver with different I2C slave devices to ensure compatibility and robustness.
