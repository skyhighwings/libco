/* co.h -- libco header */
/* Copyright (C) 2015 Alex Iadicicco */

#ifndef __INC_CO_H__
#define __INC_CO_H__

#include <unistd.h>

typedef int                            co_err_t;

typedef enum co_open_type              co_open_type_t;

typedef struct co_context              co_context_t;
typedef struct co_file                 co_file_t;

enum co_open_type {
	CO_RDONLY,
	CO_WRONLY,
	CO_RDWR,
	CO_APPEND
};

typedef void co_thread_fn(
	co_context_t                  *ctx,
	void                          *user
	);


/* THREAD OPERATIONS
   ========================================================================= */

extern co_err_t co_spawn(
	co_context_t                  *ctx,
	co_thread_fn                  *start,
	void                          *user
	);


/* IO OPERATIONS
   ========================================================================= */

extern co_err_t co_read(
	co_context_t                  *ctx,
	co_file_t                     *file,
	void                          *buf,
	size_t                         nbyte,
	ssize_t                       *rsize
	);

extern co_err_t co_write(
	co_context_t                  *ctx,
	co_file_t                     *file,
	const void                    *buf,
	size_t                         nbyte,
	ssize_t                       *wsize
	);

/* -- filesystem -- */

extern co_file_t *co_open(
	co_context_t                  *ctx,
	const char                    *path,
	co_open_type_t                 typ,
	unsigned                       mode
	);

extern void co_close(
	co_context_t                  *ctx,
	co_file_t                     *file
	);


/* -- sockets -- */

extern co_file_t *co_connect_tcp(
	co_context_t                  *ctx,
	const char                    *host,
	unsigned short                 port
	);

extern co_file_t *co_bind_tcp(
	co_context_t                  *ctx,
	const char                    *host,
	unsigned short                 port,
	int                            backlog
	);


/* HELPERS
   ========================================================================= */

extern co_err_t co_printf(
	co_context_t                  *ctx,
	const char                    *fmt,
	...
	);


/* CONTEXT MANAGEMENT
   ========================================================================= */

extern co_context_t *co_init(void);

extern void co_run(
	co_context_t                  *ctx,
	co_thread_fn                  *start,
	void                          *user
	);

#endif
