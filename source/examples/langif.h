#ifndef LANGIF_H
#define LANGIF_H


/* proposed logging levels */
#define  LOG_NOTSET   0
#define  LOG_DEBUG    10   /* printf-style debugging */
#define  LOG_INFO     20   /* what is being communicated, identifiers, etc. */
#define  LOG_WARN     30   /* unexpected conditions, can resume */
#define  LOG_ERROR    40   /* unexpected condition, will terminate computation prematurely */
#define  LOG_FATAL    50   /* unexpected condition, behavior undefined */

/* we use "unsigned long" here although we might use a larger/smaller
   type in the implementation. */
typedef uintptr_t fieldref_t;
typedef fieldref_t outref_t;
typedef fieldref_t claimref_t;

/* we use "unsigned long" here although we might use a larger/smaller
   type in the implementation. */
typedef uintptr_t typeid_t;


/* the "dispatch_cb" structure used by box/control entities. */

typedef dispatch_t {
    dispatch_t_api *api;
    /* invisible additional fields here
       to identify the calling task, its private state, etc. */
} dispatch_t;

dispatch_t_api {
    /* general input functions */
    void        (*bind) (dispatch_t*, ...);
    claimref_t* (*claim)(dispatch_t*, fieldref_t *r);

    /* general output functions */
    int       (*out)  (dispatch_t*, ...);
    int       (*outv) (dispatch_t*, int fmt, ...);
    void      (*log)  (dispatch_t*, int loglevel, const char *fmt, ...);
    outref_t  (*demit)(dispatch_t*, fieldref_t r);
   
    /* Common EMA/LMA functions */
    int         (*access) (dispatch_t*, fieldref_t theref, void **ptr)
    int         (*getmd)  (dispatch_t*, fieldref_t theref, size_t *thesize, typeid_t *thetype, size_t *realsize);
    void        (*release)(dispatch_t*, fieldref_t theref);
    fieldref_t  (*clone)  (dispatch_t*, fieldref_t r);

    fieldref_t  (*copyref)(dispatch_t*, fieldref_t r);
 
    /* EMA functions */
    fieldref_t  (*new)    (dispatch_t*, size_t thesize, typeid_t thetype);
    int         (*resize) (dispatch_t*, fieldref_t theref, size_t newsize);

    /* LMA functions */
    fieldref_t  (*wrap)   (dispatch_t*, typeid_t thetype, void* data);
    fieldref_t  (*capture)(dispatch_t*, typeid_t thetype, void* data);
    void*       (*unwrap) (dispatch_t*, fieldref_t theref);
    void*       (*unwrap_release) (dispatch_t*, fieldref_t theref);
};

/* wrapper macros to simplify usage of the above */

#define svp_bind(x, ...)         x->api->bind(x, __VA_ARGS__)

#define svp_out(x, ...)          x->api->out(x, __VA_ARGS__)
#define svp_log(x, y, z, ...)    x->api->log(x, y, z, __VA_ARGS__)

#define svp_access(x, y, z)      x->api->access(x, y, z)
#define svp_getmd(w, x, y, z)    x->api->getmd(w, x, y, z)
#define svp_release(x, y)        x->api->release(x, y)
#define svp_clone(x, y)          x->api->clone(x, y)
#define svp_copyref(x, y)        x->api->copyref(x, y)

#define svp_new(x, y, z)         x->api->new(x, y, z)
#define svp_resize(x, y, z)      x->api->resize(x, y, z)

#define svp_wrap(w, x, y, z)     x->api->wrap(w, x, y, z)
#define svp_capture(w, x, y, z)  x->api->capture(w, x, y, z)
#define svp_unwrap(x, y)         x->api->unwrap(x, y)
#define svp_unwrap_release(x, y) x->api->unwrap_release(x, y)

/* for demit/claim we do some bit fiddling */
// demit: sets the MSB of the fieldref, out() will check it.
#define svp_demit(x, y)       ((y) | ((fieldref_t)1 << (sizeof(fieldref_t)*CHAR_BIT-1)))
// claim: sets the LSB of the fieldref*, bind() will check it.
// assumes fieldrefs are never placed at odd memory addresses.
#define svp_claim(x, y)       ((claimref_t*)((uintptr_t)(y) | 1))

#define svp_wrap_demit(x, y, z, t)    svp_demit(x, svp_wrap(x, y, z, t))
#define svp_capture_demit(x, y, z, t) svp_demit(x, svp_capture(x, y, z, t))


/*** optional backward compatibility with C4SNet ***/

typedef struct {/*unneeded*/} c4snet_data_t;

#define C4SNetOut svp_out

/* for the following, we achieve partial backward compatibility only:
 * we need the hnd as first argument for all the functions. */

#define C4SNetCreate(hnd, type, size, data) \
    ((c4snet_data_t*)(void*)svp_wrap(hnd, type, size, data))

#define C4SNetFree(hnd, ptr) \
    svp_release(hnd, (fieldref_t)(void*)(ptr))

static inline 
c4snet_data_t* C4SNetAlloc(dispatch_t* hnd, c4snet_type_t type, size_t size, void **data)
{
    fieldref_t r = svp_new(hnd, size, type);
    svp_access(hnd, r, dataptr);
    return (c4snet_data_t*)(void*)r;
}

static inline
size_t C4SNetSizeof(dispatch_t* hnd, c4snet_data_t* ptr)
{
    size_t v;
    svp_getmd(hnd, (fieldref_t)(void*)(ptr), &v, 0, 0);
    return v;
}

static inline
void* C4SNetGetData(dispatch_t* hnd, c4snet_data_t* ptr)
{
    void *v;
    svp_access(hnd, (fieldref_t)(void*)(ptr), &v);
    return v;
}
        

#endif
