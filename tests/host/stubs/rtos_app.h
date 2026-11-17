#ifndef __RTOS_APP_H
#define __RTOS_APP_H

#include "sys.h"

/* Host state-machine tests run the bare-metal path. */
#define RTOS_ENABLE 0

#define RTOS_LcdLock()    ((void)0)
#define RTOS_LcdUnlock()  ((void)0)

#endif
