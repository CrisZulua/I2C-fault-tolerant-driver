#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H


#include "stm32f446xx.h"

/* SET BY I2C STANDARD */
#define MAX_RISE_SM 0.000001f // 1us

/* Define these to match your CubeMX clock config */
#define APB1_TIM_CLK_HZ 50000000U	// TIM14 input clock
#define TIMER_TICK_HZ 10000U		// 10 kHz -> 100 us per tick
#define TIMEOUT_CLK_CNT 50U			// e.g. 50 * 100us = 5 ms timeout, tune to your bus
#define I2C_SCL_FREQ_HZ 100000U		// 100 kHz Standard Mode

#define MAX_APB1_CLK_HZ 90000000U
#define MIN_APB1_CLK_HZ 2000000U

/* Bus Recovery Macros */
#define I2C_CLOCK_RECOVERY_CYCLES 10U		// Number of SCL cycles to recover bus (MODIFY IF NEEDED, MINIMUM 9)
#define I2C_HALF_PERIOD_RECOVERY_TICKS 1U	// Duty Cycle for bit-banding SCL (DO NOT MODIFY)

typedef enum{
	I2C_OK,
	I2C_ERROR,
	I2C_UNAVAILABLE,
	I2C_GPIO_CONFIG_ERROR,
	I2C_DMA_CONFIG_ERROR,
	I2C_TIM_CONFIG_ERROR,
	I2C_BUS_UNRECOVERABLE,
} i2c_status_t;

typedef enum{
	I2C_IDLE,
	I2C_TX_SLAVE_ADDRESS,
	I2C_TX_WRITE_REG,
	I2C_TX_WAIT_BTF,
	I2C_RX_RSTART,
	I2C_RX_SLAVE_ADDRESS,
	I2C_RX_ACTIVE,
	I2C_RECOVERY_FAILED,
} i2c_comm_state_t;

typedef enum{
	I2C_ERROR_CLEAR,
	I2C_ERROR_DMA,
	I2C_ERROR_AF,
	I2C_ERROR_ARLO,
	I2C_ERROR_BERR,
	I2C_ERROR_BUS_STUCK,
} i2c_err_flag_t;

typedef struct i2c_handle_s{
	GPIO_TypeDef *sda_port;
	GPIO_TypeDef *scl_port;
	uint16_t sda_pin;
	uint16_t scl_pin;
	I2C_TypeDef	*i2c;
	volatile uint8_t slave_addr;
	volatile uint8_t slave_reg;
	volatile i2c_comm_state_t state;
	volatile i2c_err_flag_t err_flag;
	uint8_t max_retrys;
	volatile uint8_t curr_retrys;
} i2c_handle_t;

typedef struct dma_handle_s
{
	uint8_t rx_stream;
	uint8_t rx_channel;
	volatile uint8_t *rx_buffer;
	volatile uint16_t rx_nb_transfers;
} dma_handle_t;

/* PRIVATE API ROUTINES */
void i2c_start_init(i2c_handle_t *i2c_handle);
void i2c_start(i2c_handle_t *i2c_handle);
void i2c_stop_timer(void);
void i2c_stop(i2c_handle_t *i2c_handle);
i2c_status_t i2c_bus_recovery(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_dma_rx_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_er_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_tim_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);

/* PUBLIC API ENTRYS */
i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_mem_read(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
void i2c_clear_recovery_failure(i2c_handle_t *i2c_handle);

#endif // I2C_DRIVER_H
