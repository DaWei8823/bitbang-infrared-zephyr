#include <nec.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

typedef nec_err_t (*_nec_stage_handler_t)(nec_state_t *state, const nec_protocol_t *protocol,
					  const nec_platform_t *platform, bool sample,
					  uint64_t timestamp);

static nec_err_t nec_handle_idle(nec_state_t *state, const nec_protocol_t *protocol,
				 const nec_platform_t *platform, bool sample, uint64_t timestamp);

static nec_err_t nec_handle_start_symbol(nec_state_t *state, const nec_protocol_t *protocol,
					 const nec_platform_t *platform, bool sample,
					 uint64_t timestamp);

static nec_err_t nec_handle_addr(nec_state_t *state, const nec_protocol_t *protocol,
				 const nec_platform_t *platform, bool sample, uint64_t timestamp);

static nec_err_t nec_handle_cmd(nec_state_t *state, const nec_protocol_t *protocol,
				const nec_platform_t *platform, bool sample, uint64_t timestamp);

static nec_err_t nec_handle_stop_symbol(nec_state_t *state, const nec_protocol_t *protocol,
					const nec_platform_t *platform, bool sample,
					uint64_t timestamp);

static nec_err_t handle_generic_zero_one_symbol(const nec_protocol_t *protocol,
						const nec_platform_t *platform, bool sample,
						uint64_t symbol_elapsed_usecs, bool *bit_out,
						bool *symbol_complete);

const _nec_stage_handler_t _stage_handlers[] = {
	[NEC_STAGE_IDLE] = nec_handle_idle,
	[NEC_STAGE_START_SYMBOL] = nec_handle_start_symbol,
	[NEC_STAGE_ADDR] = nec_handle_addr,
	[NEC_STAGE_ADDR_INV] = NULL,
	[NEC_STAGE_CMD] = nec_handle_cmd,
	[NEC_STAGE_CMD_INV] = NULL,
	[NEC_STAGE_STOP_SYMBOL] = nec_handle_stop_symbol,
	[NEC_STAGE_FINISHED] = NULL,
};

nec_err_t nec_init_state(nec_state_t *state)
{
	state->stage = NEC_STAGE_IDLE;
	state->bit_idx = 0;
	state->symbol_start_usecs = 0;
	state->addr = 0;
	state->cmd = 0;
	state->inv_addr = 0;
	state->inv_cmd = 0;
	return NEC_ERR_NONE;
}

nec_err_t nec_process_sample(nec_state_t *state, const nec_protocol_t *protocol,
			     const nec_platform_t *platform, bool sample, uint64_t timestamp)
{
	_nec_stage_handler_t handler = NULL;
	if (NULL == state) {
		return NEC_ERR_STATE_NULL;
	}

	if (NULL == protocol) {
		return NEC_ERR_PROTOCOL_NULL;
	}

	if (NULL == platform) {
		return NEC_ERR_PLATFORM_NULL;
	}

	if (state->stage > NEC_STAGE_MAX) {
		return NEC_ERR_INVALID_STAGE;
	}

	handler = _stage_handlers[state->stage];
	assert(NULL != handler);

	return handler(state, protocol, platform, sample, timestamp);
}

nec_err_t nec_handle_idle(nec_state_t *state, const nec_protocol_t *protocol,
			  const nec_platform_t *platform, bool sample, uint64_t timestamp)
{
	if (!sample) {
		return NEC_ERR_NONE;
	}

	state->stage = NEC_STAGE_START_SYMBOL;
	state->symbol_start_usecs = timestamp;
	return NEC_ERR_NONE;
}

nec_err_t nec_handle_start_symbol(nec_state_t *state, const nec_protocol_t *protocol,
				  const nec_platform_t *platform, bool sample, uint64_t timestamp)
{
	uint32_t elapsed_usecs = timestamp - state->symbol_start_usecs;
	uint32_t min_pulse = protocol->start_symbol.pulse_usecs - platform->usecs_tolerance;
	uint32_t max_pulse = protocol->start_symbol.pulse_usecs + platform->usecs_tolerance;
	uint32_t min_period = protocol->start_symbol.period_usecs - platform->usecs_tolerance;
	uint32_t max_period = protocol->start_symbol.period_usecs + platform->usecs_tolerance;

	if (elapsed_usecs < min_pulse) {
		if (!sample) {
			return NEC_ERR_PULSE_TOO_SHORT;
		}
	} else if (elapsed_usecs < max_pulse) {
	} else if (elapsed_usecs < min_period) {
		if (sample) {
			return NEC_ERR_UNEXPECTED_LINE_HIGH;
		}
	} else if (elapsed_usecs < max_period) {
		if (sample) {
			state->symbol_start_usecs = timestamp;
			state->stage = NEC_STAGE_ADDR;
		}
	} else if (elapsed_usecs > max_period) {
		return NEC_ERR_SYMBOL_PERIOD_TOO_LONG;
	}

	return NEC_ERR_NONE;
}

nec_err_t nec_handle_addr(nec_state_t *state, const nec_protocol_t *protocol,
			  const nec_platform_t *platform, bool sample, uint64_t timestamp)
{
	uint32_t elapsed_usecs = timestamp - state->symbol_start_usecs;
	bool value;
	bool complete = false;
	nec_err_t err = handle_generic_zero_one_symbol(protocol, platform, sample, elapsed_usecs,
						       &value, &complete);
	if (NEC_ERR_NONE != err || !complete) {
		return err;
	}

	if (value) {
		state->addr |= (1U << state->bit_idx++);
	} else {
		state->addr &= ~(1U << state->bit_idx++);
	}

	if (state->bit_idx == protocol->address_num_bits) {
		state->stage = protocol->addr_verify_inverse ? NEC_STAGE_ADDR_INV : NEC_STAGE_CMD;
		state->bit_idx = 0;
	}

	state->symbol_start_usecs = timestamp;
	return err;
}

nec_err_t nec_handle_cmd(nec_state_t *state, const nec_protocol_t *protocol,
			 const nec_platform_t *platform, bool sample, uint64_t timestamp)
{

	uint32_t elapsed_usecs = timestamp - state->symbol_start_usecs;
	bool value;
	bool complete;
	nec_err_t err = handle_generic_zero_one_symbol(protocol, platform, sample, elapsed_usecs,
						       &value, &complete);
	if (NEC_ERR_NONE != err || !complete) {
		return err;
	}

	if (value) {
		state->cmd |= (1U << state->bit_idx++);
	} else {
		state->cmd &= ~(1U << state->bit_idx++);
	}

	if (state->bit_idx == protocol->command_num_bits) {
		state->stage =
			protocol->addr_verify_inverse ? NEC_STAGE_CMD_INV : NEC_STAGE_STOP_SYMBOL;
		state->bit_idx = 0;
	}

	state->symbol_start_usecs = timestamp;
	return err;
}

static nec_err_t nec_handle_stop_symbol(nec_state_t *state, const nec_protocol_t *protocol,
					const nec_platform_t *platform, bool sample,
					uint64_t timestamp)
{

	uint32_t pulse_min = protocol->stop_symbol_pulse_usecs - platform->usecs_tolerance;
	uint32_t pulse_max = protocol->stop_symbol_pulse_usecs + platform->usecs_tolerance;
	uint32_t elapsed_usecs = timestamp - state->symbol_start_usecs;
	if (elapsed_usecs < pulse_min) {
		if (!sample) {
			return NEC_ERR_PULSE_TOO_SHORT;
		}
	} else if (elapsed_usecs < pulse_max) {
		if (!sample) {
			state->stage = NEC_STAGE_FINISHED;
		}
	} else {
		if (sample) {
			return NEC_ERR_UNEXPECTED_LINE_HIGH;
		}
	}
	return NEC_ERR_NONE;
}

nec_err_t handle_generic_zero_one_symbol(const nec_protocol_t *protocol,
					 const nec_platform_t *platform, bool sample,
					 uint64_t symbol_elapsed_usecs, bool *bit_out,
					 bool *symbol_complete)
{
	*symbol_complete = 0;

	uint32_t pulse_min = protocol->data_bit_pulse_usecs - platform->usecs_tolerance;
	uint32_t pulse_max = protocol->data_bit_pulse_usecs + platform->usecs_tolerance;

	uint32_t shorter_symbol_period;
	uint32_t longer_symbol_period;
	bool symbol_with_shorter_period_value;
	bool symbol_with_longer_period_value;
	if (protocol->zero_symbol_period_usecs < protocol->one_symbol_period_usecs) {
		shorter_symbol_period = protocol->zero_symbol_period_usecs;
		longer_symbol_period = protocol->one_symbol_period_usecs;
		symbol_with_shorter_period_value = false;
		symbol_with_longer_period_value = true;
	} else {
		shorter_symbol_period = protocol->one_symbol_period_usecs;
		longer_symbol_period = protocol->zero_symbol_period_usecs;
		symbol_with_shorter_period_value = false;
		symbol_with_longer_period_value = true;
	}

	uint32_t shorter_period_min = shorter_symbol_period - platform->usecs_tolerance;
	uint32_t shorter_period_max = shorter_symbol_period + platform->usecs_tolerance;
	uint32_t longer_period_min = longer_symbol_period - platform->usecs_tolerance;
	uint32_t longer_period_max = longer_symbol_period + platform->usecs_tolerance;
	if (symbol_elapsed_usecs < pulse_min) {
		if (!sample) {
			return NEC_ERR_PULSE_TOO_SHORT;
		}
	} else if (symbol_elapsed_usecs < pulse_max) {

	} else if (symbol_elapsed_usecs < shorter_period_min) {
		if (sample) {
			return NEC_ERR_UNEXPECTED_LINE_HIGH;
		}
	} else if (symbol_elapsed_usecs < shorter_period_max) {
		if (sample) {
			*symbol_complete = true;
			*bit_out = symbol_with_shorter_period_value;
		}
	} else if (symbol_elapsed_usecs < longer_period_min) {
		if (sample) {
			return NEC_ERR_UNEXPECTED_LINE_HIGH;
		}
	} else if (symbol_elapsed_usecs < longer_period_max) {
		if (sample) {
			*symbol_complete = true;
			*bit_out = symbol_with_longer_period_value;
		}
	} else if (symbol_elapsed_usecs > longer_period_max) {
		return NEC_ERR_SYMBOL_PERIOD_TOO_LONG;
	}
	return NEC_ERR_NONE;
}
