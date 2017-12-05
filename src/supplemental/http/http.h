//
// Copyright 2017 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2017 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef NNG_SUPPLEMENTAL_HTTP_HTTP_H
#define NNG_SUPPLEMENTAL_HTTP_HTTP_H

// nni_http_msg represents an HTTP request or response message.
typedef struct nni_http_msg nni_http_msg;

typedef struct nni_http_tran {
	void *h_data;
	void (*h_read)(void *, nni_aio *);
	void (*h_write)(void *, nni_aio *);
	void (*h_close)(void *);
} nni_http_tran;

// nni_http_msg_init initializes an HTTP request.
extern int         nni_http_msg_init_req(nni_http_msg **);
extern int         nni_http_msg_init_res(nni_http_msg **);
extern void        nni_http_msg_fini(nni_http_msg *);
extern int         nni_http_msg_set_method(nni_http_msg *, const char *);
extern const char *nni_http_msg_get_method(nni_http_msg *);
extern int         nni_http_msg_set_version(nni_http_msg *, const char *);
extern const char *nni_http_msg_get_version(nni_http_msg *);
extern const char *nni_http_msg_get_uri(nni_http_msg *);
extern int         nni_http_msg_set_uri(nni_http_msg *, const char *);
extern const char *nni_http_msg_get_header(nni_http_msg *, const char *);
extern int         nni_http_msg_del_header(nni_http_msg *, const char *);
extern int         nni_http_msg_set_status(nni_http_msg *, int, const char *);
extern int         nni_http_msg_get_status(nni_http_msg *);
extern const char *nni_http_msg_get_reason(nni_http_msg *);

extern int nni_http_msg_set_header(nni_http_msg *, const char *, const char *);
extern int nni_http_msg_set_data(nni_http_msg *, const void *, size_t);
extern int nni_http_msg_copy_data(nni_http_msg *, const void *, size_t);
extern void nni_http_msg_get_data(nni_http_msg *, void **, size_t *);
extern int  nni_http_msg_parse(nni_http_msg *, char *, size_t, size_t *);
extern int  nni_http_msg_parse_data(nni_http_msg *, char *, size_t, size_t *);
extern int  nni_http_msg_get_buf(nni_http_msg *, void **, size_t *);

// HTTP status codes.  This list is not exhaustive.
enum { NNI_HTTP_STATUS_CONTINUE                  = 100,
	NNI_HTTP_STATUS_SWITCHING                = 101,
	NNI_HTTP_STATUS_PROCESSING               = 102,
	NNI_HTTP_STATUS_OK                       = 200,
	NNI_HTTP_STATUS_CREATED                  = 201,
	NNI_HTTP_STATUS_ACCEPTED                 = 202,
	NNI_HTTP_STATUS_NOT_AUTHORITATIVE        = 203,
	NNI_HTTP_STATUS_NO_CONTENT               = 204,
	NNI_HTTP_STATUS_RESET_CONTENT            = 205,
	NNI_HTTP_STATUS_PARTIAL_CONTENT          = 206,
	NNI_HTTP_STATUS_MULTI_STATUS             = 207,
	NNI_HTTP_STATUS_ALREADY_REPORTED         = 208,
	NNI_HTTP_STATUS_IM_USED                  = 226,
	NNI_HTTP_STATUS_MULTIPLE_CHOICES         = 300,
	NNI_HTTP_STATUS_STATUS_MOVED_PERMANENTLY = 301,
	NNI_HTTP_STATUS_FOUND                    = 302,
	NNI_HTTP_STATUS_SEE_OTHER                = 303,
	NNI_HTTP_STATUS_NOT_MODIFIED             = 304,
	NNI_HTTP_STATUS_USE_PROXY                = 305,
	NNI_HTTP_STATUS_TEMPORARY_REDIRECT       = 307,
	NNI_HTTP_STATUS_PERMANENT_REDIRECT       = 308,
	NNI_HTTP_STATUS_BAD_REQUEST              = 400,
	NNI_HTTP_STATUS_UNAUTHORIZED             = 401,
	NNI_HTTP_STATUS_PAYMENT_REQUIRED         = 402,
	NNI_HTTP_STATUS_FORBIDDEN                = 403,
	NNI_HTTP_STATUS_NOT_FOUND                = 404,
	NNI_HTTP_STATUS_PROXY_AUTH_REQUIRED      = 407,
	NNI_HTTP_STATUS_REQUEST_TIMEOUT          = 408,
	NNI_HTTP_STATUS_CONFLICT                 = 409,
	NNI_HTTP_STATUS_GONE                     = 410,
	NNI_HTTP_STATUS_LENGTH_REQUIRED          = 411,
	NNI_HTTP_STATUS_PRECONDITION_FAILED      = 412,
	NNI_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE = 413,
	NNI_HTTP_STATUS_REQUEST_URI_TOO_LONG     = 414,
	NNI_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE   = 415,
	NNI_HTTP_STATUS_RANGE_NOT_SATISFIABLE    = 416,
	NNI_HTTP_STATUS_EXPECTATION_FAILED       = 417,
	NNI_HTTP_STATUS_TEAPOT                   = 418,
	NNI_HTTP_STATUS_UNPROCESSABLE_ENTITY     = 422,
	NNI_HTTP_STATUS_LOCKED                   = 423,
	NNI_HTTP_STATUS_FAILED_DEPENDENCY        = 424,
	NNI_HTTP_STATUS_UPGRADE_REQUIRED         = 426,
	NNI_HTTP_STATUS_PRECONDITION_REQUIRED    = 428,
	NNI_HTTP_STATUS_TOO_MANY_REQUESTS        = 429,
	NNI_HTTP_STATUS_HEADERS_TOO_LARGE        = 431,
	NNI_HTTP_STATUS_UNAVAIL_LEGAL_REASONS    = 451,
	NNI_HTTP_STATUS_INTERNAL_SERVER_ERROR    = 500,
	NNI_HTTP_STATUS_NOT_IMPLEMENTED          = 501,
	NNI_HTTP_STATUS_BAD_GATEWAY              = 502,
	NNI_HTTP_STATUS_SERVICE_UNAVAILABLE      = 503,
	NNI_HTTP_STATUS_GATEWAY_TIMEOUT          = 504,
	NNI_HTTP_STATUS_HTTP_VERSION_NOT_SUPP    = 505,
	NNI_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES  = 506,
	NNI_HTTP_STATUS_INSUFFICIENT_STORAGE     = 507,
	NNI_HTTP_STATUS_LOOP_DETECTED            = 508,
	NNI_HTTP_STATUS_NOT_EXTENDED             = 510,
	NNI_HTTP_STATUS_NETWORK_AUTH_REQUIRED    = 511,
};

// An HTTP connection is a connection over which messages are exchanged.
// Generally, clients send request messages, and then read responses.
// Servers, read requests, and write responses.  However, we do not
// require a 1:1 mapping between request and response here -- the application
// is responsible for dealing with that.
//
// We only support HTTP/1.1, though using the nni_http_conn_read and
// nni_http_conn_write low level methods, it is possible to write an upgrader
// (such as websocket!) that might support e.g. HTTP/2 or reading data that
// follows a legacy HTTP/1.0 message.
//
// Any error on the connection, including cancellation of a request, is fatal
// the connection.
typedef struct nni_http nni_http;

extern int  nni_http_init(nni_http **, nni_http_tran *);
extern void nni_http_close(nni_http *);
extern void nni_http_fini(nni_http *);

// Reading messages -- the caller must supply a preinitialized (but otherwise
// idle) message.  We recommend the caller store this in the aio's user data.
// Note that the iovs of the aio's are clobbered by these methods -- callers
// must not use them for any other purpose.
extern void nni_http_read_msg(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_read_data(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_read_msg_data(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_write_msg(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_write_data(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_write_msg_data(nni_http *, nni_http_msg *, nni_aio *);
extern void nni_http_read(nni_http *, nni_aio *);
extern void nni_http_write(nni_http *, nni_aio *);

#endif // NNG_SUPPLEMENTAL_HTTP_HTTP_H