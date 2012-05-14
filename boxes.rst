==========================
 Dynamic box registration
==========================

:Authors: kena
:Date: May 2012

:Abstract: This note proposes a simple interface to capture all boxes
   behind a single uniform interface from the perspective of the run-time
   system. This interface in turn allows to delay the selection of
   box implementations until run-time, and will serve as a basis to
   implement run-time box substitution in the future.


Record slot types
=================

Each slot in a record has both a *slot type* and a *payload*. The slot type
identifies what meaning to give to the payload, and in turn determines how
to convert the payload to box/entity variables.

.. code:: c

   typedef ... slottype_t; /* some unspecified integer type */

The slot types are as follows.

``SLOT_TAG`` :
    a tag

``SLOT_FIELD_OBJECT`` :
    an object reference

``SLOT_SCALAR_INTEGER`` :
    an integer scalar

``SLOT_SCALAR_FLOAT`` :
    a single-precision FP value

``SLOT_SCALAR_DOUBLE`` :
    a double-precision FP value


Box signatures
==============

To call a box function, the environment must know which concrete slot
types it accepts as input and produces as output. The slot types are
statically known, since they are structurally encoded in the code
(either as the box function prototype, or as the type/order of
variables in the call to ``out`` and ``bind``).

We define the following implementation for a signature:

.. code::

   struct boxsig { 
        size_t      nslots;
        slottype_t  types[];
   };


Box registration
================

We propose that collections of box functions captured a compile-time
into a single link-time object (or shared object suitable for dynamic
loading) are hidden behind a single "registration" function.

This registration function in turn receives a pointer to a
``boxregctx_t``, and then uses the API ``svp_reg_boxmod`` defined as
follows:

.. code:: c

   typedef ... boxregctx_t;

   struct boxmgr_cb {    
     int     (*init)       (void** mgrctx);
     void    (*cleanup)    (void* mgrctx);
   };          

   boxmodid_t svp_reg_boxmod(boxregctx_t* reg, struct boxmgr_cb* boxmgr, const char *humanname);

``reg_boxmod`` registers as *box module* with the environment. A
box module is a collection of box functions that have been compiled
and linked together in a single link-time component (eg. a ``.so``).

The environment in turn guarantees that the ``init`` function, if
specified, is called before any of the boxes starts. The ``mgrctx``
produced by ``init``, if any, is subsequently passed as argument to
all the box invocations. This can be used eg. to carry state around
for the box language's run-time system. ``init`` can return non-zero
to indicate failure, in which case the associated boxes cannot be
used.

The ``cleanup`` function is defined is called after all boxes
terminate upon system shut-down.

Universal box wrapper
=====================

Then we propose the following interface for all boxes provided by a
box module:

.. code:: c

   struct boxapi_cb {
     const struct boxsig *input_sig;
     const struct boxsig *output_sig;
     int   (*run)(void* boxmgrctx, dispatch_t* d);
   };

This interface would be generated automatically by the "box compiler"
(to be described separately). Each interface would then be registered
by the box module loader function, using the following API:

.. code:: c

   boxid_t    svp_reg_box(boxregctx_t* reg, boxmodid_t modid, struct boxapi_cb* box, const char *humanname);

``reg_box`` registers a *box API* with the environment, for a given
box module ID (produced previously by ``reg_boxmod``).

Box metadata retrieval and execution
====================================

.. code:: c

   typedef ... boxmgr_t;

   const struct boxsig* svp_box_input_sig(boxmgr_t*mgr, boxmodid_t mod, boxid_t box);
   const struct boxsig* svp_box_output_sig(boxmgr_t*mgr, boxmodid_t mod, boxid_t box);

   int svp_box_run(boxmgr_t*mgr, boxmodid_t mod, boxid_t box, dispatch_t* cb);


The network interpreter will be given a reference to a *box manager*
which is aware of which boxes are registered.

Provided a box module ID and box ID previously registered with
``reg_boxmod`` and ``reg_box``, the box manager will run the named box
using the given dispatcher.

(The implementation of the box manager will be "smart" enough to provide
low-overhead, constant-time look-up of boxes)

Putting it all together
=======================

Let us define a module ``mymod`` with a single box named ``strcat``.

The box programmer has provided separately:

- the name of the actual box function, ``strcat_box``

- the concrete type signature "(object,object) -> (object)"

From this we can generate automatically the following:

.. code:: c
   
   // ALL THIS CODE WILL BE BE AUTOMATICALLY GENERATED

   // box function interface
   extern int strcat_box(dispatch_t* cb, fieldref_t f1, fieldref_t f2);

   static int strcat_wrap(void* unused, dispatch_t* cb)
   {
       fieldref_t f1, f2;
       svp_bind(cb, &f1, &f2);
       return strcat_box(cb, f1, f2);
   }

   static struct boxsig strcat_in = { 2, { SLOT_FIELD_OBJECT, SLOT_FIELD_OBJECT }};
   static struct boxsig strcat_out = { 1, { SLOT_FIELD_OBJECT } };

   void boxreg(boxregctx_t* reg)
   {
        struct boxmgr_cb mymgr = { NULL, NULL }; 
        struct boxapi_cb strcat_api = { &strcat_in, &strcat_out, &strcat_wrap };

        boxmodid_t mymod = svp_reg_boxmod(reg, &mymgr, "mymod");
        svp_reg_box(reg, mymod, &strcat_api, "strcat");
   }

When this is compiled, together with the object providing
``strcat_box``, the result is a library defining the name
``boxreg``. Assuming dynamic loading can be used, the environment can
then look up the name ``boxreg`` from the shared object, transfer
control to it and receive back the initialization of the box module
and the box APIs.

Other example: let us define a module ``math`` with two boxes ``cos``
and ``sin``.  The box programmer has provided separately:

- the name of the actual box functions, ``sin_box`` and ``cos_box``;

- the concrete type signatures "(x:double) -> (x:double)" for both
  functions.

From this we can generate automatically the following:

.. code:: c
   
   // ALL THIS CODE WILL BE BE AUTOMATICALLY GENERATED

   // box function interface
   extern int sin_box(dispatch_t* cb, double x);
   extern int cos_box(dispatch_t* cb, double x);

   static int sin_wrap(void* unused, dispatch_t* cb)
   {
       double x;
       svp_bind(cb, &x);
       return sin_box(cb, x);
   }
   static int cos_wrap(void* unused, dispatch_t* cb)
   {
       double x;
       svp_bind(cb, &x);
       return cos_box(cb, x);
   }

   static struct boxsig thesig = { 1, { SLOT_DOUBLE } };

   void boxreg(boxregctx_t* reg)
   {
        struct boxmgr_cb mymgr = { NULL, NULL }; 
        struct boxapi_cb sin_api = { &thesig, &thesig, &sin_wrap };
        struct boxapi_cb cos_api = { &thesig, &thesig, &cos_wrap };

        boxmodid_t mymod = svp_reg_boxmod(reg, &mymgr, "math");
        svp_reg_box(reg, mymod, &sin_api, "sin");
        svp_reg_box(reg, mymod, &cos_api, "cos");
   }

When this is compiled, together with the object providing ``sin_box``
and ``cos_box``, the result is a library defining only the name
``boxreg``.


Example ``sin_box`` / ``cos_box``
---------------------------------

(To complete the example above)


.. code:: c

   #include <math.h>

   int sin_box(dispatch_t *cb, double x) { return svp_out(cb, sin(x)); }

   int cos_box(dispatch_t *cb, double x) { return svp_out(cb, cos(x)); }

Example ``strcat_box``
----------------------

(To complete the example above)

.. code:: c

   #include <string.h>

   int strcat_box(dispatch_t* cb, fieldref_t f1, fieldref_t f2)
   {
      const char *s1, s2;
      svp_access(cb, f1, &s1);      
      svp_access(cb, f1, &s2);      

      size_t l1, l2;
      svp_getmd(cb, f1, &l1, 0, 0);
      svp_getmd(cb, f2, &l2, 0, 0);
      
      fieldref_t fr = svp_new(cb, BYTES_UNALIGNED, l1+l2);
      if (!fr) return 1; // error

      char *sr;
      svp_access(cb, fr, &sr);

      strncpy(sr, s1, l1);
      strncpy(sr + l1, s2, l2);

      return svp_out(cb, svp_demit(cb, fr))    
   }

Scenarios
=========

Late binding
------------

The run-time system can be seen as a program which reads as input an
*application description* defined as:

a) a network description and
b) information about the outermost input and output stream endpoints

the RTS program then performs:

1. deployment of the network description into a run-time instance;
2. binding of the box names mentioned in the network description to a
   concrete implementation, and dynamic loading of the concrete
   implementation;
3. execution of the run-time instance until the input stream is empty;
4. flushing the remaining output;
5. destruction of the run-time instance;
6. unloading of the boxes.

This scenario can be repeated over time in a single RTS process, and
even carried out concurrently between multiple application descriptions.

The interface proposed here simplifies #2 and #6 by removing any
link-time dependencies between box code and the execution environment.


Introspection
-------------

In the running system, an API will be provided separately to query dynamically:

- the list of all ``boxmodid_t`` that have been registered so far,
  together with their "human name";
- arbitrary metadata associated with box modules, keyed on
  ``boxmodid_t``;
- the list of all ``boxid_t`` that have been registered so far for a
  given ``boxmodid_t``, together with their "human name";
- arbitrary metadata associated with boxes, keyed on ``boxmodid_t`` and
  ``boxid_t``.

