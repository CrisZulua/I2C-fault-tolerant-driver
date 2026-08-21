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

typedef enum{
	I2C_TX_SLAVE_ADDRESS,
	I2C_TX_WRITE_REG,
	I2C_TX_WAIT_BTF,
	I2C_RX_RSTART,
	I2C_RX_SLAVE_ADDRESS,
	I2C_RX_ACTIVE,
	I2C_COM_SUCCEDED,
	I2C_COM_FAILED,
} i2c_comm_state_t;

typedef enum{
	I2C_ERROR_CLEAR,
	I2C_ERROR_DMA,
	I2C_ERROR_AF,
	I2C_ERROR_ARLO,
	I2C_ERROR_BERR,
} i2c_err_flag_t;

typedef struct i2c_handle_s{
	GPIO_TypeDef *sda_port;
	GPIO_TypeDef *scl_port;
	uint16_t sda_pin;
	uint16_t scl_pin;
	I2C_TypeDef	*i2c;
	uint8_t slave_addr;
	uint8_t slave_reg;
	i2c_comm_state_t next_step;
	i2c_err_flag_t err_flag;
	uint8_t max_retrys;
	uint8_t curr_retrys;
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
void i2c_start(i2c_handle_t *i2c_handle);
void i2c_stop(i2c_handle_t *i2c_handle);
void i2c_bus_recovery(i2c_handle_t *i2c_handle);

void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_dma_rx_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_er_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);   // AF (NACK), BERR, ARLO, timeout-related
#endif // I2C_DRIVER_H
