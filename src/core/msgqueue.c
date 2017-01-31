//
// Copyright 2017 Garrett D'Amore <garrett@damore.org>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng_impl.h"

// Message queue.  These operate in some respects like Go channels,
// but as we have access to the internals, we have made some fundamental
// differences and improvements.  For example, these can grow, and either
// side can close, and they may be closed more than once.

struct nni_msgq {
	nni_mtx			mq_lock;
	nni_cv			mq_readable;
	nni_cv			mq_writeable;
	nni_cv			mq_drained;
	int			mq_cap;
	int			mq_alloc; // alloc is cap + 2...
	int			mq_len;
	int			mq_get;
	int			mq_put;
	int			mq_closed;
	int			mq_puterr;
	int			mq_geterr;
	int			mq_rwait; // readers waiting (unbuffered)
	int			mq_wwait;
	nni_msg **		mq_msgs;

	nni_taskq_ent		mq_putcq_tqe;
	nni_taskq_ent		mq_getcq_tqe;

	int			mq_notify_sig;
	nni_cv			mq_notify_cv;
	nni_thr			mq_notify_thr;
	nni_msgq_notify_fn	mq_notify_fn;
	void *			mq_notify_arg;
};


// nni_msgq_notifier thread runs if events callbacks are registered on the
// message queue, and calls the notification callbacks outside of the
// lock.  It looks at the actual msgq state to trigger the right events.
static void
nni_msgq_notifier(void *arg)
{
	nni_msgq *mq = arg;
	int sig;
	nni_msgq_notify_fn fn;
	void *fnarg;

	for (;;) {
		nni_mtx_lock(&mq->mq_lock);
		while ((mq->mq_notify_sig == 0) && (!mq->mq_closed)) {
			nni_cv_wait(&mq->mq_notify_cv);
		}
		if (mq->mq_closed) {
			nni_mtx_unlock(&mq->mq_lock);
			break;
		}

		sig = mq->mq_notify_sig;
		mq->mq_notify_sig = 0;

		fn = mq->mq_notify_fn;
		fnarg = mq->mq_notify_arg;
		nni_mtx_unlock(&mq->mq_lock);

		if (fn != NULL) {
			fn(mq, sig, fnarg);
		}
	}
}


// nni_msgq_kick kicks the msgq notification thread.  It should be called
// with the lock held.
static void
nni_msgq_kick(nni_msgq *mq, int sig)
{
	if (mq->mq_notify_fn != NULL) {
		mq->mq_notify_sig |= sig;
		nni_cv_wake(&mq->mq_notify_cv);
	}
}


int
nni_msgq_init(nni_msgq **mqp, int cap)
{
	struct nni_msgq *mq;
	int rv;
	int alloc;

	if (cap < 0) {
		return (NNG_EINVAL);
	}

	// We allocate 2 extra cells in the fifo.  One to accommodate a
	// waiting writer when cap == 0. (We can "briefly" move the message
	// through.)  This lets us behave the same as unbuffered Go channels.
	// The second cell is to permit pushback later, e.g. for REQ to stash
	// a message back at the end to do a retry.
	alloc = cap + 2;

	if ((mq = NNI_ALLOC_STRUCT(mq)) == NULL) {
		return (NNG_ENOMEM);
	}

	if ((rv = nni_mtx_init(&mq->mq_lock)) != 0) {
		goto fail;
	}
	if (((rv = nni_cv_init(&mq->mq_readable, &mq->mq_lock)) != 0) ||
	    ((rv = nni_cv_init(&mq->mq_writeable, &mq->mq_lock)) != 0) ||
	    ((rv = nni_cv_init(&mq->mq_drained, &mq->mq_lock)) != 0) ||
	    ((rv = nni_cv_init(&mq->mq_notify_cv, &mq->mq_lock)) != 0)) {
		goto fail;
	}
	if ((mq->mq_msgs = nni_alloc(sizeof (nng_msg *) * alloc)) == NULL) {
		rv = NNG_ENOMEM;
		goto fail;
	}

	mq->mq_cap = cap;
	mq->mq_alloc = alloc;
	mq->mq_len = 0;
	mq->mq_get = 0;
	mq->mq_put = 0;
	mq->mq_closed = 0;
	mq->mq_puterr = 0;
	mq->mq_geterr = 0;
	mq->mq_wwait = 0;
	mq->mq_rwait = 0;
	mq->mq_notify_fn = NULL;
	mq->mq_notify_arg = NULL;
	*mqp = mq;

	return (0);

fail:
	nni_cv_fini(&mq->mq_drained);
	nni_cv_fini(&mq->mq_writeable);
	nni_cv_fini(&mq->mq_readable);
	nni_mtx_fini(&mq->mq_lock);
	if (mq->mq_msgs != NULL) {
		nni_free(mq->mq_msgs, sizeof (nng_msg *) * alloc);
	}
	NNI_FREE_STRUCT(mq);
	return (rv);
}


void
nni_msgq_fini(nni_msgq *mq)
{
	nni_msg *msg;

	if (mq == NULL) {
		return;
	}
	nni_thr_fini(&mq->mq_notify_thr);
	nni_cv_fini(&mq->mq_drained);
	nni_cv_fini(&mq->mq_writeable);
	nni_cv_fini(&mq->mq_readable);
	nni_mtx_fini(&mq->mq_lock);

	/* Free any orphaned messages. */
	while (mq->mq_len > 0) {
		msg = mq->mq_msgs[mq->mq_get];
		mq->mq_get++;
		if (mq->mq_get > mq->mq_alloc) {
			mq->mq_get = 0;
		}
		mq->mq_len--;
		nni_msg_free(msg);
	}

	nni_free(mq->mq_msgs, mq->mq_alloc * sizeof (nng_msg *));
	NNI_FREE_STRUCT(mq);
}


int
nni_msgq_notify(nni_msgq *mq, nni_msgq_notify_fn fn, void *arg)
{
	int rv;

	nni_thr_fini(&mq->mq_notify_thr);

	nni_mtx_lock(&mq->mq_lock);
	rv = nni_thr_init(&mq->mq_notify_thr, nni_msgq_notifier, mq);
	if (rv != 0) {
		nni_mtx_unlock(&mq->mq_lock);
		return (rv);
	}
	mq->mq_notify_fn = fn;
	mq->mq_notify_arg = arg;
	nni_thr_run(&mq->mq_notify_thr);
	nni_mtx_unlock(&mq->mq_lock);
	return (0);
}


void
nni_msgq_set_put_error(nni_msgq *mq, int error)
{
	nni_mtx_lock(&mq->mq_lock);
	mq->mq_puterr = error;
	if (error) {
		mq->mq_wwait = 0;
		nni_cv_wake(&mq->mq_writeable);
	}
	nni_mtx_unlock(&mq->mq_lock);
}


void
nni_msgq_set_get_error(nni_msgq *mq, int error)
{
	nni_mtx_lock(&mq->mq_lock);
	mq->mq_geterr = error;
	if (error) {
		mq->mq_rwait = 0;
		nni_cv_wake(&mq->mq_readable);
	}
	nni_mtx_unlock(&mq->mq_lock);
}


void
nni_msgq_set_error(nni_msgq *mq, int error)
{
	nni_mtx_lock(&mq->mq_lock);
	mq->mq_geterr = error;
	mq->mq_puterr = error;
	if (error) {
		mq->mq_rwait = 0;
		mq->mq_wwait = 0;
		nni_cv_wake(&mq->mq_readable);
		nni_cv_wake(&mq->mq_writeable);
	}
	nni_mtx_unlock(&mq->mq_lock);
}


// nni_msgq_signal raises a signal on the signal object. This allows a
// waiter to be signaled, so that it can be woken e.g. due to a pipe closing.
// Note that the signal object must be *zero* if no signal is raised.
void
nni_msgq_signal(nni_msgq *mq, int *signal)
{
	nni_mtx_lock(&mq->mq_lock);
	*signal = 1;

	// We have to wake everyone.
	mq->mq_rwait = 0;
	mq->mq_wwait = 0;
	nni_cv_wake(&mq->mq_readable);
	nni_cv_wake(&mq->mq_writeable);
	nni_cv_wake(&mq->mq_notify_cv);
	nni_mtx_unlock(&mq->mq_lock);
}


int
nni_msgq_put_(nni_msgq *mq, nni_msg *msg, nni_time expire, nni_signal *sig)
{
	int rv;

	nni_mtx_lock(&mq->mq_lock);

	for (;;) {
		// if closed, we don't put more... this check is first!
		if (mq->mq_closed) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_ECLOSED);
		}

		if ((rv = mq->mq_puterr) != 0) {
			nni_mtx_unlock(&mq->mq_lock);
			return (rv);
		}

		// room in the queue?
		if (mq->mq_len < mq->mq_cap) {
			break;
		}

		// unbuffered, room for one, and a reader waiting?
		if (mq->mq_rwait &&
		    (mq->mq_cap == 0) &&
		    (mq->mq_len == mq->mq_cap)) {
			break;
		}

		// interrupted?
		if (*sig) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_EINTR);
		}

		// single poll?
		if (expire == NNI_TIME_ZERO) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_EAGAIN);
		}

		// waiting....
		mq->mq_wwait = 1;

		// if we are unbuffered, kick the notifier, because we're
		// writable.
		if (mq->mq_cap == 0) {
			nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANGET);
		}

		// not writeable, so wait until something changes
		rv = nni_cv_until(&mq->mq_writeable, expire);
		if (rv == NNG_ETIMEDOUT) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_ETIMEDOUT);
		}
	}

	// Writeable!  Yay!!
	mq->mq_msgs[mq->mq_put] = msg;
	mq->mq_put++;
	if (mq->mq_put == mq->mq_alloc) {
		mq->mq_put = 0;
	}
	mq->mq_len++;
	if (mq->mq_rwait) {
		mq->mq_rwait = 0;
		nni_cv_wake(&mq->mq_readable);
	}
	if (mq->mq_len < mq->mq_cap) {
		nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANPUT);
	}
	nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANGET);
	nni_mtx_unlock(&mq->mq_lock);

	return (0);
}


// nni_msgq_putback will attempt to put a single message back
// to the head of the queue.  It never blocks.  Message queues always
// have room for at least one putback.
int
nni_msgq_putback(nni_msgq *mq, nni_msg *msg)
{
	nni_mtx_lock(&mq->mq_lock);

	// if closed, we don't put more... this check is first!
	if (mq->mq_closed) {
		nni_mtx_unlock(&mq->mq_lock);
		return (NNG_ECLOSED);
	}

	// room in the queue?
	if (mq->mq_len >= mq->mq_cap) {
		nni_mtx_unlock(&mq->mq_lock);
		return (NNG_EAGAIN);
	}

	// Subtract one from the get index, possibly wrapping.
	mq->mq_get--;
	if (mq->mq_get == 0) {
		mq->mq_get = mq->mq_cap;
	}
	mq->mq_msgs[mq->mq_get] = msg;
	mq->mq_len++;
	if (mq->mq_rwait) {
		mq->mq_rwait = 0;
		nni_cv_wake(&mq->mq_readable);
	}

	nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANGET);
	nni_mtx_unlock(&mq->mq_lock);

	return (0);
}


static int
nni_msgq_get_(nni_msgq *mq, nni_msg **msgp, nni_time expire, nni_signal *sig)
{
	int rv;
	nni_list cq;

	nni_mtx_lock(&mq->mq_lock);

	for (;;) {
		// always prefer to deliver data if its there
		if (mq->mq_len != 0) {
			break;
		}
		if (mq->mq_closed) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_ECLOSED);
		}
		if ((rv = mq->mq_geterr) != 0) {
			nni_mtx_unlock(&mq->mq_lock);
			return (rv);
		}
		if (expire == NNI_TIME_ZERO) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_EAGAIN);
		}
		if (*sig) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_EINTR);
		}
		if ((mq->mq_cap == 0) && (mq->mq_wwait)) {
			// let a write waiter know we are ready
			mq->mq_wwait = 0;
			nni_cv_wake(&mq->mq_writeable);
		}
		mq->mq_rwait = 1;

		if (mq->mq_cap == 0) {
			// If unbuffered, kick it since a writer would not
			// block.
			nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANPUT);
		}

		rv = nni_cv_until(&mq->mq_readable, expire);
		if (rv == NNG_ETIMEDOUT) {
			nni_mtx_unlock(&mq->mq_lock);
			return (NNG_ETIMEDOUT);
		}
	}

	// Readable!  Yay!!

	*msgp = mq->mq_msgs[mq->mq_get];
	mq->mq_len--;
	mq->mq_get++;
	if (mq->mq_get == mq->mq_alloc) {
		mq->mq_get = 0;
	}
	if (mq->mq_wwait) {
		mq->mq_wwait = 0;
		nni_cv_wake(&mq->mq_writeable);
	}
	if (mq->mq_len) {
		nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANGET);
	}
	nni_msgq_kick(mq, NNI_MSGQ_NOTIFY_CANPUT);
	nni_mtx_unlock(&mq->mq_lock);

	return (0);
}


int
nni_msgq_get(nni_msgq *mq, nni_msg **msgp)
{
	nni_signal nosig = 0;

	return (nni_msgq_get_(mq, msgp, NNI_TIME_NEVER, &nosig));
}


int
nni_msgq_get_sig(nni_msgq *mq, nni_msg **msgp, nni_signal *signal)
{
	return (nni_msgq_get_(mq, msgp, NNI_TIME_NEVER, signal));
}


int
nni_msgq_get_until(nni_msgq *mq, nni_msg **msgp, nni_time expire)
{
	nni_signal nosig = 0;

	return (nni_msgq_get_(mq, msgp, expire, &nosig));
}


int
nni_msgq_put(nni_msgq *mq, nni_msg *msg)
{
	nni_signal nosig = 0;

	return (nni_msgq_put_(mq, msg, NNI_TIME_NEVER, &nosig));
}


int
nni_msgq_tryput(nni_msgq *mq, nni_msg *msg)
{
	nni_signal nosig = 0;

	return (nni_msgq_put_(mq, msg, NNI_TIME_ZERO, &nosig));
}


int
nni_msgq_put_sig(nni_msgq *mq, nni_msg *msg, nni_signal *signal)
{
	return (nni_msgq_put_(mq, msg, NNI_TIME_NEVER, signal));
}


int
nni_msgq_put_until(nni_msgq *mq, nni_msg *msg, nni_time expire)
{
	nni_signal nosig = 0;

	return (nni_msgq_put_(mq, msg, expire, &nosig));
}


void
nni_msgq_drain(nni_msgq *mq, nni_time expire)
{
	nni_mtx_lock(&mq->mq_lock);
	mq->mq_closed = 1;
	mq->mq_wwait = 0;
	mq->mq_rwait = 0;
	nni_cv_wake(&mq->mq_writeable);
	nni_cv_wake(&mq->mq_readable);
	nni_cv_wake(&mq->mq_notify_cv);
	while (mq->mq_len > 0) {
		if (nni_cv_until(&mq->mq_drained, expire) != 0) {
			break;
		}
	}
	// If we timedout, free any remaining messages in the queue.
	while (mq->mq_len > 0) {
		nni_msg *msg = mq->mq_msgs[mq->mq_get++];
		if (mq->mq_get > mq->mq_alloc) {
			mq->mq_get = 0;
		}
		mq->mq_len--;
		nni_msg_free(msg);
	}
	nni_mtx_unlock(&mq->mq_lock);
}


void
nni_msgq_close(nni_msgq *mq)
{
	nni_mtx_lock(&mq->mq_lock);
	mq->mq_closed = 1;
	mq->mq_wwait = 0;
	mq->mq_rwait = 0;
	nni_cv_wake(&mq->mq_writeable);
	nni_cv_wake(&mq->mq_readable);
	nni_cv_wake(&mq->mq_notify_cv);

	// Free the messages orphaned in the queue.
	while (mq->mq_len > 0) {
		nni_msg *msg = mq->mq_msgs[mq->mq_get++];
		if (mq->mq_get > mq->mq_alloc) {
			mq->mq_get = 0;
		}
		mq->mq_len--;
		nni_msg_free(msg);
	}
	nni_mtx_unlock(&mq->mq_lock);
}


int
nni_msgq_len(nni_msgq *mq)
{
	int rv;

	nni_mtx_lock(&mq->mq_lock);
	rv = mq->mq_len;
	nni_mtx_unlock(&mq->mq_lock);
	return (rv);
}


int
nni_msgq_cap(nni_msgq *mq)
{
	int rv;

	nni_mtx_lock(&mq->mq_lock);
	rv = mq->mq_cap;
	nni_mtx_unlock(&mq->mq_lock);
	return (rv);
}


int
nni_msgq_resize(nni_msgq *mq, int cap)
{
	int alloc;
	nni_msg *msg;
	nni_msg **newq, **oldq;
	int oldget;
	int oldput;
	int oldcap;
	int oldlen;
	int oldalloc;

	alloc = cap + 2;

	if (alloc > mq->mq_alloc) {
		newq = nni_alloc(sizeof (nni_msg *) * alloc);
		if (newq == NULL) {
			return (NNG_ENOMEM);
		}
	} else {
		newq = NULL;
	}

	nni_mtx_lock(&mq->mq_lock);
	while (mq->mq_len > (cap + 1)) {
		// too many messages -- we allow that one for
		// the case of pushback or cap == 0.
		// we delete the oldest messages first
		msg = mq->mq_msgs[mq->mq_get++];
		if (mq->mq_get > mq->mq_alloc) {
			mq->mq_get = 0;
		}
		mq->mq_len--;
		nni_msg_free(msg);
	}
	if (newq == NULL) {
		// Just shrinking the queue, no changes
		mq->mq_cap = cap;
		goto out;
	}

	oldq = mq->mq_msgs;
	oldget = mq->mq_get;
	oldput = mq->mq_put;
	oldcap = mq->mq_cap;
	oldalloc = mq->mq_alloc;
	oldlen = mq->mq_len;

	mq->mq_msgs = newq;
	mq->mq_len = mq->mq_get = mq->mq_put = 0;
	mq->mq_cap = cap;
	mq->mq_alloc = alloc;

	while (oldlen) {
		mq->mq_msgs[mq->mq_put++] = oldq[oldget++];
		if (oldget == oldalloc) {
			oldget = 0;
		}
		if (mq->mq_put == mq->mq_alloc) {
			mq->mq_put = 0;
		}
		mq->mq_len++;
		oldlen--;
	}
	nni_free(oldq, sizeof (nni_msg *) * oldalloc);

out:
	// Wake everyone up -- we changed everything.
	nni_cv_wake(&mq->mq_readable);
	nni_cv_wake(&mq->mq_writeable);
	nni_cv_wake(&mq->mq_drained);
	nni_mtx_unlock(&mq->mq_lock);
	return (0);
}
