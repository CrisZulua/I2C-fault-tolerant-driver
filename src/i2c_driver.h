#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "stm32f446xx.h"

/* Set by the I2C standard: maximum rise time for Standard-mode operation. */
#define MAX_RISE_SM (1.0e-6F)

/*
 * Define these values to match the CubeMX clock configuration.
 *
 * On STM32F4, when the APB1 prescaler is not 1, the timer clock on that bus
 * is twice the peripheral clock. I2C1/2/3 and TIM14 therefore use distinct
 * clock values even though both clocks derive from APB1.
 */
#define APB1_TIM_CLK_HZ (UINT32_C(50000000)) /* TIM14 input clock. */
#define APB1_PERIPH_CLK_HZ (UINT32_C(25000000)) /* APB1 peripheral clock. */
#define TIMER_TICK_HZ (UINT32_C(100000)) /* Timer tick frequency: 10 kHz. */
/*
 * Timeout counter value. The effective timeout is approximately one timer
 * tick longer than this value due to timer update and interrupt latency.
 */
#define TIMEOUT_CLK_CNT (UINT32_C(50)) /* (50 + 1) * 10us = 510 us ~ Watchdog timeput value*/
#define I2C_SCL_FREQ_HZ (UINT32_C(100000)) /* Standard-mode SCL: 100 kHz. */

/* Number of SCL cycles used for recovery; the I2C specification requires at least 9. */
#define I2C_CLOCK_RECOVERY_CYCLES (UINT32_C(10))
/* Do not modify: one timer tick is used for each half-period of recovery. */
#define I2C_HALF_PERIOD_RECOVERY_TICKS (UINT32_C(1))

#define MAX_APB1_CLK_HZ (UINT32_C(45000000)) /* Maximum supported APB1 clock. */
#define MIN_APB1_CLK_HZ (UINT32_C(2000000)) /* Minimum supported APB1 clock. */


typedef enum
{
	I2C_OK = 0,
	I2C_ERROR,
	I2C_UNAVAILABLE,
	I2C_GPIO_CONFIG_ERROR,
	I2C_DMA_CONFIG_ERROR,
	I2C_TIM_CONFIG_ERROR
} i2c_status_t;

typedef enum
{
	I2C_IDLE = 0,
	I2C_TX_SLAVE_ADDRESS,
	I2C_TX_WRITE_REG,
	I2C_TX_WAIT_BTF,
	I2C_RX_RSTART,
	I2C_RX_SLAVE_ADDRESS,
	I2C_RX_ACTIVE,
	I2C_BUS_UNAVAILABLE
} i2c_comm_state_t;

typedef enum
{
	I2C_ERROR_CLEAR = 0,
	I2C_ERROR_DMA,
	I2C_ERROR_AF,
	I2C_ERROR_ARLO,
	I2C_ERROR_BERR,
	I2C_ERROR_BUS_STUCK
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
	DMA_Stream_TypeDef	*rx_stream;
	uint8_t 			rx_stream_n;
	uint8_t				rx_channel_n;
	volatile uint8_t	*rx_buffer;
	volatile uint16_t	rx_nb_transfers;
} dma_handle_t;

/* Internal API. */
extern i2c_status_t i2c_tim14_setup(void);
extern i2c_status_t i2c_enable_clock(I2C_TypeDef *i2c);
extern i2c_status_t i2c_DMA_setup(dma_handle_t *dma, I2C_TypeDef *i2c);
extern i2c_status_t i2c_GPIO_enable_clock(GPIO_TypeDef *sda_port, GPIO_TypeDef *scl_port);
extern i2c_status_t i2c_GPIO_AF(GPIO_TypeDef *port, uint16_t pin);
extern i2c_status_t i2c_setup(I2C_TypeDef *i2c);
extern void i2c_start_init(i2c_handle_t *i2c_handle);
extern void i2c_start(i2c_handle_t *i2c_handle);
extern void i2c_arm_rx_dma(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
extern void i2c_stop_timer(void);
extern void i2c_stop(i2c_handle_t *i2c_handle);
extern i2c_status_t i2c_bus_recovery(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);

/* Public API. */
extern i2c_status_t i2c_init(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
extern void i2c_mem_read(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
extern void i2c_clear_bus_unavailable(i2c_handle_t *i2c_handle);

/* Interrupt Handlers - Wrap this functions inside CMSIS IRQ_Handlers */
extern void i2c_ev_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
extern void i2c_dma_rx_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle, void (*callback)(uint8_t *, uint32_t));
extern void i2c_er_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);
extern void i2c_tim_irq_handler(i2c_handle_t *i2c_handle, dma_handle_t *dma_handle);

#endif /* I2C_DRIVER_H */
