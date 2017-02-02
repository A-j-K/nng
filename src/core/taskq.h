//
// Copyright 2017 Garrett D'Amore <garrett@damore.org>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_TASKQ_H
#define CORE_TASKQ_H

#include "core/nng_impl.h"

struct nni_taskq_ent {
	nni_list_node	tqe_node;
	void *		tqe_arg;
	nni_cb		tqe_cb;
	nni_taskq *	tqe_tq;
	int		tqe_flags;
};

#define NNI_TQE_SCHED		1
#define NNI_TQE_RUN		2
#define NNI_TQE_CANCEL		4

extern int nni_taskq_init(nni_taskq **, int);
extern void nni_taskq_fini(nni_taskq *);

extern int nni_taskq_dispatch(nni_taskq *, nni_taskq_ent *);
extern int nni_taskq_cancel(nni_taskq_ent *);
extern void nni_taskq_ent_init(nni_taskq_ent *, nni_cb, void *);

#endif // CORE_TASKQ_H
