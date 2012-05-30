==========================
Multithreaded SaC in S-Net
==========================

:Authors: jara
:Date: May 2012

:Abstract: This note describes the interfacing of the SaC language and run-time system
  with the S-Net above it and the LPEL below it.


Public interfaces provided by SaC
=================================

The SaC compiler can produce either directly executable stand-alone programs,
or dynamic libraries that can be used from an external program.
The sac2c compiler produces modules and executable programs,
and afterwards the sac4c tool may be used to create a wrapper code that enables any SaC module
to be called externally.

Concurrent parallel execution in SaC is supported using either multithreading, OpenMP, or CUDA.
Multithreading can be done via PThreads or LPEL backends.

With regards to S-Net the SaC code has to be compiled into shared libraries (using sac2c/sac4c),
and if the parallel execution is desired the LPEL multithreading backend must be used.

SaC system initialisation
-------------------------

.. code:: c

  /* All the declarations are in sacinterface.h */
  #include "sacinterface.h"

  void SAC_InitRuntimeSystem (void);
  void SAC_FreeRuntimeSystem (void);

The init/free functions are supposed to be called when initialising/finalising the S-Net environment.
The init function sets up shared global variables used by all SaC-generated functions.


Compute bee-hives
-----------------

By a '*bee-hive*' we mean a collection of compute processors (*bees*) allocated for SaC.
A hive is materialised as a set of slave threads (in the PThreads backend) or slave LPEL tasks.
Due to historical reasons hives are *not* passed into SaC user functions via an explicit argument.
Instead they must be bound (*attached*) to the calling thread (or task) beforehand; thus they are passed into
the user functions implicitly.

Terminology note: Hives are collections of bees. In the PThreads backend the bees are ordinary threads,
and in the LPEL backend the bees are LPEL tasks.

When computing on N bees SaC actually creates only N-1 new bees (threads or tasks),
and then it uses the original "queen" bee to participate in the computation along with its peers.
Otherwise the queen bee would have to be blocked anyway and one more bee would have to be created.
Clemens Grelck did the experiments in his paper "Shared Memory Multiprocessor Support for Functional Array Processing in SAC".


Hive (de-)allocation
~~~~~~~~~~~~~~~~~~~~

.. code:: c

  SAChive *SAC_AllocHive( unsigned int num_bees,
                          int num_schedulers,
                          const int *places,
                          void *thdata);
  void SAC_ReleaseHive(SAChive *hive);

The function SAC_AllocHive() creates a bee-hive with the given number of bees.
This actually means creating ``(num_bees-1)`` bees (threads or tasks) because the calling bee is counted in as well.
The ``thdata`` argument is an opaque parameter to be passed unchanged into the lower-level LPEL or PThreads library call.

In the LPEL backend the ``places`` array should specify worker IDs for all tasks.
Note that ``places[0]`` corresponds to the calling task and hence actually won't be used.

The function SAC_ReleaseHive() should be used to free an unused hive.


Attaching/detaching hives to the bees
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code:: c

  void SAC_AttachHive(SAChive *hive);
  SAChive *SAC_DetachHive(void);

When a new hive is first created it exists as a group of bees and it is *not* actually related to the bee which created it.
To use the hive it must be first *attached* to the calling bee by the means of the SAC_AttachHive() function.
This function will create a *queen-bee* stub within the calling bee and associate it with the hive.
After attaching the hive the handle ``hive`` is conceptually no longer valid.

Conversely, a hive can be detached from the current bee context using the SAC_DetachHive() function.
The handle returned by the function can be either attached to a different queen-bee, or released.


Cleaning up
~~~~~~~~~~~

.. code:: c

  void SAC_ReleaseQueen(void);

As mentioned above when the hive is attached to the pristine bee the queen-bee stub is first created for the calling context.
However, this stub is *not* released when the hive is later detached, because it can be reused if a different hive is re-attached to the context.
But when the context is about to be destroyed (i.e. LPEL task or thread ends), the stub should be released by calling SAC_ReleaseQueen().

The PThreads and LPEL backends support an automatic cleanup.
If the queen stub is not released by the time the bee ends, it will be released automatically;
the cleanup code is called via the Thread Local Storage destructor facility.


Example
-------

A naive pseudo-code example of an S-Net task wrapper function:

.. code:: c

  void my_box_or_entity_fun(snet_handle_t *hnd, args...)
  {
    /* hnd->cont is a continuation info structure. */
    SAChive *hive = hnd->cont;
    
    if (hnd->mapping.is_new) {
      /* a request to remap/resize our hive */
      if (hive != NULL) {
        /* release the old hive */
        SAC_ReleaseHive(hive);
      }
      /* alloc a new hive */
      hive = SAC_AllocHive(hdn->mapping.num, 2,
                          hnd->mapping.int_names,   /* places */
                          hnd->ent->name);   /* opaque thdata */
    }
    
    SAC_AttachHive(hive);
    hnd->cont = hive = NULL;    /* handle no longer valid */

    /* call the SAC box function as usual */
    P = <receive params from SNet input stream>;
    my_sacfun(P);
    
    /* pick up my continuation */
    hive = SAC_DetachHive();
    
    if (hnd->terminating) {
      SAC_ReleaseHive(hive);
      SAC_ReleaseQueen();       /* not needed */
    } else {
      /* pass the hive as the continuation */
      hnd->cont = hive;
    }
  }

To summarise the example:

  * the S-Net continuation is a pointer to a hive.
  * when the whole box instance is destroyed, the continuation must be released by SAC_ReleaseHive().


LPEL extensions to support SaC
==============================

LPEL runtime enquiry
---------------------

.. code:: c

  int LpelTaskGetWorkerId(lpel_task_t *t);

Return the worker ID the given task is associated with.

.. code:: c

  int LpelWorkerCount(void);

Return the total number of workers in LPEL.

The functions LpelTaskGetWorkerId(), LpelWorkerCount() will be used only in the configuration of a standalone SaC program with MT via LPEL.
In that case SaC needs to invent a default placement of tasks on LPEL workers. 
For that it needs to know the current worker id and the total number of workers so that it can do a simple round-robin placement.
More technically speaking, the 'places' argument to SAC_AllocHive() is optional, 
and hence when it is NULL the function does a default round-robin placement starting at the current worker, then +1, times number of tasks, modulo worker count.


The Task Local Storage facility in LPEL
---------------------------------------

The Task Local Storage (TLS) facility in LPEL is the same concept as the 'Thread Local Storage' in PThreads.

S-Net note:
The 'Task Local Storage' is *not* a continuation. It will be used only within a task, but emptied when the task terminates via SAC_ReleaseQueen().
It is used to store a pointer to a bee structure, i.e. a queen-bee in a box-task and slave bees in the tasks created in SAC_AllocHive().

.. code:: c

  void  LpelSetUserData (lpel_task_t *t, void *data);
  void *LpelGetUserData (lpel_task_t *t);

The function LpelSetUserData() sets a user data pointer for the given task.
The function LpelGetUserData() returns the user data pointer of the given task.


.. code:: c

  typedef void (*lpel_usrdata_destructor_t)
                    (lpel_task_t *t, void *data);

  void LpelSetUserDataDestructor (lpel_task_t *t,
                                  lpel_usrdata_destructor_t destr);
  lpel_usrdata_destructor_t LpelGetUserDataDestructor 
                                  (lpel_task_t *t);

The function LpelSetUserDataDestructor() allows to register a destructor function for the value stored in the Task Local Storage (TLS).
The registered function will be executed in the task prior to the task's termination, but only if the TLS user data is not NULL.
The function can be used to release the data and perform any other necessary cleanups.


Binary semaphores
-----------------

Binary semaphores are used in SaC to synchronise data-parallel bees.

.. code:: c

  void LpelBiSemaInit (lpel_bisema_t *sem);
  void LpelBiSemaDestroy (lpel_bisema_t *sem);

Create/destroy a semaphore. Initially the semaphore is in a signalled (unlocked) state.

.. code:: c

  void LpelBiSemaWait (lpel_bisema_t *sem);
  void LpelBiSemaSignal (lpel_bisema_t *sem);

The LpelBiSemaWait() function waits on the semaphore until it is signalled; i.e. it locks it.
The LpelBiSemaSignal() function signals the semaphore; i.e. it unlocks it.
