/*
 * Copyright (c) David Ullmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/drivers/gpio.h"
#include "zephyr/dt-bindings/gpio/gpio.h"
#include <stdint.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <nec.h>
#include <zephyr/logging/log.h>

#define USECS_PER_SEC 1000U * 1000U

#define RECEIVER_NODE DT_ALIAS(ir_receiver)
#if !DT_NODE_HAS_STATUS(RECEIVER_NODE, okay)
#error "Unsupported board: ir-receiver devicetree alias is not defined"
#endif

static const struct gpio_dt_spec ir_receiver = GPIO_DT_SPEC_GET(RECEIVER_NODE, gpios);

LOG_MODULE_REGISTER(ir, LOG_LEVEL_INF);

const nec_protocol_t necext_protocol = {
	.address_num_bits = 16U,
	.command_num_bits = 16U,
	.addr_verify_inverse = false,
	.cmd_verify_inverse = false,
	.start_symbol =
		{
			.pulse_usecs = 9U * 1000U,
			.period_usecs = 13500U,
		},
	.stop_symbol_pulse_usecs = 562U,
	.data_bit_pulse_usecs = 562U,
	.zero_symbol_period_usecs = 562U * 2,
	.one_symbol_period_usecs = 562U + 1687U,
};

const nec_platform_t platform = {
	.usecs_tolerance = 75U,
};

int main(void)
{
	int err = 0;

	if (!gpio_is_ready_dt(&ir_receiver)) {
		LOG_ERR("reciever pin not ready.");
		return err;
	}

	err = gpio_pin_configure_dt(&ir_receiver, ir_receiver.dt_flags | GPIO_INPUT);
	if (err) {
		LOG_ERR("error configurint reciever pin. err = %d", err);
		return err;
	}
	return 0;
}

uint64_t get_timestamp_usecs()
{
	return k_uptime_ticks() * USECS_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

static int record_handler(const struct shell *sh, size_t argc, char **argv)
{
	nec_err_t err = NEC_ERR_NONE;
	bool gpio_val = false;
	uint64_t timestamp_usecs = 0;

	nec_state_t state;
	err = nec_init_state(&state);
	if (NEC_ERR_NONE != err) {
		shell_print(sh, "error initializing state: %u", err);
		return err;
	}

	while (NEC_STAGE_FINISHED != state.stage) {
		timestamp_usecs = get_timestamp_usecs();
		gpio_val = gpio_pin_get_dt(&ir_receiver);
		err = nec_process_sample(&state, &necext_protocol, &platform, gpio_val,
					 timestamp_usecs);
		if (NEC_ERR_NONE != err) {
			shell_print(sh, "error decoding signal. stage = %u, err = %u", state.stage,
				    err);
			return err;
		}
	}

	shell_print(sh, "addr: 0x%X, cmd: 0x%X", state.addr, state.cmd);
	return err;
}

SHELL_CMD_REGISTER(record, NULL, "record IR signal", record_handler);
