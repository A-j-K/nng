/*
 * Copyright 2016 Garrett D'Amore <garrett@damore.org>
 *
 * This software is supplied under the terms of the MIT License, a
 * copy of which should be located in the distribution where this
 * file was obtained (LICENSE.txt).  A copy of the license may also be
 * found online at https://opensource.org/licenses/MIT.
 */

#ifndef CORE_LIST_H
#define CORE_LIST_H

#include "core/nng_impl.h"

/*
 * In order to make life easy, we just define the list structures
 * directly, and let consumers directly inline structures.
 */
typedef struct nni_list_node {
	struct nni_list_node *	ln_next;
	struct nni_list_node *	ln_prev;
} nni_list_node_t;

typedef struct nni_list {
	struct nni_list_node	ll_head;
	size_t			ll_offset;
} nni_list_t;

extern void nni_list_init_offset(nni_list_t *list, size_t offset);

#define NNI_LIST_INIT(list, type, field) \
	nni_list_init_offset(list, offsetof(type, field))
extern void *nni_list_first(nni_list_t *);
extern void *nni_list_last(nni_list_t *);
extern void nni_list_append(nni_list_t *, void *);
extern void nni_list_prepend(nni_list_t *, void *);
extern void *nni_list_next(nni_list_t *, void *);
extern void *nni_list_prev(nni_list_t *, void *);
extern void nni_list_remove(nni_list_t *, void *);
extern void nni_list_node_init(nni_list_t *, void *);

#define NNI_LIST_FOREACH(l, it)	\
	for (it = nni_list_first(l); it != NULL; it = nni_list_next(l, it))

#endif  /* CORE_LIST_H */
