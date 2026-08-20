#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H


#include "stm32f446xx.h"
#define F_SCL 100000 // 100 kHz Standard Mode
#define MAX_RISE_SM 0.000001f // 1us

typedef enum{
	I2C_OK,
	I2C_ERROR,
	I2C_UNAVAILABLE,
	I2C_GPIO_CONFIG_ERROR,
	I2C_DMA_CONFIG_ERROR,
} i2c_status_t;

typedef struct i2c_handle_s{
	GPIO_TypeDef *sda_port;
	GPIO_TypeDef *scl_port;
	uint16_t sda_pin;
	uint16_t scl_pin;
	I2C_TypeDef	*i2c;
} i2c_handle_t;

typedef struct dma_handle_s
{
	uint8_t rx_stream;
	uint8_t rx_channel;
	uint8_t tx_stream;
	uint8_t tx_channel;
	uint8_t *rx_buffer;
	uint8_t *tx_buffer;
	uint16_t rx_nb_transfers;
	uint16_t tx_nb_transfers;
} dma_handle_t;

i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle,uint8_t APB1_freq_MHz);
void i2c_ev_irq_handler(i2c_handle_t *hi2c);   // SB, ADDR, BTF, etc.
void i2c_er_irq_handler(i2c_handle_t *hi2c);   // AF (NACK), BERR, ARLO, timeout-related
void i2c_dma_tx_irq_handler(i2c_handle_t *hi2c); // TX stream TC/TE
void i2c_dma_rx_irq_handler(i2c_handle_t *hi2c); // RX stream TC/TE

#endif // I2C_DRIVER_H
