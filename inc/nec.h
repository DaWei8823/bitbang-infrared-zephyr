#include <stdbool.h>
#include <stdint.h>
#include <sys/_stdint.h>

// TODO: maybe add repeat codes
typedef struct {
	const uint32_t address_num_bits;
	const uint32_t command_num_bits;
	const struct {
		uint32_t pulse_usecs;
		uint32_t period_usecs;
	} start_symbol;
	const uint32_t stop_symbol_pulse_usecs;
	const uint32_t data_bit_pulse_usecs;
	const uint32_t zero_symbol_period_usecs;
	const uint32_t one_symbol_period_usecs;
	const bool addr_verify_inverse;
	const bool cmd_verify_inverse;
} nec_protocol_t;

typedef struct {
	uint32_t usecs_tolerance;
} nec_platform_t;

typedef enum {
	NEC_STAGE_IDLE = 0,
	NEC_STAGE_START_SYMBOL,
	NEC_STAGE_ADDR,
	NEC_STAGE_ADDR_INV,
	NEC_STAGE_CMD,
	NEC_STAGE_CMD_INV,
	NEC_STAGE_STOP_SYMBOL,
	NEC_STAGE_FINISHED,
	NEC_STAGE_MAX = NEC_STAGE_FINISHED,
} nec_stage_t;

typedef struct {
	nec_stage_t stage;
	uint32_t bit_idx;
	uint64_t symbol_start_usecs;
	uint32_t addr;
	uint32_t cmd;
	uint32_t inv_addr;
	uint32_t inv_cmd;
} nec_state_t;

typedef enum {
	NEC_ERR_NONE = 0,
	NEC_ERR_STATE_NULL,
	NEC_ERR_PROTOCOL_NULL,
	NEC_ERR_PLATFORM_NULL,
	NEC_ERR_INVALID_STAGE,
	NEC_ERR_PULSE_TOO_SHORT,
	NEC_ERR_UNEXPECTED_LINE_HIGH,
	NEC_ERR_SYMBOL_PERIOD_TOO_LONG,
} nec_err_t;

nec_err_t nec_init_state(nec_state_t *state);

nec_err_t nec_process_sample(nec_state_t *state, const nec_protocol_t *protocol,
			     const nec_platform_t *platform, bool sample, uint64_t timestamp_usecs);
