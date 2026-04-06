#ifndef __FSM_SIGNALS_H__
#define __FSM_SIGNALS_H__

#include <stdint.h>

enum fsm_signal : uint8_t {
    SIG_ENTRY = 0,      // synthesised by dispatcher on state enter
    SIG_EXIT,           // synthesised by dispatcher on state exit
    SIG_INIT,
    SIG_USER_CODE
};


#endif