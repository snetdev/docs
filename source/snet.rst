===================================
 Quick technical intro about S-Net
===================================

:Authors: knz
:Date: May 2012

Background
==========

Main S-Net project site: http://www.snet-home.org/ -- contains:

  - the **S-Net Language Report**, which serves as:

    - a specification for the S-Net coordination language
    - a high-level overview of the various implementation strategies for S-Net

  - links to academic publications and S-Net related events

Development site: http://snetdev.github.com -- contains:

  - "How to build" guide

  - links to the software components and source repositories

Key concepts
============

From the language report:

  *S-Net is a coordination language and component technology for the
  era of multi-core and many-core computing. It turns functional code
  in conventional languages into asynchronous components that interact
  with each other via a streaming network. The specification of these
  networks, a core feature of S-Net, follows an algebraic approach:
  only four different network combinators allow us the concise
  specification of complex streaming networks through a simple
  expression language. Routing of data packages is defined via a
  record type system with structural subtyping and (flow)
  inheritance.*

From a technical perspective we distinguish the following concepts:

- the S-Net **language**, specified by the language report. The
  language is used by a "concurrency engineer" to specify how processing
  components relate to each other to form a complete application. 

- the S-Net **boxes**, which implement the "bricks" used by S-Net
  programmers.

- the S-Net **run-time system**, which coordinates the execution of
  boxes over input data according to the rules in a S-Net program.

- the S-Net **execution platform**, which provides hardware access to
  computing resources to run S-Net programs.

Overview of the S-Net language
==============================

S-Net is an abstract language. This is an example S-Net program::
   
    net example { 
        box foo ((a,b)->(c,d)); 
        box bar ((c)->(e)); 
    } connect foo..bar;

This example expresses that:

- there are two processing components (boxes) called ``foo`` and
  ``bar``;
- ``foo`` accepts records each with two values ``(a,b)``, and
  produces records each with two values ``(c,d)``;
- ``bar`` accepts records each with one value ``(e)`` and produces
  records each with one value ``(e)``;
- the S-Net component ``example`` is composed of the two boxes
  ``foo`` and ``bar`` connected in sequence.

S-Net further defines operators that cause the network of components
to **expand dynamically** at run-time. For example::

    net example { 
        box foo ((a)->(b)); 
    } connect foo!<x>;

This network indicates that the box ``foo`` will be replicated, at
run-time, for each value of the input tag ``<x>`` (a tag is a special
integer field in data records that can be inspected by a S-Net
program).

Overview of the S-Net boxes
===========================

The S-Net programmer works together with "box programmers"
who provide the actual processing components, in the example above
the concrete implementation for ``foo`` and ``bar``. 

The contract is that the S-Net programmer only has to know the
*external interface* of the box code, and does not need to know (in
too much detail) what the box actually does.

This is an example box function in C:

.. code:: c

   /* bar computes the sine of the value given as input */
   int bar(snet_dispatch_t *hnd,  double c)
   {
        snet_out(hnd, sin(c));
   }

Boxes can be implemented in a variety of technologies, and S-Net can
mix and match boxes from different programming languages. For
example, the earliest implementations already supported C and SAC boxes.

Overview of the S-Net run-time system
=====================================

The S-Net run-time system (RTS) is in charge of reading input data
from "outside" (eg files, network) and feed it into the network of
boxes described by the S-Net program. It also *coordinates* the
execution of boxes according to the rules of S-Net.

As such the RTS can be seen as an *interpreter* for the S-Net language.

There are multiple possible *evaluation strategies* for an S-Net program:

- **processes and streams**, which is the original idea to implement
  S-Net and described in the language report. 

  In this vision, each component in a S-Net program is implemented as
  a task and each S-Net stream is implemented as a channel. Each task
  repeatedly reads a record from its input channel(s), performs the
  processing of the component, then writes the produced record(s) to
  the output channel(s).

  Also, when the network of components defined by a S-Net program
  expands at run-time, the number of tasks and channels increases
  accordingly.

  This implementation is said to be "process-centric" in that the components
  mostly stay at the same location while the data moves around.

- **Hydra**, details of which can be found in [PH10]_.

  In this vision, the entire S-Net program is encoded in a
  function. For every input record, one new task is created to execute
  the entire S-Net program over that input. If no more tasks can be
  created, the implementation waits until a previous input record has
  been processed then reuses its task for the next input record.

  Here, when the network of components defined by a S-Net program
  expands at run-time, this increases the number of recursion levels
  in the function application in each tasks. The maximum number of
  tasks can be configured independently from the structure of the
  S-Net program.

  This implementation is said to be "data-centric" in that the input
  records mostly stay at the same location while the computation
  stages are applied to them.

  .. [PH10] Philip Kaj Ferdinand Hölzenspies. On run-time
     exploitation of concurrency. PhD thesis, University of Twente,
     Enschede, the Netherlands, April 2010. URL
     http://doc.utwente.nl/70959/.

- **graph walker**, of which an outline can be found in [JS08]_.

  In this vision, the S-Net program is stored in memory as a graph of
  components. For every input record, new tasks are created for each
  node in the graph to process that input record, with
  1-shot synchronization between tasks for communication instead of streams.

  Here again, although there are no streams, the number of tasks
  increases with the dynamic expansion of the S-Net program.

  This implementation is also "data-centric".

  .. [JS08] Chris Jesshope and Alex Shafarenko. Concurrency
     Engineering. In Proc. 13th IEEE Asia-Pacific Computer Systems
     Architecture Conference, 2008. ISBN 978-1-4244-2683-6.


Overview of the S-Net execution platforms
=========================================

The S-Net RTS executes S-Net programs using available parallelism on
the underlying platform. The different RTS evaluation strategies
(outlined above) require different services from the environment:

- the "processes and streams" approach requires task parallelism and a
  stream abstraction. Moreover, it requires the environment to support
  as many tasks and streams as described in the dynamic expansion of a
  S-Net program.

  Here the implementation uses two platforms: plain POSIX threads (one
  pthread per task) and a "Lightweight Parallel Execution Layer"
  (LPEL) providing tasks and workers, where multiple tasks are
  multiplexed over a single system thread.

- the "Hydra" approach requires the least services from the
  environment, as it could run the entire S-Net program within a
  single task. Moreover it can adapt dynamically to any additional
  available parallelism.

  There is no preferred platform support for Hydra, although it would
  work on the same platforms as "processes and streams" above.

- the "graph walker" approach requires task parallelism with very low
  task management overheads (many tasks are created and removed
  dynamically), and point-to-point dataflow synchronizers.

  This approach requires support for at least as many tasks as the
  dynamic expansion of the S-Net program, and can also use more tasks
  to process multiple input records in parallel.  The preferred
  platform for this strategy would be hardware optimized for
  fine-grained dataflow processing, such as the Microgrid_ platform.
  
  .. _Microgrid: http://svp-home.org/microgrids
