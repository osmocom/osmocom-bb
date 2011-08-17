#ifndef OSMOCORE_PANIC_H
#define OSMOCORE_PANIC_H

/*! \addtogroup utils
 *  @{
 */

/*! \file panic.h */

#include <stdarg.h>

/*! \brief panic handler callback function type */
typedef void (*osmo_panic_handler_t)(const char *fmt, va_list args);

extern void osmo_panic(const char *fmt, ...);
extern void osmo_set_panic_handler(osmo_panic_handler_t h);

/*! @} */

#endif /* OSMOCORE_PANIC_H */
