#ifndef __FSM_SIGNALS_H__
#define __FSM_SIGNALS_H__

#include "stdint.h"

enum fsm_signal : uint8_t {
    SIG_ENTRY = 0,   
    SIG_EXIT,         
    SIG_INIT,
    SIG_USER_CODE
};


#endif