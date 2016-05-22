/* ========================================================================== */
/*                                                                            */
/*   log.h                                                                */
/*   (c) 2016 TheDrake                                                        */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

#ifndef __LOG_H__
#define __LOG_H__

#ifdef __cplusplus
#define extern "C" {		/* respect c++ callers */
#endif

extern void log_message(const int log, const char* format, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __LOG_H__ */

