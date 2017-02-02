//
// Copyright 2017 Garrett D'Amore <garrett@damore.org>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "core/nng_impl.h"

#include <stdlib.h>
#include <string.h>

int
nni_ev_init(nni_event *event, int type, nni_sock *sock)
{
	int rv;

	memset(event, 0, sizeof (*event));
	if ((rv = nni_cv_init(&event->e_cv, &sock->s_mx)) != 0) {
		return (rv);
	}
	NNI_LIST_NODE_INIT(&event->e_node);
	event->e_type = type;
	event->e_sock = sock;
	event->e_sockid = sock->s_id;
	return (0);
}


void
nni_ev_fini(nni_event *event)
{
	nni_cv_fini(&event->e_cv);
}


void
nni_ev_submit(nni_event *event)
{
	nni_sock *sock = event->e_sock;

	// If nobody is listening, don't bother submitting anything.
	// This reduces pressure on the socket locks & condvars, in the
	// typical case.
	if (nni_list_first(&sock->s_notify) == NULL) {
		event->e_pending = 0;
		event->e_done = 1;
		return;
	}

	// Call with socket mutex owned!
	if (event->e_pending == 0) {
		event->e_pending = 1;
		event->e_done = 0;
		nni_list_append(&sock->s_events, event);
		nni_cv_wake(&sock->s_notify_cv);
	}
}


void
nni_ev_wait(nni_event *event)
{
	// Call with socket mutex owned!
	// Note that the socket mutex is dropped during the call.
	while ((event->e_pending) && (!event->e_done)) {
		nni_cv_wait(&event->e_cv);
	}
}


void
nni_notifier(void *arg)
{
	nni_sock *sock = arg;
	nni_event *event;
	nni_notify *notify;

	nni_mtx_lock(&sock->s_mx);
	for (;;) {
		if (sock->s_closing) {
			break;
		}

		if ((event = nni_list_first(&sock->s_events)) != NULL) {
			event->e_pending = 0;
			nni_list_remove(&sock->s_events, event);
			nni_mtx_unlock(&sock->s_mx);

			// Lock the notify list, it must not change.
			nni_mtx_lock(&sock->s_notify_mx);
			NNI_LIST_FOREACH (&sock->s_notify, notify) {
				if ((notify->n_mask & event->e_type) == 0) {
					// No interest.
					continue;
				}
				notify->n_func(event, notify->n_arg);
			}
			nni_mtx_unlock(&sock->s_notify_mx);

			nni_mtx_lock(&sock->s_mx);
			// Let the event submitter know we are done, unless
			// they have resubmitted.  Submitters can wait on this
			// lock.
			event->e_done = 1;
			nni_cv_wake(&event->e_cv);
			continue;
		}

		nni_cv_wait(&sock->s_notify_cv);
	}
	nni_mtx_unlock(&sock->s_mx);
}


static void
nni_notify_can_sendrecv(void *arg)
{
	nni_notify *notify = arg;

	notify->n_func(&notify->n_ev, notify->n_arg);
}


nni_notify *
nni_add_notify(nni_sock *sock, int mask, nng_notify_func fn, void *arg)
{
	nni_notify *notify;

	if ((notify = NNI_ALLOC_STRUCT(notify)) == NULL) {
		return (NULL);
	}
	memset(notify, 0, sizeof (*notify));
	notify->n_func = fn;
	notify->n_arg = arg;
	notify->n_mask = mask;
	nni_ev_init(&notify->n_ev, mask, sock);

	NNI_LIST_NODE_INIT(&notify->n_node);

	switch (mask) {
	case NNG_EV_CAN_RCV:
		nni_msgq_notify_canget(&notify->n_compl, sock->s_urq,
		    nni_notify_can_sendrecv, notify);
		break;
	case NNG_EV_CAN_SND:
		nni_msgq_notify_canput(&notify->n_compl, sock->s_uwq,
		    nni_notify_can_sendrecv, notify);
		break;
	default:
		break;
	}
	return (notify);
}


void
nni_rem_notify(nni_sock *sock, nni_notify *notify)
{
	nni_compl_fini(&notify->n_compl);
	NNI_FREE_STRUCT(notify);
}
