//
// Copyright 2017 Garrett D'Amore <garrett@damore.org>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include "core/defs.h"
#include "core/list.h"

struct nng_event {
	int		e_type;
	uint32_t	e_sockid;
	nni_sock *	e_sock;
	nni_ep *	e_ep;
	nni_pipe *	e_pipe;

	int		e_done;         // true when notify thr is finished
	int		e_pending;      // true if event is queued
	nni_cv		e_cv;           // signaled when e_done is noted
	nni_list_node	e_node;         // location on the socket list
};

struct nng_notify {
	nni_list_node	n_node;
	nng_notify_func n_func;
	void *		n_arg;
	int		n_mask;
	nni_compl	n_compl;
	nng_event	n_ev;
};

extern void nni_notifier(void *);
extern int nni_ev_init(nni_event *, int, nni_sock *);
extern void nni_ev_fini(nni_event *);
extern void nni_ev_submit(nni_event *);         // call holding sock lock
extern void nni_ev_wait(nni_event *);           // call holding sock lock
extern nni_notify *nni_add_notify(nni_sock *, int, nng_notify_func, void *);
extern void nni_rem_notify(nni_sock *, nni_notify *);

#endif // CORE_EVENT_H
