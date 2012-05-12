===============
 Scalar fields
===============

:Authors: frank, kena
:Date: May 2012

:Abstract: This note proposes to extend the field syntax with a simple
   scheme to support integer and floating point scalars, with no visible
   change for existing code.

Introduction
============

The type system for records currently knows about tags, which are
simple integers, and fields, which are manipulated by reference.

The machinery to support object fields, while suitable for "large"
data like arrays, is heavyweight to communicate "simple" values like
long integers or floating-point scalars.

So we propose a simple syntax to annotate fields in a box interface
which should be communicated around as scalars instead of object references.

Syntax at box interface
=======================

The box syntax is of the form "(a,b,c,...) -> (d,e,f,...)".

As long as each of the names "a" "b" "c" etc contains only
alphanumeric characters, it refers to an object manipulated by
reference. This default remains.

In addition to this, we introduce the following 3 formats:

"X:int" : 
   X refers to a scalar integer, large enough to hold a
   64-bit value or a pointer.

"X:float" : 
   X refers to a scalar single-precision IEEE floating-point value (32 bits).

"X:double" : 
   X refers to a scalar double-precision IEEE floating-point value (64 bits).

From the perspective of box typing and combination these should be
considered as part of the field name, ie they should match exactly
when connecting the output of one box to the input to another. We do not propose
automatic conversion of any kind.

Interface in boxes
==================

We introduce the following implementation type:

``intval_t`` : 
    A signed integer type at least as large as C's ``intptr_t`` or
    ``int64_t``, whichever is the largest.

From this point a box can use the scalar types in their interface, for
example:

+--------------------------------------------+------------------------------------------+
|Using the "external bind" interface         |Using the "self bind" interface           |
+--------------------------------------------+------------------------------------------+
|.. code:: c                                 |.. code:: c                               |
|                                            |                                          |
|   // sig: (a:int,b:int) -> (c:int)         |   // sig: (a:int,b:int) -> (c:int)       |
|   int add(dispatch_t*cb,                   |   int add(dispatch_t*cb)                 |
|           intval_t a, intval_t b)          |   {                                      |
|   {                                        |      intval_t a, b;                      |
|       return svp_out(cb, a+b);             |      svp_bind(cb, &a, &b);               |
|   }                                        |      return svp_out(cb, a+b);            |
|                                            |   }                                      |
|                                            |                                          |
+--------------------------------------------+------------------------------------------+
|.. code:: c                                 |.. code:: c                               |
|                                            |                                          |
|   // sig: (x:double) -> (a:double,b:int)   |   // sig: (x:double) -> (a:double,b:int) |
|   int box_frexp(dispatch_t*cb, double x)   |   int box_frexp(dispatch_t*cb)           |
|   {                                        |   {                                      |
|      double mantissa;                      |      double x;                           |
|      int exponent;                         |      svp_bind(cb, &x);                   |
|      mantissa = frexp(x, &exponent);       |                                          |
|                                            |      double mantissa;                    |
|      // NB: cast to intval_t necessary     |      int exponent;                       |
|      return svp_out(cb, mantissa,          |      mantissa = frexp(x, &exponent);     |
|                      (intval_t)exponent);  |                                          |
|   }                                        |      return svp_out(cb, mantissa,        |
|                                            |                      (intval_t)exponent);|
|                                            |   }                                      |
|                                            |                                          |
+--------------------------------------------+------------------------------------------+

Rationale for not introducing more primitive types
==================================================

Why not more integer or FP types? 
   Each scalar value occupies a slot in a record. All slots are
   equally sized. Therefore, the minimum width of a slot must be large
   enough to accommodate all scalar types and field references.

   With the proposed design, a slot is large enough for both a
   ``fieldref_t``, ``double`` and ``intval_t``. The latter is in turn
   guaranteed to be wide enough for 64 bits or a pointer. Therefore,
   any smaller integer type is implicitly supported, which includes
   most integer types in contemporary systems.
 
   Introducing support for larger integers or floats (eg. ``__int128``
   or ``long double``) would in turn require to grow the size of a
   slot, impacting overall storage usage by records. We might consider
   this if needs arise, but for now the corresponding storage overhead
   was not deemed justified.
   

Why not arrays? 
   Arrays are already supported by the default data language of the
   EMA object interface for regular object fields. 

   For example:

   .. code:: c

      // sig: (<sz>, v:float) -> (a)
      // function: produce an array of sz floats with value v
      int box(dispatch_t* cb, tagval_t sz, float v)
      {
         // allocate the array
         fieldref_t f = svp_new(cb, FLOATS, sz);

         // get access to the float storage
         float *p;
         svp_access(cb, f, &p);

         // fill in the values
         for (int i = 0; i < sz; ++i)
             p[i] = v;

         // produce the output record
         return svp_out(cb, svp_demit(f));
      }
 
Summary of types usable at box interface
========================================

=============== ====================================================================
Type            Description
=============== ====================================================================
``tagval_t``    Tag value: integer of non-guaranteed width.
``fieldref_t``  Object reference field, use the field manager to access.
``intval_t``    **(new)** Integer scalar field, passed by value; min size 64 bits or pointer.
``float``       **(new)** 32-bits (single-precision) FP scalar field, passed by value.
``double``      **(new)** 64-bits (double-precision) FP scalar field, passed by value.
=============== ====================================================================

