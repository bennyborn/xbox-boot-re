/* Stand-in for the generated trace header: all events off. Own file. */
#pragma once
#define trace_event_get_state(x) 0
#define TRACE_DSP56K_EXECUTE_INSTRUCTION_DISASM 0
#define trace_dsp_read_peripheral(a, v) do {} while (0)
#define trace_dsp_write_peripheral(a, v) do {} while (0)
#define trace_dsp56k_execute_instruction(a, b) do {} while (0)
#define trace_dsp56k_execute_instruction_disasm(a) do {} while (0)
