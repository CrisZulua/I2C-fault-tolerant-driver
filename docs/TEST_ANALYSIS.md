# Test Results — I2C Driver (STM32F446RE + BME280)

## Reference Configuration

* **Target:** STM32F446RE, I2C1, BME280 (1-byte read from register `0xD0`, ID).
* **Measurement clock (`DWT->CYCCNT`):** 50 MHz (CPU clock).
* **APB1 peripheral clock (I2C1):** 25 MHz.
* **Theoretical pure bus reference at actual 100 kHz:** ~390 µs per transaction (register write + repeated START + 1-byte read).

---

## Test 1 — Actual Duration of a Single Transaction (n = 1)

### Objective

Measure the actual duration of a complete 1-byte read transaction (register write + repeated START + 1-byte read) over I2C1 at 100 kHz Standard Mode.

### Method

Three `DWT->CYCCNT` timestamps, with no reset between them:

* `start`: immediately before calling `i2c_mem_read()`.
* `t_stop_cycles`: inside `i2c_dma_rx_irq_handler` (`i2c_isr.c`), immediately after `i2c_stop(i2c_handle)` in the TC block (happy path). Stored in a `test_t` field so it can be read from `main.c`.
* `end`: in `main.c`, immediately after the guard wait `while` releases (`SR2 & BUSY == 0` and `state == I2C_IDLE`).

| Timestamp       | Value (cycles) |
| --------------- | -------------: |
| `start`         |             38 |
| `t_stop_cycles` |          19676 |
| `end`           |          19985 |

### Result

| Metric                                               | Cycles |          Time |
| ---------------------------------------------------- | -----: | ------------: |
| From `i2c_mem_read()` to STOP issued                 |  19638 |     392.76 µs |
| Post-STOP BUSY window (STOP → BUSY=0 and state=IDLE) |    309 |       6.18 µs |
| **Total**                                            |  19947 | **398.94 µs** |

### Result Analysis

Compared with the pure bus theoretical value (390 µs), the time up to STOP (392.76 µs) gives a ratio of **1.01x** — within the expected ISR overhead (NVIC latency, handler entry/exit, DMA startup).

### Conclusion

The transaction runs practically at the theoretical bus rate at 100 kHz Standard Mode, with only ~1% software overhead over the minimum time required by the I2C protocol itself.

### Key Point

**Key data:** 398.94 µs total measured for a 1-byte transaction, only 1.01x the theoretical 390 µs — minimal software overhead.

---

## Test 2 — n = 100 (Consecutive Transactions)

### Objective

Measure the average transaction time over a series of 100 consecutive transactions and compare it against the result of a single transaction (Test 1) and the theoretical bus time.

### Method

A `for (cnt = 0; cnt < 100; cnt++)` loop executing `i2c_mem_read()` followed by the guard wait `while` (BUSY=0 and state=IDLE) on each iteration.

A single start and end timestamp are used, both from `DWT->CYCCNT`:

* `start`: before the `for` loop.
* `end`: `DWT->CYCCNT - start` taken after the `for` loop, meaning it is the accumulated total delta of the 100 transactions.

| Timestamp | Value (cycles) |
| --------- | -------------: |
| `start`   |              3 |
| `end`     |        1997421 |

`g_test.trys = 100`, `g_test.success = 100`, with no recorded errors (`af`, `arlo`, `berr`, `dma`, `wdg`, `br` = 0).

### Result

| Metric                           |         Value |
| -------------------------------- | ------------: |
| Total time (100 transactions)    |    39.9484 ms |
| **Average time per transaction** | **399.48 µs** |

### Result Analysis

| Test                         | Time per transaction | Ratio vs. theoretical (390 µs) |
| ---------------------------- | -------------------: | -----------------------------: |
| Theoretical (actual 100 kHz) |            390.00 µs |                          1.00x |
| n = 1 (Test 1)               |            392.76 µs |                          1.01x |
| n = 100 (average)            |            399.48 µs |                          1.02x |

The difference between n=1 and n=100 (≈6.7 µs) is consistent with the post-STOP BUSY window being included in each iteration of the n=100 loop (not present in the same way in the isolated n=1 measurement).

Using the n=1 measured time (from `i2c_mem_read()` to the issued STOP) as the reference against the pure bus theoretical value:

| Metric                           |   Value |
| -------------------------------- | ------: |
| Absolute overhead (392.76 − 390) | 2.76 µs |
| Relative overhead                |  0.71 % |

This overhead corresponds to the software surrounding the transaction (I2C event/error ISR entry latency and DMA ISR latency, handler execution, DMA transfer startup) on top of the minimum time required by the I2C protocol on the bus at 100 kHz.

### Conclusion

The driver effectively operates at 100 kHz Standard Mode. Software overhead (interrupts + DMA startup) over the theoretical bus time is only ~0.71% for a single transaction, and remains in the consistent 1–2.4% range between a single transaction and a series of 100 consecutive transactions, with no errors on the happy path.

### Key Point

**Key data:** 100/100 successful transactions, 399.48 µs average (1.02x theoretical) — stable software overhead between 1 and 100 consecutive transactions.

---

## Test 3 — Software Overhead Determinism (100 Individual Samples)

### Objective

Determine whether the driver's software overhead (I2C event/error ISRs + DMA ISR) is consistent and repeatable across 100 independent transactions, or whether it exhibits relevant variability between samples.

### Method

For each of 100 independent transactions, the `start → t_stop_cycles` segment is measured (from the start of `i2c_mem_read()` to the STOP issued in `i2c_dma_rx_irq_handler`), **excluding** the post-STOP BUSY window and the guard `while` — isolating the driver's software overhead (ISR + DMA) without mixing it with bus behaviour after STOP.

* `DWT->CYCCNT` is reset to 0 on each loop iteration, immediately before `start[cnt] = DWT->CYCCNT`, so each `(start[cnt], stop_cycles[cnt])` pair is independent and comparable.
* `stop_cycles[]` is an array in `test_t` (`volatile uint32_t stop_cycles[100]`), written inside `i2c_dma_rx_irq_handler` immediately after `i2c_stop()`, indexed by `test->success` (post-increment), so each position corresponds to a consecutive successful transaction.
* `start[]` resides in `main.c`.
* 100/100 successes, with no recorded errors (`af/arlo/berr/dma/wdg/br = 0`).

### Result

| Metric                       |  Cycles |        Time |
| ---------------------------- | ------: | ----------: |
| Average                      | 19641.4 |   392.83 µs |
| Minimum                      |   19629 |   392.58 µs |
| Maximum                      |   19649 |   392.98 µs |
| Range (max − min)            |      20 |     0.40 µs |
| Standard deviation           |    6.97 |    0.139 µs |
| **Coefficient of variation** |       — | **0.035 %** |

All individual values fall into one of six discrete values (19629, 19633, 19643, 19645, 19647, 19649 cycles), with no significant outliers or observable trend (drift) across the 100 samples.

### Result Analysis

The pattern of discrete and closely spaced values is consistent with **quantization jitter between clock domains** (DWT/CPU at 50 MHz vs. I2C bus derived from APB1 at 25 MHz with SCL at 100 kHz), rather than actual software variability: the relative phase between the two clocks at the START instant determines which CPU-cycle "step" the measured duration falls into.

### Conclusion

With a CV of 0.035% and a total range of only 0.4 µs over an average of ~393 µs, the driver's software overhead (I2C event/error ISRs + DMA ISR) in the happy path is considered **deterministic** for practical purposes.

### Key Point

**Key data:** CV = 0.035%, range of only 0.40 µs over 100 independent samples — deterministic software overhead in the happy path.

---

## Test 4 — AF Detection Latency (Slave NACK)

### Objective

Measure the detection latency of an AF (Acknowledge Failure) fault caused by an incorrect slave address, and distinguish it from the latency until the channel becomes available for a new transaction.

### Method

In `i2c_er_irq_handler`, the AF branch clears the error flag, issues STOP and sets `err_flag = I2C_ERROR_AF`, but does not modify `i2c_handle->state` or stop TIM14. The state remains at the point in the state machine where the fault was detected until TIM14 expires (~5 ms, `TIMEOUT_CLK_CNT=50` at `TIMER_TICK_HZ=10 kHz`) and `i2c_tim_irq_handler` sets `state = I2C_IDLE`. The watchdog acts as the sole error-resolution mechanism for the entire driver, instead of having an ad-hoc exit path for each error type.

As a consequence, this test distinguishes two times:

* **Detection latency** for AF (what this test measures): on the order of tens of µs.
* **Channel recovery latency** (channel available for the next transaction): ~5 ms, dominated by the timeout, not by detection.

The AF branch of `i2c_er_irq_handler` is instrumented to store `DWT->CYCCNT` immediately after `i2c_stop()`:

```c
if (i2c_handle->i2c->SR1 & (0x1 << 10))
{
    i2c_handle->i2c->SR1 &= ~(0x1 << 10); // Clear flag
    i2c_stop(i2c_handle);
    test->stop_cycles[test->trys - 1] = DWT->CYCCNT;
    i2c_handle->err_flag = I2C_ERROR_AF;
    test->af++;
}
```

`DWT->CYCCNT` is reset to 0 on each loop iteration in `main.c`, immediately before `start[cnt] = DWT->CYCCNT`, so each `(start[cnt], stop_cycles[cnt])` pair measures an independent AF detection. The 100-iteration loop takes ~500 ms in total, due to the 5 ms timeout that must be completed on each iteration before the `main.c` guard `while` releases the next iteration.

Final counters: `trys=100`, `af=100`, `wdg=100` — all 100 iterations completed the expected cycle (start → AF → timeout → IDLE).

### Result

| Metric                       |  Cycles |        Time |
| ---------------------------- | ------: | ----------: |
| Average                      | 4927.16 |    98.54 µs |
| Minimum                      |    4927 |    98.54 µs |
| Maximum                      |    4935 |    98.70 µs |
| Range (max − min)            |       8 |     0.16 µs |
| Standard deviation           |    0.88 |    0.018 µs |
| **Coefficient of variation** |       — | **0.018 %** |

95 of the 100 samples fall exactly at 4927 cycles; the remaining samples are 4 samples at 4929 cycles and 1 at 4935 cycles — with no significant outliers.

### Result Analysis

The sequence up to AF is START + address (7b) + W bit + (N)ACK ≈ 10 bit-equivalents at 100 kHz, i.e. ~100 µs if 10 complete SCL periods were completed. The measured value (98.54 µs) is lower than this theoretical value, which is consistent with the protocol mechanism: AF is generated **during** the ninth clock pulse (the ACK/NACK bit), when the master samples SDA and finds it high, rather than after completing an additional tenth period.

A geometric estimate of "sampling halfway through the ninth pulse" (9 complete periods + half a period ≈ 95 µs) also does not exactly match the measured value — there is a difference of **~3.5 µs with no verified explanation**. The exact point within the ninth pulse at which the peripheral generates the flag has not been confirmed against the Reference Manual (RM0390), so those ~3.5 µs could be due to one or more of the following causes, without isolating which one:

1. **Internal peripheral latency** between the physical SDA sampling and the update of the `AF` flag in `SR1` (signal synchronization, flag-generation logic).
2. **The actual sampling point within the ninth pulse** not being exactly halfway through the period (it could occur closer to the rising or falling edge of SCL than assumed in the geometric estimate).
3. **Quantization jitter between clock domains** (DWT/CPU at 50 MHz vs. I2C bus at 100 kHz), of the same type observed in Test 3, which could also be contributing to this discrepancy.

### Conclusion

Fault detection is as deterministic as the happy path (CV 0.018% vs. 0.035%), and numerically confirms the design gap between "detecting" (~98.5 µs) and "releasing the channel" (~5 ms, dominated by the watchdog). The exact origin of the ~3.5 µs offset relative to the geometric estimate remains an open point, to be confirmed against the Reference Manual if precision at that level is required.

### Key Point

**Key data:** AF detection at 98.54 µs average (CV 0.018%), versus ~5 ms until the channel is released — design gap between detection and resolution.

---

## Test 5 — Actual Watchdog Accuracy (TIM14)

### Objective

Determine how much the actual watchdog timeout (TIM14) deviates from its configured theoretical value, and whether this deviation depends on the timeout value or the tick resolution.

### Method

The fault-injection mechanism (GPIO PA6, open-drain, triggered by TIM2) is used to force an **ARLO** (Arbitration Lost) condition by pulling SDA low at the instant when the master should read its own bit high. The I2C hardware interprets this as arbitration loss. The ARLO branch in `i2c_er_irq_handler` does not automatically resolve the error (it only clears the flag and issues STOP), so the transaction remains pending until the watchdog (TIM14) expires — making it a reproducible way to accurately measure how long the watchdog actually takes to expire compared with its configured theoretical value.

For each attempt (n=100 per configuration), the time from `i2c_mem_read()` until the TIM14 ISR triggers (`stop_cycles`) is measured. The test is repeated across 4 configurations, combining two watchdog tick resolutions (`TIMER_TICK_HZ`) with two theoretical timeout values (`TIMEOUT_CLK_CNT`):

| Configuration | `TIMER_TICK_HZ` | Tick period | `TIMEOUT_CLK_CNT` | Theoretical timeout |
| ------------- | --------------: | ----------: | ----------------: | ------------------: |
| A             |          10 kHz |      100 µs |                50 |             5000 µs |
| B             |          10 kHz |      100 µs |                 5 |              500 µs |
| C             |         100 kHz |       10 µs |                50 |             5000 µs |
| D             |         100 kHz |       10 µs |                 5 |              500 µs |

In all 4 configurations: `trys=100`, `arlo=100`, `wdg=100`, with no other error branch activated.

### Result

| Config | Measured average |   Range |        CV | Delta (measured − theoretical) |
| ------ | ---------------: | ------: | --------: | -----------------------------: |
| A      |       5103.72 µs | 0.10 µs | 0.00021 % |                     +103.72 µs |
| B      |        603.77 µs | 0.10 µs | 0.00766 % |                     +103.77 µs |
| C      |       5013.80 µs | 0.10 µs | 0.00063 % |                      +13.80 µs |
| D      |        513.74 µs | 0.10 µs | 0.00642 % |                      +13.74 µs |

| Config | Tick period | Absolute delta | Delta in ticks |
| ------ | ----------: | -------------: | -------------: |
| A      |      100 µs |      103.72 µs |    1.037 ticks |
| B      |      100 µs |      103.77 µs |    1.038 ticks |
| C      |       10 µs |       13.80 µs |    1.380 ticks |
| D      |       10 µs |       13.74 µs |    1.374 ticks |

### Result Analysis

With a 100 µs tick, the delta is ~103.7 µs for **both** timeouts (5000 and 500 µs). With a 10 µs tick (10x finer), the delta drops to ~13.7–13.8 µs, also for both timeouts. The delta scales with the tick period, not with the `TIMEOUT_CLK_CNT` value.

In all 4 configurations, the absolute range between the fastest and slowest sample is identical (0.10 µs), and the CV remains below 0.008% — the mechanism is deterministic in all cases; the higher CV in the shorter timeouts is an artifact of dividing the same absolute variation by a smaller average, not a sign of lower actual determinism.

The observed delta corresponds, in all 4 configurations, to approximately **1–1.4 complete watchdog ticks**, not to a fixed number of microseconds. This is an expected quantization effect in any auto-reload timer: the exact instant at which the counter is enabled within the prescaler phase, combined with how discrete ticks accumulate until reaching `TIMEOUT_CLK_CNT` overflows, adds up to one complete tick of margin over the nominal value — not overhead from the ISRs involved (the actual Cortex-M4 interrupt latency is 12–20 cycles, on the order of 0.24–0.4 µs at 50 MHz, much smaller than the observed delta).

### Conclusion

When calculating `TIMEOUT_CLK_CNT` for a target timeout, it is necessary to add at least **1 complete tick** (`1 / TIMER_TICK_HZ`) to the desired nominal value, since the actual observed timeout will systematically be greater than `TIMEOUT_CLK_CNT × (1/TIMER_TICK_HZ)`. In systems where timing is critical, increasing `TIMER_TICK_HZ` (finer tick) directly reduces this uncertainty margin in absolute terms — as shown by the comparison between 100 µs and 10 µs ticks, a 10x finer resolution reduced the actual margin by a factor of ~7.5x.

### Key Point

**Key data:** the actual timeout exceeds the configured value by ~1–1.4 watchdog ticks in all 4 tested configurations — margin to budget when calculating `TIMEOUT_CLK_CNT`.

---

## Test 6 — Bus Recovery, Worst Case (SDA Never Released)

### Objective

Evaluate the complete `i2c_bus_recovery()` path in its most demanding scenario — SDA forced low indefinitely — measuring the total time of the routine when all attempts are exhausted without success, and confirming that the manual API recovery mechanism (`clear_bus_unavailable()` + `i2c_init()`) leaves the channel operational afterwards.

### Method

Configuration: watchdog at 5 ms (`TIMER_TICK_HZ=100 kHz`, `TIMEOUT_CLK_CNT=500`).

```c
for (uint16_t cnt = 0; cnt < 100; cnt++)
{
    dma_handle.rx_buffer = &buffer[cnt];
    fault_injection_arm(380, FAULT_NEVER_RELEASE);
    i2c_mem_read(&i2c_handle, &dma_handle, &g_test);
    while ((i2c_handle.i2c->SR2 & 0x2) ||
           (i2c_handle.state != I2C_IDLE && i2c_handle.state != I2C_BUS_UNAVAILABLE));
    if (i2c_handle.state == I2C_BUS_UNAVAILABLE)
    {
        clear_bus_unavailable(&i2c_handle);
        (void)i2c_init(&i2c_handle, &dma_handle);
    }
}
// +1 final read, after channel reset, to confirm that it remained operational
uint8_t sample = 0;
dma_handle.rx_buffer = &sample;
i2c_mem_read(&i2c_handle, &dma_handle, &g_test);
while ((i2c_handle.i2c->SR2 & 0x2) || i2c_handle.state != I2C_IDLE);
```

SDA is forced low at 380 µs from the start of each transaction and held indefinitely (`FAULT_NEVER_RELEASE`). Each failed attempt triggers BERR (retried once, according to `max_retrys`) and ends in watchdog timeout → `i2c_bus_recovery()`. Since SDA cannot be released in either of the 2 bit-banging rounds, the routine returns with an error and the driver sets `I2C_BUS_UNAVAILABLE`. The test then manually clears the state and fully reinitializes the driver.

`stop_cycles` measures the time from the start of `i2c_bus_recovery()` until its return, for each of the 100 attempts (n=100).

### Result

| Counter               |                                                Value |
| --------------------- | ---------------------------------------------------: |
| `trys`                |                    101 (100 failures + 1 final read) |
| `success`             |               1 (final read, after reinitialization) |
| `wdg`                 |                                                  100 |
| `br`                  |                                                  100 |
| `br_recover_routine`  | 200 (2 rounds exhausted in each of the 100 attempts) |
| `br_recover`          |          0 (SDA never detected high, in any attempt) |
| `sample` (final read) | 96 (0x60 — expected BME280 chip ID at register 0xD0) |

**`i2c_bus_recovery()` duration: 11757 cycles = 235.14 µs, identical across all 100 samples (CV = 0%).**

### Result Analysis

All 100 repetitions exhaust exactly the 2 bit-banging rounds (`br_recover_routine=200`, 2 per attempt) without exception, and none detects released SDA (`br_recover=0`) — the driver does not hang or enter any unexpected state in any case; it always consistently ends by setting `I2C_BUS_UNAVAILABLE`.

The total duration (235.14 µs) is consistent with the expected breakdown: up to 200 µs of bit-banging (2 rounds × 10 pulses × 10 µs, always fully exhausted because SDA is never released) plus ~35.14 µs of manual STOP and peripheral reconfiguration (GPIO×2, TIM14, DMA, I2C).

There is no variability across the 100 repetitions (CV=0%), unlike the small dispersion (0.1 µs) observed in other tests — possibly because this path does not depend on the exact instant at which SDA is released (it is never released), eliminating that source of variation.

### Conclusion

The manual recovery mechanism works end-to-end. After `clear_bus_unavailable()` + `i2c_init()`, the verification read returns the correct BME280 chip ID (`0x60`) — the internal driver state does not merely change; the physical channel is actually operational for a new real transaction.

### Key Point

**Key data:** 100/100 cases exhaust the worst-case path without hanging (235.14 µs, CV=0%), and manual recovery leaves the channel operational (chip ID `0x60` read correctly).

---

## Test 7 — Successful Bus Recovery (SDA Released After WDG Trigger)

### Objective

Evaluate `i2c_bus_recovery()` in a scenario where SDA is released *after* the actual watchdog trigger, forcing the routine to generate real bit-banging pulses before detecting the release (unlike a trivial case where SDA would already be released upon entry).

### Method

Configuration: watchdog with a 100 kHz tick (`TIMER_TICK_HZ=100 kHz`, `TIMEOUT_CLK_CNT=500`, actual trigger measured in Test 5 ≈ 5013.8 µs).

```c
for (uint16_t cnt = 0; cnt < 100; cnt++)
{
    dma_handle.rx_buffer = &buffer[cnt];
    fault_injection_arm(380, 5120);
    i2c_mem_read(&i2c_handle, &dma_handle, &g_test);
    while ((i2c_handle.i2c->SR2 & 0x2) ||
           (i2c_handle.state != I2C_IDLE && i2c_handle.state != I2C_BUS_UNAVAILABLE));
    if (i2c_handle.state == I2C_BUS_UNAVAILABLE)
        break;

    // Second read, normal, to verify that the channel remained operational
    i2c_mem_read(&i2c_handle, &dma_handle, &g_test);
    while ((i2c_handle.i2c->SR2 & 0x2) ||
           (i2c_handle.state != I2C_IDLE && i2c_handle.state != I2C_BUS_UNAVAILABLE));
    if (i2c_handle.state == I2C_BUS_UNAVAILABLE)
        break;
}
```

SDA is forced low at 380 µs and released at 5120 µs from the start of each transaction — after the actual instant at which the watchdog triggers bus recovery in this configuration (~5013.8 µs), so the routine does need to generate real clock pulses before detecting the release. Each iteration performs two reads: the first, with the fault armed, produces BERR → timeout → bus recovery; the second, without the fault, verifies that the channel remained operational after recovery.

`stop_cycles` measures the duration of `i2c_bus_recovery()` in each of the 100 attempts.

### Result

| Counter              |                                                    Value |
| -------------------- | -------------------------------------------------------: |
| `trys`               |                           200 (2 reads × 100 iterations) |
| `success`            |                                   100 (all second reads) |
| `berr`               |                                                      100 |
| `wdg`                |                                                      100 |
| `br`                 |                                                      100 |
| `br_recover_routine` | 100 (1 round per attempt — the second is never required) |
| `br_recover`         |                     100 (bus recovered in 100% of cases) |

**`i2c_bus_recovery()` duration:** 144.02 µs in 97/100 samples; 138.98 µs in 1/100 samples (difference of 5.04 µs).

### Result Analysis

| Quantity                                               |       Value |
| ------------------------------------------------------ | ----------: |
| Actual watchdog trigger in this configuration (Test 5) | ≈ 5013.8 µs |
| SDA release instant (fault injection)                  |     5120 µs |
| Bit-banging required before detecting SDA high         |  ≈ 106.2 µs |
| Total measured duration (modal value)                  |   144.02 µs |
| Remainder (manual STOP + peripheral reconfiguration)   |   ≈ 37.8 µs |
| Equivalent remainder in Test 6 (SDA never released)    |   ≈ 35.1 µs |

The remainder obtained by subtraction (~37.8 µs) is consistent with the same reconfiguration block independently measured in Test 6 (~35.1 µs) — both values correspond to the same operation (`i2c_bus_recovery_peripheral_config()` + manual STOP), and their proximity supports that the bit-banging / reconfiguration breakdown is correct.

All 100 repetitions detect the release within the first round and successfully recover the bus. The observed variability (97/100 at one value, 1/100 with a difference of ~5 µs) is consistent with the fact that the "SDA high" detection point depends on the relative phase between two independent timers (TIM2 for fault injection, TIM14 for the watchdog, and the recovery bit-banging itself) — with such a tight margin (~106 µs against discrete 10 µs pulses), a small phase difference between the clocks can shift detection by one pulse.

### Conclusion

The channel is fully operational after automatic recovery, without requiring manual intervention (`clear_bus_unavailable()`) — unlike Test 6, here `i2c_bus_recovery()` returns success on its own and the second read of each iteration completes normally in 100% of cases.

### Key Point

**Key data:** 100/100 successful recoveries in 144.02 µs (detection in the first round), with reconfiguration overhead (~37.8 µs) consistent with the independently measured value in Test 6 (~35.1 µs).
