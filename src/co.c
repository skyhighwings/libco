/* co.c -- libco */
/* Copyright (C) 2015 Alex Iadicicco */

#include <co.h>

#include "event.h"
#include "thread.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define error(x) abort()

struct co_context {
	thread_context_t *threads;
	int num_threads;
	struct new_thread *new_thread;
	co_file_t *files;
};

struct co_file {
	thread_t *waiting;
	int fd;
	co_file_t *next;
	co_file_t *prev;
};

struct new_thread {
	struct new_thread *next;
	co_context_t *ctx;
	co_thread_fn *start;
	void *user;
	thread_t *t;
};

static void start_thread(thread_context_t *ctx, void *priv) {
	struct new_thread *nt = priv;
	nt->start(nt->ctx, nt->user);
	nt->ctx->num_threads --;
	free(nt);
}

static void add_file(co_context_t *ctx, co_file_t *f) {
	if (ctx->files) ctx->files->prev = f;

	f->next = ctx->files;
	f->prev = NULL;

	ctx->files = f;
}

static void remove_file(co_context_t *ctx, co_file_t *f) {
	if (ctx->files == f) ctx->files = f->next;

	if (f->next) f->next->prev = f->prev;
	if (f->prev) f->prev->next = f->next;
}

co_err_t co_spawn(
	co_context_t                  *ctx,
	co_thread_fn                  *start,
	void                          *user
) {
	struct new_thread *next;

	next = calloc(1, sizeof(*next));
	next->ctx = ctx;
	next->start = start;
	next->user = user;
	next->t = thread_create(ctx->threads, start_thread, next);
	next->next = ctx->new_thread;
	ctx->new_thread = next;
	ctx->num_threads ++;

	return 0;
}

co_err_t co_read(
	co_context_t                  *ctx,
	co_file_t                     *file,
	void                          *buf,
	size_t                         nbyte,
	ssize_t                       *rsize
) {
	ssize_t rsz;

	if (file->waiting != NULL) {
		error("another context is already waiting on this file");
		return -1;
	}

	file->waiting = thread_self(ctx->threads);
	event_fd_want_read(file->fd);

	thread_defer_self(ctx->threads);

	rsz = read(file->fd, buf, nbyte);
	if (rsize) *rsize = rsz;
	return 0;
}

co_err_t co_write(
	co_context_t                  *ctx,
	co_file_t                     *file,
	const void                    *buf,
	size_t                         nbyte,
	ssize_t                       *wsize
) {
	ssize_t wsz;

	if (file->waiting != NULL) {
		error("another context is already waiting on this file");
		return -1;
	}

	file->waiting = thread_self(ctx->threads);
	event_fd_want_write(file->fd);

	thread_defer_self(ctx->threads);

	wsz = write(file->fd, buf, nbyte);
	if (wsize) *wsize = wsz;
	return 0;
}

co_file_t *co_open(
	co_context_t                  *ctx,
	const char                    *path,
	co_open_type_t                 typ,
	unsigned                       mode
) {
	int fd;
	co_file_t *f;

	if (!(fd = open(path, O_RDONLY)))
		return NULL;

	f = calloc(1, sizeof(*f));
	f->waiting = NULL;
	f->fd = fd;
	add_file(ctx, f);
	ctx->files = f;

	return f;
}

void co_close(
	co_context_t                  *ctx,
	co_file_t                     *f
) {
	if (f == NULL)
		return;

	remove_file(ctx, f);

	close(f->fd);
	free(f);
}

co_file_t *co_connect_tcp(
	co_context_t                  *ctx,
	const char                    *host,
	unsigned short                 port
) {
	struct addrinfo gai_hints;
	struct addrinfo *gai;
	struct sockaddr_in *sa4;
	struct sockaddr_in6 *sa6;
	void *addr;
	int sock, err;
	co_file_t *f;
	char buf[512];

	/* TODO: replace this with an async lookup mechanism */
	memset(&gai_hints, 0, sizeof(gai_hints));
	gai_hints.ai_family = AF_INET;
	gai_hints.ai_socktype = SOCK_STREAM;
	gai_hints.ai_protocol = 0;
	if ((err = getaddrinfo(host, NULL, &gai_hints, &gai)) != 0) {
		printf("co_connect_tcp failed: %s\n", gai_strerror(err));
		return NULL;
	}
	if (gai->ai_addr == NULL)
		goto fail_addr;

	switch (gai->ai_family) {
	case AF_INET:
		sa4 = (void*)gai->ai_addr;
		sa4->sin_port = htons(port);
		addr = &sa4->sin_addr;
		break;
	case AF_INET6:
		sa6 = (void*)gai->ai_addr;
		sa6->sin6_port = htons(port);
		addr = &sa6->sin6_addr;
		break;
	default:
		goto fail_addr;
	}

	if ((sock = socket(gai->ai_family, SOCK_STREAM, 0)) < 0)
		goto fail_addr;
	fcntl(sock, F_SETFL, O_NONBLOCK);

	f = calloc(1, sizeof(*f));
	f->waiting = NULL;
	f->fd = sock;
	f->next = f->prev = NULL;
	add_file(ctx, f);

	for (;;) {
		err = connect(sock, gai->ai_addr, gai->ai_addrlen);
		if (err == 0)
			break;

		switch (errno) {
		case EINPROGRESS:
		case EINTR:
		case EALREADY:
			f->waiting = thread_self(ctx->threads);
			event_fd_want_write(f->fd);
			thread_defer_self(ctx->threads);
			continue;

		default:
			goto fail_connect;
		}
	}

	freeaddrinfo(gai);
	return f;

fail_connect:
	perror("co_connect_tcp failed");
	remove_file(ctx, f);
	free(f);
	close(sock);
fail_addr:
	freeaddrinfo(gai);
	return NULL;
}

co_context_t *co_init(void) {
	co_context_t *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->threads = thread_context_new();
	ctx->new_thread = NULL;
	ctx->files = NULL;

	event_init();

	return ctx;
}

static thread_t *unwait(co_context_t *ctx, int fd) {
	co_file_t *f;

	for (f=ctx->files; f; f=f->next) {
		if (f->fd == fd) {
			thread_t *t = f->waiting;
			f->waiting = NULL;
			return t;
		}
	}

	return NULL;
}

static thread_t *thread_poller(thread_context_t *threads, void *_ctx) {
	thread_t *res;
	co_context_t *ctx = _ctx;
	event_polled_t evt;
	co_file_t *f;

	if (ctx->num_threads == 0) {
		thread_context_stop(threads);
		return NULL;
	}

	if (ctx->new_thread) {
		struct new_thread *next = ctx->new_thread->next;
		res = ctx->new_thread->t;
		ctx->new_thread = next;
		return res;
	}

	if (event_poll(&evt)) {
		switch (evt.tag) {
		case EVENT_NOTHING:
			break;
		case EVENT_FD_CAN_READ:
		case EVENT_FD_CAN_WRITE:
			return unwait(ctx, evt.v.fd);
		}
	}

	return NULL;
}

void co_run(
	co_context_t                  *ctx,
	co_thread_fn                  *start,
	void                          *user
) {
	co_spawn(ctx, start, user);

	thread_context_run(ctx->threads, thread_poller, ctx);
}
