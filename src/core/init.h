//
// Copyright 2016 Garrett D'Amore <garrett@damore.org>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_INIT_H
#define CORE_INIT_H

#include "core/nng_impl.h"

// nni_init is called each time the user enters the library.  It ensures that
// the library is initlialized properly, and also deals with checks such as
// whether the process has forked since last initialization.
int nni_init(void);

// nni_fini tears everything down.  In the future it may be used to ensure
// that all resources used by the library are released back to the system.
void nni_fini(void);

// Private hash tables matching IDs to values.  Consumers need to use the
// nni_idlock to protect access to these.
extern nni_mtx *nni_idlock;
extern nni_idhash *nni_endpoints;
extern nni_idhash *nni_pipes;
extern nni_idhash *nni_sockets;
extern nni_taskq *nni_main_taskq;

#endif // CORE_INIT_H
