=============================
 Language interface clean-up
=============================

:Authors: kena, merijn, frank
:Date: May 2012

:Abstract: This note proposes a new API to manage field data,
   especially from SNet box code, and also from control entities. It
   integrates the "sane" reference semantics proposed by Merijn on the
   snet-dev mailing list on March 15th 2012 and discussed subsequently
   in technical meetings. It also ensures that all APIs can be
   overridden at run-time. The main changes are 1) clear semantics for
   ownership 2) mandatory passing of the execution context as 1st
   argument to all API calls, not only the SNetOut function.

.. sectnum::

.. contents::

Introduction
============

Objectives
----------

We want to:

1. support both "internally managed" objects (by box language)
   and "externally managed" objects (by the environment).

  - external management by the environment can be "better" because it
    can allow nice tricks like allocating storage physically closer to
    the eventual destination of an object. We learned that while
    working on the SCC.

  - however we acknowledge that some box language RTSs may have their
    own smart allocation and reference counting semantics and we thus
    want to allow delegating to them the management.

  In both cases, fields have metadata and management structures, outside
  of the box languages, that need to be dynamically allocated and
  tracked as well.

2. track storage allocated for fields and introspect this data outside
   of box code.

3. support multiple box languages.

4. in each language we want to support different allocation / destruction
   policies for different data types.

Summary of proposal
-------------------

So we introduce the following abstractions:

- a common *field database*, which registers the concrete allocated
  items of data and their reference counters.

- a unique *field reference* type which refers to an entry in the
  field database, with accessors for the actual data.

- Each component in the entire system receives *callbacks* to
  these "management utilities" as arguments when invoked. This has
  the following advantages:

  - we avoid polluting the global namespace with fixed-name functions 

  - we avoid global variables and the question of "who manages what":
    if a component does not receive a pointer to something, that means
    clearly "it does not need to know".

  - we avoid any future linking headache, and we open the
    opportunities to choose different implementations for things using
    dynamic linking.

- a common *concrete type database*, which understands the concrete
  (implementation) types and their various language-specific data
  handlers. The various box language run-times register their types to
  this. It also knows about a few byte-based data types.

  The concrete type database registers the
  serialization/deserialization, allocation/deallocation functions for
  the individual concrete types. 

- two different APIs to manage field data within box code:
 
  - one that delegates data management to the environment, called
    "environment-managed API" (EMA)

  - one that keeps control of data management to the box language,
    called "language-managed API" (LMA).

  Which API should be used depends on the concrete field type used. If the
  wrong API is used for a given concrete type (eg the EMA is used with
  the SAC array type, which should use LMA instead), an error would be reported.

  The contracts for object ownership differs between these two APIs as
  documented below.


Common interface visible from box code
======================================

Box functions as previously receive as first argument a "handle"
followed by the actual field arguments. The handle must be passed to
all the APIs.

Logging
-------

We introduce the following declaration in ``langif.h``:

.. code:: c

   typedef ... dispatcher_t;

   // proposed logging levels
   #define  LOG_NOTSET   0
   #define  LOG_DEBUG    10   // printf-style debugging
   #define  LOG_INFO     20   // what is being communicated, identifiers, etc
   #define  LOG_WARN     30   // unexpected conditions, can resume
   #define  LOG_ERROR    40   // unexpected condition, will terminate computation prematurely
   #define  LOG_FATAL    50   // unexpected condition, behavior undefined
 
   /* API accessors */
   int svp_log(dispatcher_t*, int, const char*, ...);

The logging function reports messages on a logging channel specific to
the context where it is called (identified via ``dispatcher_t``).

For example, a box function with type ``{<a>} -> {<x>,<y>,<z>}`` could
be written so:

.. code:: c

   /* box signature:  {<a>} -> {} */
   int testbox(dispatcher_t* cb,  tagval_t a)
   {
      svp_log(cb, LOG_INFO, "textbox received %d", a);

      return 0;      
   }
 
The box function can return non-zero to indicate an error has
occurred.

Field output
------------

We introduce the following:

.. code:: c

   // out: emit one output record.
   int svp_out(dispatcher_t* hnd, ...);

Each call to the output function produces one record on the default
output stream associated with the context (identified via ``dispatcher_t``).
   
For example, a box function with type ``{<a>} -> {<x>,<y>,<z>}``
could be written so:

.. code:: c

   int testbox(dispatcher_t* cb,  tagval_t a)
   {
      svp_log(cb, LOG_INFO, "textbox received %d", a);
      
      // this box' behavior is to forward 3 copies of the tag
      // and then its value +5, in two separate records.
 
      return svp_out(cb, a, a, a) &&
             svp_out(cb, a+5, a+5, a+5);
   }
 

Field input
-----------

We introduce the following in ``langif.h``:

.. code:: c

   // fieldref_t is an opaque integer which names a field data item.
   // The special value 0 means "null reference".
   typedef ... fieldref_t;
 
   // typeid_t is an opaque integer which names a concrete data type.
   typedef ... typeid_t;
 
   // access: retrieve a pointer to the actual data.
   int svp_access(dispatcher_t*, fieldref_t theref, void **ptr);

   // getmd: retrieve field metadata.
   int svp_getmd (dispatcher_t*, fieldref_t theref, 
                  size_t *thesize, typeid_t *thetype, size_t *realsize);

Semantics:

``access`` : access the object's contents.

  If the reference is valid, ``access`` overwrites ``*theptr``
  with the pointer to the object's contents. If ``theptr`` is NULL,
  ``access`` just tests the number of references.

  ============= ==========================================
  Return value  Description
  ============= ==========================================
  0             The data is read-only (there is more than 
                one reference to it)
  1             The data is read-write (there is exactly
                one reference to it)
  -1            The reference is invalid.
  ============= ==========================================
  

``getmd`` : retrieve object metadata.

  ``getmd`` overwrites the variables provided by non-NULL address as
  argument by the corresponding fields metadata:

  - ``*thesize``: requested/logical size. 
 
  - ``*thetype``: concrete type identifier.

  - ``*moresize``: *actual* allocated size. The difference with
    ``*thesize`` is made because ``new`` in the EMA may have allocated
    more bytes than requested. This stays equal to ``thesize`` when
    using the LMA.

  Return value: as per ``access`` above.

For example:

.. code:: c

   /* box signature: {string} -> {<t>}  */
   int testfirstnul(dispatcher_t* cb, fieldref_t f)
   {
        /* the box tests tests if the first byte of its input
           field is not ASCI NUL (``'\0'``). If the input
           field contains no data, no output record is produced. */

        // retrieve and test the size
        size_t s;
        svp_getmd(cb, f, &s, 0, 0);
        if (s == 0)
        {
           svp_log(cb, LOG_INFO, "no data");
           return 0;
        }
        
        // get access to the data
        char *p;
        svp_access(cb, f, &p);

        // test and output        
        svp_out(cb, (tagval_t)(p[0] == '\0'));
  
        return 0;
   }

Field database
==============

The field database maps integer values of type ``fieldref_t`` to
managed objects in memory.

The field database is only visible from box/control entity code using
the two ``dispatcher_t``-based APIs, LMA and EMA, documented below. 

Another API will be described separately for monitoring/analysis code
which wants to tracks how many fields are currently allocated, who has
allocated what, and so on.

Common API
----------

We provide the following:

.. code:: c

   fieldref_t  svp_copyref(dispatcher_t* cb, fieldref_t r);
 
For each call to ``copyref``, ``release`` must be called on the
returned reference.

``copyref`` may return 0 to indicate an invalid reference was given as input.

If the concrete type indicates the LMA is used, ``copyref`` will
inform the language-specific reference management that a new reference
was created.

Concrete type database
======================

The concrete type database is invisible to box/control entity
code. However we give a few words here to clarify what is its role and
how it is possible to use language-specific data type with the
management API presented below.

The concrete type database introduces language-specific "type IDs"
which act as a key to find type-specific management functions:
allocation/deallocation, serialization, copy.

The box code only sees and gives ``typeid_t`` to the field management
API, discussed below. The field management API, in turn, only stores
``typeid_t`` values alongside the objects in the field database.  Only
for ``release`` with refcount 1, ``new``, ``clone``, the field
database then communicates with the concrete type database to delegate
the actual management of data.

So to give control of data to the field management database, each
language must *register* its object management API to the environment.

Common registration API
-----------------------

For this we provide the following API in ``datareg.h``:

.. code:: c

   typedef ... regctx_t;

   typedef ... datalangid_t;

   struct datamgr_cb {
     int     (*init)       (void** mgrctx);
     void    (*cleanup)    (void* mgrctx);

     size_t  (*getsersize) (void* mgrctx, typeid_t thetype, size_t objsize, const void *data);
     int     (*serialize)  (void* mgrctx, typeid_t thetype, size_t objsize, const void *data, 
                                          char* dstbuf, size_t bufsize);

     size_t  (*getdesersize)(void* mgrctx, typeid_t thetype, const char* srcbuf, size_t bufsize);
     int     (*deserialize) (void* mgrctx, typeid_t thetype, const char* srcbuf, size_t bufsize, 
                                           void **data, size_t *objsize);
   };

   // reg_datamgt: register a data manager.
   datalangid_t svp_reg_datamgr(regctx_t* reg, struct datamgr_cb* datamgr, const char *humanname);

When a language run-time is started up, it can obtain a pointer to a
``regctx_t``, which it can subsequently use to register itself and its
type management. 

``reg_datamgr`` does not take ownership of the ``datamgr_cb``
structure: the contents of the ``datamgr_cb`` are copied internally.

Example use during initialization:

.. code:: c

   void mylang_start_up(...., regctx_t* reg)
   {
       struct datamgr_cb mycb = {
            NULL, NULL, /* no init() nor clean-up() for this language */
            &mylang_getsersize,
            &mylang_serialize,
            NULL, /* no getdesersize() for this language */
            &mylang_deserialize
            };

       datalangid_t l = svp_reg_datamgr(reg, &mycb);        

       /* ... */
    }

Intended semantics of the common management functions
-----------------------------------------------------

- ``getsersize``: get a conservative estimate of the buffer size
  needed for serialization.

- ``serialize``: serialize the data. The output buffer is
  pre-allocated.

- ``getdesersize``: get a conservative estimate of the object size
  needed for deserialization.

- ``deserialize``: deserialize the data. The output object is 
  either pre-allocated (not-NULL) or not (NULL). The
  ``deserialize`` function can also drop the pre-allocated object
  and replace it with a new one.

The environment guarantees it will call ``init`` after system
initialization is complete but before the application starts up. After
``init`` is called and if ``init`` returns 0, the environment will pass the
value of ``mgrctx`` updated by ``init`` to all the other APIs, so
that they can carry state around. If ``init`` returns non-zero, an error
will be reported and the application will not be allowed to use that
language interface.

The other APIs (ser/deser) should assume they may be
called concurrently and perform their own mutual exclusion if needed.

The environment will also call ``cleanup`` after the application
terminates but before the system shuts down.

If ``getdesersize`` is not provided, the environment will provide a
NULL ``data`` pointer to ``deserialize``, which should then thus
allocate a fresh object.

The reason why serialize/getsersize and deserialize/getdesersize are
decoupled is that the environment may select different destinations
in memory for the object's data depending on where the data will be used.

If the concrete type is language-managed (LMA), then the ``objsize``
parameter to serialize/getsersize will be set to 0.


Environment-managed API (EMA)
=============================

Registration for EMA types during system initialization
-------------------------------------------------------

We extend ``datareg.h`` as follows:

.. code:: c

   void svp_reg_ema_type(regctx_t*, datalangid_t thelang,
                                    typeid_t thetype, const char *humanname,
                                    struct ema_typemgr_cb* tcb);
   };

   struct ema_typemgr_cb {
     void*   (*alloc)(void* langctx, typeid_t thetype, size_t thesize, size_t *realsize);
     void    (*free) (void* langctx, typeid_t thetype, size_t thesize, void* data);
     void*   (*copy) (void* langctx, typeid_t thetype, size_t thesize, void* data);
   };

Semantics:

- ``alloc``: allocate a new object of the specified type and size
  on the heap, return a pointer to it. Also update ``realsize`` with
  the size actually usable by the program. For example a program may
  require an allocation of 15 bytes and the minimum allocation size is
  32 bytes. Then ``realsize`` should be updated to 32.

- ``free``: release a previously allocated object. 

- ``clone``: make a copy of a previously allocated object. 

The type and size are both indicated to ``free`` / ``clone`` for
reference, in case the deallocator uses separate heaps for different
types/sizes.


Example use during initialization:

.. code:: c

   void mylang_start_up(... regctx_t* reg ...)
   {
       /* datalangid_t l = ... */

       struct ema_typemgr_cb mytcb = {
            &mylang_alloc,
            &mylang_dealloc
            };

       svp_reg_ema_typemgr(reg, l, 0, "myconcretetypeA", &mytcb);
       svp_reg_ema_typemgr(reg, l, 1, "myconcretetypeB", &mytcb);
       svp_reg_ema_typemgr(reg, l, 2, "myconcretetypeC", &mytcb);
    }

Predefined languages and types
``````````````````````````````

The special ``datalangid_t`` with value 0 is the "common data language",
which is the data language used by all entities which are not
configured to use another data language.

In the common data language the following concrete type ids are
predefined, to be used with the EMA:

- ``BYTES_UNALIGNED``: size unit is bytes, no alignment expected.

- ``BYTES_SCALAR_ALIGNED``: size unit is bytes, allocation is scalar
  aligned (aligned on uintmax_t or long long double, whichever is
  largest)

- ``BYTES_CACHE_ALIGNED``: size unit is bytes, allocation is scalar
  and cache line aligned.

- ``BYTES_PAGE_ALIGNED``: size unit is bytes, allocation page
  aligned.

All these types serialize and deserialize to themselves.

Discussion about field ownership
--------------------------------

There is a discussion about who is responsible for releasing
references manipulated by boxes, in case the EMA is used.

There are two questions that need answering:

1. who releases the field references that a box gets as input?

   Two options:
  
   a. the box itself, before it terminates.
   b. the environment, automatically after the box terminates.

2. who releases the field references that a box creates
   during its execution?

   Three options:

   a. the box itself, after it sends it via out().
   b. the out() function.
   c. the environment, automatically after the box terminates.

Analysis
````````

We analyze the different scenarios using the "traditional" implementation where
newly created references have count 1.

About 1a: yields memory leaks if the programmer forgets to call ``release``.

About 1b: yields a potential wasted opportunity in long-running boxes
with the following structure:

.. code:: c
  
   // signature: {bytes} -> {<x>, bytes}
   int examplebox(dispatcher_t* cb,  fieldref_t  x)
   {
      // this box outputs its input record with tag 0,
      // then 1000 fresh records with tag 1.
      svp_out(cb, 0, x);

      for (int i = 0; i < 1000; ++i)
      { 
           fieldref_t f = svp_new(cb, 1, BYTES_UNALIGNED);
           svp_out(cb, 1, f);
      }
      return 0;
   }

When this box runs, the memory for the input field ``x`` will remain
allocated for the entire duration of the box' execution, even though
it is not needed after the initial "out".


About 2a: yields memory leaks if the code forgets to call ``release``.

About 2b: creates a problem if a box wants to output multiple
references to the same field data. For example:

.. code:: c

   // signature: {bytes} -> {bytes}
   int examplebox(dispatcher_t* cb,  fieldref_t  x)
   {
      for (int i = 0; i < 1000; ++i)
           svp_out(cb, x);

      return 0;
   }

This code is invalid: if ``out`` calls ``release``, then after the
first iteration the reference ``x`` would not be valid any more.

About 2c: like 1b above, is inefficient when a long-running box
allocates many objects but only outputs each reference a few times. For example:

.. code:: c

   // signature: {<tag>} -> {bytes}
   int examplebox(dispatcher_t* cb,  tagval_t tag)
   {
      for (int i = 0; i < 1000; ++i)
      { 
           fieldref_t f = svp_new(cb, 1, BYTES_UNALIGNED);
           svp_out(cb, f);
      }
      return 0;
   }

In this box, it would be inefficient if the environment waits until
the end before it releases the allocated objects. Also it would create
a memory leak if the box is modified so that the loop never
terminates.

Solution
````````

We do this as follows:

- upon entry into a box function:

  - the field reference counter is *decreased without deallocation*
    immediately prior to entry; this may result in a ref count 0 but
    the data is not deallocated.

  - if the ref count becomes 0, the reference is also placed on a
    "clean up" list attached to the box activation.

  - when the box function terminates, the environment walks through
    the clean up list and simply frees the data associated with all
    the references on the list.

- each new reference creation by a box instance (eg. ``new``, ``wrap``
  in LMA) will set the reference count to 0 and cause the environment
  to store the reference to the newly allocated object in the "clean
  up" list.

- when ``copyref`` is called on a reference count with value 0, it
  increases the counter to 1 and it also removes the reference from
  its context's clean-up list.

- upon entry, ``out`` calls ``copyref``:

  - if the ref count was 0 upon entry, this takes ownership: the last
    subsequent ``release`` as part of ``out``'s continuation will
    deallocate the field data. After ``out`` returns, the reference is
    not valid any more.

  - if the ref count was 1 or greater upon entry, ``out`` does not
    take ownership: when ``out`` returns, the reference is still
    valid.

- if the box code explicitly calls ``release`` on a
  reference, ``release`` will do its work and
  also remove the reference from the clean up list.

Not that the maintained invariant is that a reference is on the clean
up list if and only if its reference counter becomes 0 but the data is
still allocated. 

We illustrate with two examples. The first creates 1000 different
fields:

.. code:: c

   // signature: {<tag>} -> {bytes}
   int examplebox(dispatcher_t* cb,  tagval_t tag)
   {
      for (int i = 0; i < 1000; ++i)
      { 
           fieldref_t f = svp_new(cb, 1, BYTES_UNALIGNED);
           svp_out(cb, f);
      }

      return 0; 
   }

At every iteration of the loop ``new`` creates a new reference with
count 0.  Then ``out`` increase the count to 1 and removes the
reference from the clean-up list. All the 1000 fields are deallocated
as part of ``out`` 's continuation; the references are not valid when
``out`` returns.

The second example outputs the same input field 1000 times:

.. code:: c

   // signature: {bytes} -> {bytes}
   int examplebox(dispatcher_t* cb,  fieldref_t  x)
   {
      x = svp_copyref(cb, x);

      for (int i = 0; i < 1000; ++i)
           svp_out(cb, x);

      svp_release(cb, x);

      return 0;
   }

Before the first iteration ``copyref`` increases the counter to 1 and
thus makes the box function "own" the reference. From that point the
reference becomes persistent across multiple calls to ``out``. However
because the box is now owner it must explicitly call release before
terminating.


.. hint:: Implementation detail.
 
   If a box takes two fields {A,B} as input, both must be separate
   entries in the clean up list because:
   
   - the box code must be able to call ``release`` explicitly on
     either

   - if the box code only calls ``release`` on one, the environment
     must only call ``release`` on the other.

   However in some cases the same object reference x will be passed as
   two separate inputs to a box function, for example with a box
   following a filter which creates two conceptual copies of the same
   field as separate fields in its output record.

   If the clean up list is a linked list using the objects themselves
   as nodes, a naive implementation would have a problem: there would
   be only one node on the list when the same object is listed as 2
   separate arguments of the box.

   So instead each linked list node also stores the number of times
   the object is listed in the input argument list, which is also
   the number of times ``release`` must be called on that node when
   the box ends.



Usage in box code
-----------------

For this purpose the ``dispatcher_t`` API is extended as follows:

.. code:: c

   // new: creates a fresh new object of the specified type and size.
   fieldref_t svp_new(dispatcher_t*, size_t thesize, typeid_t thetype);
 
   // release: drop the specified reference.
   void svp_release(dispatcher_t*, fieldref_t theref);
 
   // resize: modify the logical size of the object.
   int svp_resize(dispatcher_t*, fieldref_t theref, size_t newsize);
 
   // clone: create a fresh copy of the data with its own reference
   void svp_clone(dispatcher_t*, fieldref_t theref);
   
Semantics:

``new`` : allocate a fresh object.

  The size is the number of elements of the individual type provided.
  For example with special type "0" (non-aligned bytes) the size
  will specify the number of bytes to allocate.
 
  The actual available types depend on the `Concrete type database`_.

  ============= ==========================================
  Return value  Description
  ============= ==========================================
  >0            reference to the data item. At that point 
                the object is guaranteed writeable (only 
                one reference).
  0             (null reference) the allocation has 
                failed: not enough memory, type not 
                registered, etc.
  ============= ==========================================

``release`` : release the provided reference.

  The object will be deallocated if the provided reference was the
  last one.

``clone`` : copy the data.

  This allocates a new object with identical contents and returns the
  reference to the copy. 

  ``clone`` is implemented by calling ``copyref`` then ``release`` on
  the reference given as input. Although this seems idempotent and
  thus unnecessary, it has the interesting side effect to allow early
  deallocation in the following "common" code pattern:
 
  .. code:: c

     int boxfunc(dispatch_t* cb,  fieldref_t x)
     {
       void *ptr;
       int rw = svp_access(cb, x, &ptr);
       if (!rw) {
           // can't write, so make a copy.
           x = svp_clone(cb, x);
           svp_access(cb, x, &ptr);
       }
       /* ... use ptr here ... */
       svp_out(cb, x);
     }

  In this pattern, if ``x`` arrives in the box with reference count 0,
  then the ``copyref``/``release`` pair in ``clone`` ensures that its
  storage is de-allocated "early" during the call to ``clone``. 

  As with ``new`` and ``wrap``, ``clone`` initially sets the field
  reference counter to 0 and places the newly field reference in the
  context's clean-up list. See `Discussion about field ownership`_ for
  details.

``resize`` : modify the logical size.

  When ``new`` has allocated more bytes than requested, the extra
  bytes can be used to "shrink" or "expand" the object contained.
  Shrinking or expanding does not change the *actual* (physical)
  allocated size, returned via ``*realsize`` by ``getmd``. It does
  change ``thesize`` as returned by further calls to ``getmd``.

  Return value:

  ============= ==========================================
  Return value  Description
  ============= ==========================================
  0             Operation successful.
  1             Failed, data is read-only.
  -1            Possible cases:

                - reference invalid
                - the desired new size does not fit 
                  within the actual allocated size.
  ============= ==========================================
  
For example, we want to make a box "t2l" which takes one tag as input
and converts it to a C "long long". For this we can write the
following code in ``boxes.c``:

.. code:: c

   #include "langif.h"
   
   // signature: {<tag>} -> {ll}
   void t2l(dispatcher_t* cb,  tagval_t tag)
   {
       svp_log(cb, LOG_INFO, "hello from t2l, tag = %d", tag);
 
       // allocation by the "environment"
       fieldref_t f = svp_new(cb, sizeof(long long), BYTES_SCALAR_ALIGNED);
 
       // output the field reference
       svp_out(cb, f);

       return 0;
   }

We can make a box which forwards its entire input string as a new
record except for the first character which is capitalized:

.. code:: c

   #include "langif.h"

   // signature: {string} -> {string}
   int capitalize(dispatcher_t* cb,  fieldref_t  string)
   {
       char *str;
       int rw = svp_access(cb, string, &str);
       if (!rw) {
           // can't write, so make a copy.
           string = svp_clone(cb, string);
           svp_access(cb, string, &str);
       }

       // do the update. 
       str[0] = toupper(str[0]);

       svp_out(cb, string);

       return 0;
   }

Field access using the language-managed API (LMA)
=================================================

In this setting:

- the box language's RTS must register some reference management
  functions (make a new reference, drop a reference) upon system
  initialization; and

- in each box, direct pointers to the data allocated by the box
  language are passed around:
 
  - a box function receives directly a pointer to the data, not a
    field reference;

  - when a box function "sends" the data to ``out``, it must first
    *wrap* the data pointer in a field container to obtain a field
    reference.

  - the pointer passed to the box function transfers ownership of that
    reference. In particular, if the box consumes an input field 
    and does not forward it via the ``out`` function, it must 
    decrease its reference counter and check for deallocation.

Registration for LMA types during system initialization
-------------------------------------------------------

In ``datareg.h``:

.. code:: c

   void svp_reg_lma_typemgr(regctx_t*, datalangid_t thelang,
                                       typeid_t thetype, const char *humanname,
                                       struct lma_typemgr_cb* tcb);

   struct lma_typemgr_cb {

     // increment the reference counter.
     void    (*incref)   (void* langctx, typeid_t thetype, void* data);

     // decrement the reference counter, deallocate if reaches 0.
     // return 1 if it was the last reference, ie effective deallocation took place.
     int     (*decref)   (void* langctx, typeid_t thetype, void* data);

     // duplicate the object; new object has ref count 1
     void*   (*copy)     (void* langctx, typeid_t thetype, void* data);

     // test the reference counter.
     // return 1 if is the last reference, 0 if there are more references.
     void    (*testref)  (void* langctx, typeid_t thetype, void* data);

     // report an estimate of the size in memory taken by the item of data.
     // this is used for monitoring purposes.
     size_t  (*getsize)  (void* langctx, typeid_t thetype, void* data);

   };

``reg_lma_typemgr`` does not take ownership of the ``lma_typemgr_cb``
structure: the contents of the ``lma_typemgr_cb`` are copied internally.

Usage in box code
-----------------

For the LMA the ``dispatcher_t`` API is extended as follows:

.. code:: c

   fieldref_t svp_wrap(dispatcher_t*, typeid_t thetype, void* data);
 
The ``wrap`` service creates an entry in the field database and
associates it with the provided pointer. 

Subsequently, whenever the environment needs to make a logical copy
of the object or release a copy, it will also call the language-provided 
reference management functions. 
 
For example:

.. code:: c
 
   /* box signature: {A}->{B},  mode LMA for A */
   int box_func1(dispatcher_t* cb,  void *arg)
   {
       /* this box simply forwards its input to its output. */
       
       svp_out(cb, svp_wrap(cb, MYTYPE, arg));
   }


.. code:: c

   /* box signature: {A}->{B},  mode LMA for A and B */
   int box_func1(dispatcher_t* cb,  void *arg)
   {
       /* this box consumes its input and produces an unrelated output */
       
       /* .. decrease reference on arg, check if it was last reference ... */
 
       void *newdata = ...;
       svp_out(cb, svp_wrap(cb, MYTYPE, newdata));
   }

Semantics of ``copyref``/``release`` with LMA
---------------------------------------------


As per `Discussion about field ownership`_ is it useful to introduce
the notion of a "field reference counter with value 0" for field
references received as input to a box. Since we may work with field
references outside of box code, we need to specify what it means to
have a "field reference counter 0 and still keep the field data".

We distinguish the field reference counter (FRC) managed by the
environment for the field reference, and the data reference counter
(DRC) managed indirectly by the type manager's ``incref`` and
``decref`` functions. The state transitions are as follows:

- FRC goes from N>1 to N-1: ``decref`` is called; result is FRC = DRC

- FRC goes from N>=1 to N+1: ``incref`` is called; result is FRC = DRC

- FRC goes from 1 to 0: ``decref`` is *not* called. Result is FRC=0, DRC=1

- FRC goes from 0 to 1: ``incref`` is *not* called. Result is FRC=1, DRC=1

The invariant is that DRC>=1, ie the data remains allocated as long
as the field reference exists, even when FRC=0. 

Wrapping up
===========

State structures
----------------

API dispatcher (``dispatcher_t``) :
  identifies the connection between an entity and the API services in
  the environment.  Used as base for the field and communication APIs
  visible from box code.

registration context (``regctx_t``) :
  identifies a registration environment. Used as base for the
  registration APIs during system initialization.

data manager context (produced by ``langmgr_cb->init``) :
  identifies an opaque state environment for the concrete type
  management functions.

API index
---------

================ ==================== =========== ====================================================
Name             API provider         User        Description
================ ==================== =========== ====================================================
``out``          Network interpreter  Box code    Send one output record to the default output stream.
``log``          Logging manager      Any         Log text to a context-dependent logging stream.
``access``       Field manager        Any         LMA/EMA: Retrieve pointer to field data.
``clone``        Field manager        Any         LMA/EMA: Duplicate an existing object.
``copyref``      Field manager        Any         LMA/EMA: Create a new reference to an existing object.
``getmd``        Field manager        Any         LMA/EMA: Retrieve field content metadata.
``new``          Field manager        Any         EMA: create a new object.
``resize``       Field manager        Any         EMA: resize an existing object.
``wrap``         Field manager        Any         LMA: wrap an object into a field reference.
``alloc``        EMA type mgr.        Field mgr.  Allocate space for a new object.
``free``         EMA type mgr.        Field mgr.  Deallocate space.
``copy``         EMA/LMA type mgr.    Field mgr.  Duplicate object data.
``incref``       LMA type mgr.        Field mgr.  Increase the reference counter.
``decref``       LMA type mgr.        Field mgr.  Decrease the reference counter, maybe free.
``testref``      LMA type mgr.        Field mgr.  Test the reference counter.
``getsize``      LMA type mgr.        Field mgr.  Estimate the allocated memory size.
``serialize``    Data language mgr.   Dist. mgr.  Serialize an object.
``getsersize``   Data language mgr.   Dist. mgr.  Compute buffer size for serialization.
``deserialize``  Data language mgr.   Dist. mgr.  Deserialize an object.
``getdesersize`` Data language mgr.   Dist. mgr.  Compute object size for deserialization.
``init``         Data language mgr.   Sys. init.  Initialize a data language manager.
``cleanup``      Data language mgr.   Sys. init.  Clean up a data language manager.
================ ==================== =========== ====================================================

Contents of ``langif.h``
------------------------

We propose to implement the APIs not using regular C functions, but
instead as indirect calls via the dispatcher wrapped in preprocessor
macros.

.. include:: examples/langif.h
   :code: c



Backward compatibility
----------------------

The APIs proposed above are similar to C4SNet in the following fashion:

.. code:: c

   #define C4SNetOut svp_out

   #define C4SNetCreate(hnd, type, size, data) \
       ((c4snet_data_t*)(void*)svp_wrap(hnd, type, data))
   
   #define C4SNetFree(hnd, ptr) \
       svp_release(hnd, (fieldref_t)(void*)(ptr))
   
   static inline 
   c4snet_data_t* C4SNetAlloc(dispatcher_t* hnd, c4snet_type_t type, size_t size, void **data)
   {
       fieldref_t r = svp_new(hnd, size, type);
       svp_access(hnd, r, data);
       return (c4snet_data_t*)(void*)r;
   }
   
   static inline
   size_t C4SNetSizeof(dispatcher_t* hnd, c4snet_data_t* ptr)
   {
       size_t v;
       svp_getmd(hnd, (fieldref_t)(void*)(ptr), &v, 0, 0);
       return v;
   }
   
   static inline
   void* C4SNetGetData(dispatcher_t* hnd, c4snet_data_t* ptr)
   {
       void *v;
       svp_access(hnd, (fieldref_t)(void*)(ptr), &v);
       return v;
   }

We list these "emulation" functions here for clarity and to illustrate
how the new API differs from the old, not to suggest that the old API
should still be used.

The main change compared to the original C4SNet is that each API
function learns "where" it was called from from its 1st argument.


Changes needed to existing application code
-------------------------------------------

The new ``svp_*`` macros should be used, or alternatively the existing
``C4SNet*`` calls should be adapted to provide the ``hnd`` as first
argument.

Also, the box code should be checked with regards to field ownership,
to ensure that fields are not released more than needed.

