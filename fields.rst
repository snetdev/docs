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

   typedef ... dispatch_t;

   // proposed logging levels
   #define  LOG_NOTSET   0
   #define  LOG_DEBUG    10   // printf-style debugging
   #define  LOG_INFO     20   // what is being communicated, identifiers, etc
   #define  LOG_WARN     30   // unexpected conditions, can resume
   #define  LOG_ERROR    40   // unexpected condition, will terminate computation prematurely
   #define  LOG_FATAL    50   // unexpected condition, behavior undefined
 
   /* API accessors */
   int svp_log(dispatch_t*, int, const char*, ...);

The logging function reports messages on a logging channel specific to
the context where it is called (identified via ``dispatch_t``).

For example, a box function with type ``{<a>} -> {<x>,<y>,<z>}`` could
be written so:

.. code:: c

   /* box signature:  (<a>) -> () */
   int testbox(dispatch_t* cb,  tagval_t a)
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
   int svp_out(dispatch_t* hnd, ...);

Each call to the output function produces one record on the default
output stream associated with the context (identified via ``dispatch_t``).
   
For example, a box function with type ``{<a>} -> {<x>,<y>,<z>}``
could be written so:

.. code:: c

   int testbox(dispatch_t* cb,  tagval_t a)
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
   int svp_access(dispatch_t*, fieldref_t theref, void **ptr);

   // getmd: retrieve field metadata.
   int svp_getmd (dispatch_t*, fieldref_t theref, 
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

   /* box signature: (string) -> (<t>)  */
   int testfirstnul(dispatch_t* cb, fieldref_t f)
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
        return svp_out(cb, (tagval_t)(p[0] == '\0'));
   }

Field database
==============

The field database maps integer values of type ``fieldref_t`` to
managed objects in memory.

The field database is only visible from box/control entity code using
the two ``dispatch_t``-based APIs, LMA and EMA, documented below. 

Another API will be described separately for monitoring/analysis code
which wants to tracks how many fields are currently allocated, who has
allocated what, and so on.

Common API
----------

We provide the following:

.. code:: c

   fieldref_t  svp_copyref(dispatch_t* cb, fieldref_t r);
 
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
  aligned (aligned on uintmax_t or the largest floating-point type,
  whichever is largest)

- ``BYTES_CACHE_ALIGNED``: size unit is bytes, allocation is scalar
  and cache line aligned.

- ``BYTES_PAGE_ALIGNED``: size unit is bytes, allocation page
  aligned.

All these types serialize and deserialize to themselves without any
value conversion.

The following "semantic" types are also supported:

- ``FLOATS``: size unit is 32 bits, allocation is float-aligned
  (at least 32 bits, may be larger on some platforms).

- ``DOUBLES``: size unit is 64 bits, allocation is double-aligned
  (at least 64 bits, may be larger on some platforms).

- ``INT32``: size unit is 32 bits, allocation is 32-bit aligned.

- ``INT64``: size unit is 64 bits, allocation is 64-bit aligned.

These types serialize and deserialize to network-neutral
representations of the values described by the type name.

Usage in box code
-----------------

For this purpose the ``dispatch_t`` API is extended as follows:

.. code:: c

   // new: creates a fresh new object of the specified type and size.
   fieldref_t svp_new(dispatch_t*, size_t thesize, typeid_t thetype);
 
   // release: drop the specified reference.
   void svp_release(dispatch_t*, fieldref_t theref);
 
   // resize: modify the logical size of the object.
   int svp_resize(dispatch_t*, fieldref_t theref, size_t newsize);
 
   // clone: create a fresh copy of the data with its own reference
   void svp_clone(dispatch_t*, fieldref_t theref);
   
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
   
   // signature: (<tag>) -> (ll)
   void t2l(dispatch_t* cb,  tagval_t tag)
   {
       svp_log(cb, LOG_INFO, "hello from t2l, tag = %d", tag);
 
       // allocation by the "environment"
       fieldref_t f = svp_new(cb, sizeof(long long), BYTES_SCALAR_ALIGNED);
 
       // output the field reference
       svp_out(cb, f);

       // release the field reference
       svp_release(cb, f);

       return 0;
   }

Field access using the language-managed API (LMA)
=================================================

In this setting:

- the box language's RTS must register some reference management
  functions (make a new reference, drop a reference) upon system
  initialization; and

- in each box, direct pointers to the data allocated by the box
  language can be passed around:
 
  - when a box function "sends" the data to ``out``, it must first
    *wrap* the data pointer in a field container to obtain a field
    reference.

  - a box can *unwrap* an input field to 
    release the field reference without deallocating the data.

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

For the LMA the ``dispatch_t`` API is extended as follows:

.. code:: c

   fieldref_t svp_wrap(dispatch_t*, typeid_t thetype, void* data);
   fieldref_t svp_capture(dispatch_t*, typeid_t thetype, void* data);

   void*      svp_unwrap(dispatch_t*, fieldref_t ref);

Semantics:

``wrap`` / ``capture`` : 

  Both ``wrap`` and ``capture`` create an entry in the field database
  and associates it with the provided pointer.  Subsequently, whenever
  the environment needs to make a logical copy of the object or release
  a copy, it will also call the language-provided reference management
  functions. ``wrap`` and ``capture`` differ in that ``wrap`` leaves
  ownership of the data pointer to the calling code, whereas ``capture``
  takes ownership. 

  Conceptually ``capture`` is implemented as:
  
  .. code:: c
  
     fieldref_t svp_capture(dispatch_t* cb, typeid_t thetype, void* data)
     {
         fieldref_t f = svp_wrap(cb, thetype, data);
         /* language-specific decref(data) */
         return f;
     }
   

``unwrap`` :

  ``unwrap`` drops the provided field reference without decreasing
  the data object's reference counter, and returns the unwrapped
  data object.

  Conceptually ``unwrap`` is implemented as:
  
  .. code:: c
  
     void* svp_unwrap(dispatch_t* cb, fieldref_t f)
     {
         void* data;
         svp_access(cb, f, &data);
         /* language-specific incref(data) */
         svp_release(f);
         return data;
     }
   

  In particular:

  - ``unwrap(capture(p))`` is the identity function and does not
    impact the reference count;

  - ``unwrap(wrap(p))`` is the identity function, but will require
    one extra call to ``decref`` to fully release the object.

Examples
--------

The following box emits a freshly created object:

.. code:: c

   int box_func1(dispatch_t* cb)
   {
       void *newdata = /* ... alloc ... */;

       fieldref_t r = svp_wrap(cb, MYTYPE, newdata);
       svp_out(cb, r);
       svp_release(cb, r);

       /* language-specific decref(newdata) */

       return 0;
   }

Note that ``wrap`` does not transfer the ownership of the bare data
pointer into the field reference. Instead it increases the reference
counter of the data object using the lower-level ``incref`` API. 
In the previous example, this implies that ``release`` preserves the
object, which must be subsequently deallocated explicitly: if
``newdata`` has a reference count set to 1 upon ``wrap``, then after
wrap it will have count ``2``, inside ``out`` it may grow larger than
2, then ``release`` decreases the count back to 1. 

To fully transfer ownership ``capture`` can be used:

.. code:: c

   int box_func2(dispatch_t* cb)
   {
       void *newdata = /* ... alloc ... */;

       fieldref_t r = svp_capture(cb, MYTYPE, newdata);
       svp_out(cb, r);

       /* the following call to release() also deallocates
          the object, since capture() has taken ownership. */
       svp_release(cb, r);

       /* here decref(newdata) is not needed any more. */

       return 0;
   }

The following box code receives a managed object as input, processes
it internally, then emits it again as output:

.. code:: c

   int box_func3(dispatch_t* cb, fieldref_t x)
   {
      void* xdata = svp_unwrap(cb, x);

      /* ... process via xdata internally ... */

      fieldref_t r = svp_capture(cb, xdata);
      svp_out(cb, r);
      svp_release(cb, r);
 
      return 0;
   }

We discuss below how to simplify this code further.

Discussion about field ownership
================================

There is a discussion about who is responsible for releasing
references manipulated by boxes.

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
--------

We analyze the different scenarios.

About 1a: yields memory leaks if the programmer forgets to call ``release``.

About 1b: yields a potential wasted opportunity in long-running boxes
with the following structure:

.. code:: c
  
   // signature: (bytes) -> (<x>, bytes)
   int examplebox(dispatch_t* cb,  fieldref_t  x)
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


About 2a: yields memory leaks if the programmer forgets to call
``release``. Also, yields an opportunity loss. For example:

.. code:: c

   // signature: (bytes) -> (bytes)
   int examplebox(dispatch_t* cb)
   {
      fieldref_t x = /* ... */;
      svp_out(x);
      svp_release(x);
   }

In this example, the continuation of ``examplebox`` in the application
may be serialized entirely at run-time in the call to ``out``. However
since the ownership of the newly created field object is not
transferred to ``out``, this forces the object to persist until
``out`` returns. Thus the lifespan of the object is unnecessarily
extended beyond the necessary scope.

About 2b: creates a problem if a box wants to output multiple
references to the same field data. For example:

.. code:: c

   // signature: (bytes) -> (bytes)
   int examplebox(dispatch_t* cb,  fieldref_t  x)
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

   // signature: (<tag>) -> (bytes)
   int examplebox(dispatch_t* cb,  tagval_t tag)
   {
      for (int i = 0; i < 1000; ++i)
      { 
           fieldref_t f = svp_new(cb, 1, BYTES_UNALIGNED);
           svp_out(cb, f);
      }
      return 0;
   }

In this box, it would be inefficient if the environment waits until
the end before it releases the newly allocated objects. Also it would
create a memory leak if the box is modified so that the loop never
terminates.

The outcome of this analysis is the observation that each choice of
semantics will impact negatively a programming style. We can thus seek
to provide automatic management by default, with opt-in control when
the box programmer wants to optimize storage use.

Solution
--------

We do this as follows.

Default semantics
`````````````````

By default, the following semantics apply:

- the environment will call ``release`` for each input field object
  after the box terminates. This implies that the box code must not
  call ``release`` itself on its input fields.

- the ``out`` function will increase the reference count of the
  field references it is given as arguments. This implies that the
  box code must explicitly call ``release`` on newly allocated
  objects, but not on input fields transferred to ``out``.

The following examples illustrate:

+----------------------------------------+-----------------------------------------+
|Correct                                 |Incorrect                                |
+----------------------------------------+-----------------------------------------+
|.. code:: c                             |.. code:: c                              |
|                                        |                                         |
|   int f1(dispatch_t*, fieldref_t r)    |   int f1x(dispatch_t* cb, fieldref_t r) |
|   {                                    |   {                                     |
|     /* do nothing */                   |      svp_release(cb, r);                |
|     return 0;                          |      return 0;                          |
|   }                                    |   }                                     |
|                                        |                                         |
|                                        |(extraneous release)                     |
+----------------------------------------+-----------------------------------------+
|.. code:: c                             |.. code:: c                              |
|                                        |                                         |
|   int f2(dispatch_t* cb)               |   int f2x(dispatch_t* cb)               |
|   {                                    |   {                                     |
|       fieldref_t r = svp_new(...);     |      fieldref_t r = svp_new(...);       |
|       /* ... populate r ... */         |      /* ... populate r ... */           |
|       svp_out(cb, r);                  |      svp_out(cb, r);                    |
|       svp_out(cb, r);                  |      svp_out(cb, r);                    |
|       svp_release(r);                  |      return 0;                          |
|       return 0;                        |   }                                     |
|   }                                    |                                         |
|                                        |(memory leak: release missing after last |
|(possible inefficiency: the object      |call to ``out``)                         | 
|persists until the last call to ``out`` |                                         |
|completes, even though it is not needed |                                         |
|in ``f2`` any more at the moment this   |                                         |
|last call starts)                       |                                         |
+----------------------------------------+-----------------------------------------+
|.. code:: c                             |.. code:: c                              |
|                                        |                                         |
|   int f3(dispatch_t* cb)               |  int f3x(dispatch_t* cb)                |
|   {                                    |  {                                      |
|       void *p = /* private... */;      |      void *p = /* private... */;        |
|       fieldref_t r;                    |      svp_out(cb,                        |
|       r = svp_capture(cb, ..., p);     |              svp_capture(cb, ..., p));  |
|       svp_out(cb, r);                  |      return 0;                          |
|       svp_release(r);                  |  }                                      |
|       return 0;                        |                                         |
|   }                                    |(memory leak: release missing after call |
|                                        |to ``out``)                              |
|(possible inefficiency: the object      |                                         |
|persists until the call to ``out``      |                                         |
|completes, even though it is not needed |                                         |
|in ``f3`` any more at the moment this   |                                         |
|call starts)                            |                                         |
+----------------------------------------+-----------------------------------------+
|.. code:: c                             |                                         |
|                                        |                                         |
|   int f4(dispatch_t* cb, fieldref_t r) |                                         |
|   {                                    |                                         |
|       return svp_out(cb, r) &&         |                                         |
|              svp_out(cb, r);           |                                         |
|   }                                    |                                         |
|                                        |                                         |
|(possible inefficiency: the object      |                                         |
|persists until the box terminates, even |                                         |
|though it is not needed in ``f4`` any   |                                         |
|more at the moment the last call to     |                                         |
|``out`` starts)                         |                                         |
|                                        |                                         |
+----------------------------------------+-----------------------------------------+

Ownership override for output fields
````````````````````````````````````

We want to be able to optionally transfer ownership of newly created
field objects to the ``out`` function, ie. tell the ``out`` function
to not make a new reference upon entry.

For this we can use the following:

.. code:: c
 
   typedef ... outref_t;

   outref_t svp_demit(dispatch_t* cb, fieldref_t r);

The ``demit`` API returns a value suitable for use as argument to the
``out`` API, which says to ``out`` to "take ownership" of the
reference. Subsequently, the code invoking ``out`` does not need to
call ``release`` on that reference any more.

For example:

.. code:: c

  int testbox(dispatch_t* cb)
  {
      fieldref_t r = svp_capture(cb, ...);
      svp_out(cb, svp_demit(cb, r));
      /* no need to release r here */
      return 0;
  }

This enables the following syntax shortcut, useful for LMA users:

.. code:: c

  #define svp_capture_demit(x, y, z, t) svp_demit(x, svp_capture(x, y, z, t))

  int testbox(dispatch_t* cb)
  {
      void *p = /* private... */;
      return svp_out(cb, svp_capture_demit(cb, ..., p));
  }

Note that it is not possible to yield ownership of an input argument this way;
in particular the following example is invalid:

.. code:: c

  int testbox(dispatch_t* cb, fieldref_t r)
  {
      return svp_out(cb, svp_demit(r));
  }

This is invalid because the caller of ``testbox`` will call
``release`` on behalf of ``testbox``, ie ``testbox`` does not "own"
the reference it is given as input.  As a result, with the code above
the reference may be released by ``out``, and then again when
``testbox`` returns, which is invalid. 

To transfer ownership of an input field object, we propose to override
the override definition for box inputs separately.

Ownership override for input fields
```````````````````````````````````

We want to be able to optionally take ownership, in the box code, of a
field object received as input, so that:

- the box code becomes responsible for calling ``release``;

- it can use ``demit`` to transfer ownership of its input
  to ``out``.

For this we propose a solution in two phases:

1. we introduce a new way to write box function interfaces, where the
   binding of function variables to fields/tags is done by the
   function itself instead of its caller. 

   This works as follows:
 
   +----------------------------------------------+----------------------------------------+
   |Traditional box interface ("ext bind")        |New box interface ("self bind")         |
   +----------------------------------------------+----------------------------------------+
   |.. code:: c                                   |.. code:: c                             |
   |                                              |                                        |
   |   // signature: (a, <b>) -> ...              |   // signature: (a, <b>) -> ...        |
   |   int boxfunc(dispatch_t*,                   |  int boxfunc(dispatch_t* cb)           |
   |               fieldref_t a, tagvalue_t b)    |  {                                     |
   |   {                                          |      fieldref_t a;                     |
   |       /* ... use a, b ... */                 |      tagvalue_t b;                     |
   |       return 0;                              |      svp_bind(cb, &a, &b);             |
   |   }                                          |                                        |
   |                                              |      /* ... use a, b ... */            |
   |                                              |      return 0;                         |
   |                                              |   }                                    |
   |                                              |                                        |
   +----------------------------------------------+----------------------------------------+
 
   For this we introduce the following API:
  
   .. code:: c
 
      void svp_bind(dispatch_t*, ...);
 
   Which binds the variables passed by reference to their corresponding
   input record slots.
 
2. then we introduce a new primitive to "take ownership" of an input
   field reference:
 
   .. code:: c

      typedef ... claimref_t;
     
      claimref_t* svp_claim(dispatch_t*, fieldref_t *var);

   This can be then used as follows:

   .. code:: c

      // signature: (a, <b>, c) -> ...
      int boxfunc(dispatch_t* cb)
      {
          fieldref_t a, c;
          tagvalue_t b;

          // want to claim c, but not a:
          svp_bind(cb, &a, &b, svp_claim(cb, &c));
      
          /* ... use a, b ... */

          svp_release(cb, c); // because c has been claimed
          return 0;
      }
    
   In this example, ``claim`` indicates to ``bind`` that the box
   function is taking ownership. Subsequently, the reference for
   field ``c`` is not released by the environment when the box
   function terminates; the box function must call ``release`` itself.

Examples using the EMA
----------------------

We illustrate with two examples. The first creates 1000 different
fields:

.. code:: c

   // signature: (<tag>) -> (bytes)
   int examplebox(dispatch_t* cb,  tagval_t tag)
   {
      for (int i = 0; i < 1000; ++i)
      { 
           fieldref_t f = svp_new(cb, 1, BYTES_UNALIGNED);            
           svp_out(cb, svp_demit(cb, f));
      }

      return 0; 
   }

At every iteration of the loop ``new`` creates a new reference with
count 0.  Then ``demit`` gives away ownership to ``out``. All the 1000
fields are deallocated as part of ``out`` 's continuation; the
references are not valid when ``out`` returns.

The second example outputs the same input field 1000 times:

.. code:: c

   // signature: (bytes) -> (bytes)
   int examplebox(dispatch_t* cb,  fieldref_t  x)
   {
      for (int i = 0; i < 1000; ++i)
           svp_out(cb, x);

      return 0;
   }

Third example: a box outputs a modified copy of its input. We want to
optimize for the case where the input storage can be directly
modified. 
 
+----------------------------------------+----------------------------------------+----------------------------------------+
|Incorrect                               |Correct: "ext bind", unoptimized        |Correct: "self bind", preferred         |
+----------------------------------------+----------------------------------------+----------------------------------------+
|.. code:: c                             |.. code:: c                             |.. code:: c                             |
|                                        |                                        |                                        |
|   int boxfunc(dispatch_t* cb,          |   int boxfunc(dispatch_t* cb,          |   int boxfunc(dispatch_t* cb)          |
|               fieldref_t x)            |               fieldref_t xin)          |   {                                    |
|   {                                    |   {                                    |      fieldref_t x;                     |
|     void *ptr;                         |     void *ptr;                         |      void* ptr;                        |
|     int rw = svp_access(cb, x, &ptr);  |     outref_t x = xin;                  |                                        |
|     if (!rw) {                         |     int rw = svp_access(cb, x, &ptr);  |      svp_bind(cb, svp_claim(&x));      |
|       // can't write, so make a copy.  |     if (!rw) {                         |                                        |
|       x = svp_clone(cb, x);            |       // can't write, so make a copy.  |      int rw = svp_access(cb, x, &ptr); |
|       svp_access(cb, x, &ptr);         |       x = svp_clone(cb, x);            |      if (!rw) {                        |
|     }                                  |       svp_access(cb, x, &ptr);         |         fieldref_t y;                  |
|                                        |                                        |         y = svp_clone(cb, x);          |
|     /* ... use ptr here ... */         |       // demit the copy, so that       |         svp_access(cb, y, &ptr);       |
|                                        |       // out() below will take it.     |                                        |
|     return svp_out(cb, x);             |       x = sp_demit(cb, x);             |         // release the original.       |
|   }                                    |     }                                  |         svp_release(cb, x);            |
|                                        |                                        |         x = y;                         |
|This is incorrect, because if ``clone`` |     /* ... use ptr here ... */         |      }                                 |
|is called the corresponding object will |                                        |                                        |
|never be deallocated. This is because   |     return svp_out(cb, x);             |      /* ... use ptr here ...*/         |
|the environment only calls ``release``  |   }                                    |                                        |
|automaticaly on the fields that arrive  |                                        |      return svp_out(cb,                |
|as input, not those generated by        |This is "unoptimized" because the       |                     svp_demit(cb, x)); |
|the box                                 |lifespan of the original ``xin`` object |   }                                    |
|code.                                   |extends until ``boxfunc`` terminates,   |                                        |
|                                        |although it is not needed past the call |Here the input object is released early |
|                                        |to ``clone``.                           |when ``clone`` is used.                 |
|                                        |                                        |                                        |
+----------------------------------------+----------------------------------------+----------------------------------------+

Examples using the LMA
----------------------

A box allocates a managed private data object, then sends it as an output field:

.. code:: c

   int boxfunc(dispatch_t* cb)
   {
       void *p = /* alloc */;
       
       svp_out(cb,
        svp_wrap_demit(cb, MYTYPE, p));

       /* language-specific decref(p) */
       return 0;
   }       

In this code, the call to ``wrap_demit`` captures the data pointer
in a field reference, whose ownership is subsequently transferred to
``out``. However, the ownership of the data pointer itself is not
transferred, and it must thus still be deallocated in the
language-specific manner after the call to ``out``.

To transfer the ownership of the data object itself, use ``capture``:

.. code:: c

   int boxfunc(dispatch_t* cb)
   {
       void *p = /* alloc */;
       
       svp_out(cb,
        svp_capture_demit(cb, MYTYPE, p));

       /* no decref(p) needed here */
       return 0;
   }       

In another example, we want to write a box which emits a single
private object in two successive records:

.. code:: c

   int boxfunc(dispatch_t* cb)
   {
       void *p = /* ... alloc ... */;

       svp_out(cb,
        svp_wrap_demit(cb, ..., p));

       /* ... */

       svp_out(cb,
        svp_wrap_demit(cb, ..., p));

       /* language-specific decref(p) */

       return 0;
   }

In this example, the initial allocation of ``p`` persists across
multiple calls to ``wrap``.

Wrapping up
===========

State structures
----------------

API dispatcher (``dispatch_t``) :
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
``bind``         Network interpreter  Box code    Retrieve data from the input record.
``claim``        Network interpreter  Box code    Disable automatic release on an input field.
``demit``        Network interpreter  Box code    Relinquish ownership of a field to ``out``.
``log``          Logging manager      Any         Log text to a context-dependent logging stream.
``access``       Field manager        Any         LMA/EMA: Retrieve pointer to field data.
``clone``        Field manager        Any         LMA/EMA: Duplicate an existing object.
``copyref``      Field manager        Any         LMA/EMA: Create a new reference to an existing object.
``getmd``        Field manager        Any         LMA/EMA: Retrieve field content metadata.
``new``          Field manager        Any         EMA: create a new object.
``resize``       Field manager        Any         EMA: resize an existing object.
``wrap``         Field manager        Any         LMA: wrap an object into a field reference.
``capture``      Field manager        Any         LMA: transfer an object into a field reference.
``unwrap``       Field manager        Any         LMA: unwrap a field reference and retrieve object.
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

Summary of ownership rules
--------------------------

================= ===============================================================
Pattern           Ownership rule
================= ===============================================================
new(), clone()    Caller of ``new`` receives ownership of new reference.
wrap()            Caller of ``wrap`` keeps ownership of input object,
                  receives ownership of the newly created field reference.
unwrap()          Caller of ``wrap`` transfers ownership of field reference 
                  to ``unwrap`` (which then calls ``release``), and receives
                  back ownership of the data object.
capture()         Caller of ``wrap`` receives ownership of the newly created
                  field reference; ownership of input object transferred to
                  the field reference: last ``release`` on the field reference
                  also deallocates captured object.
copyref()         Caller of ``copyref`` receives ownership for output reference.
out()             Ownership of field reference stays in caller.
out(demit())      Ownership of field reference transferred to ``out``.
bind()            Ownership of input field reference stays in environment.
bind(claim())     Ownership of input field reference transferred to caller
                  of ``bind``.
================= ===============================================================




Contents of ``langif.h``
------------------------

We propose to implement the APIs not using regular C functions, but
instead as indirect calls via the dispatcher wrapped in preprocessor
macros.

Here is an example implementation:

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
   c4snet_data_t* C4SNetAlloc(dispatch_t* hnd, c4snet_type_t type, size_t size, void **data)
   {
       fieldref_t r = svp_new(hnd, size, type);
       svp_access(hnd, r, data);
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

We list these "emulation" functions here for clarity and to illustrate
how the new API differs from the old, not to suggest that the old API
should still be used.

The main change compared to the original C4SNet is that each API
function learns "where" it was called from from its 1st argument.


Changes needed to existing application code
-------------------------------------------

The new ``svp_*`` macros should be used, or alternatively the existing
``C4SNet*`` calls should be adapted to provide the ``hnd`` as first
argument. Also, the box code should be checked with regards to field
ownership, to ensure that field objects are not released more or less
than needed.

To use the new "self bind" interface using ``bind`` instead of
receiving record fields as function arguments, a metadata annotation
can be used on the box.
