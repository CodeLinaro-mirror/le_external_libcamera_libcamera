.. SPDX-License-Identifier: CC-BY-SA-4.0

.. include:: ../documentation-contents.rst

Tracing Guide
=============

Guide to tracing in libcamera.

Profiling vs Tracing
--------------------

Tracing is recording timestamps at specific locations. libcamera provides a
tracing facility. This guide shows how to use this tracing facility.

Tracing should not be confused with profiling, which samples execution
at periodic points in time. This can be done with other tools such as
callgrind, perf, gprof, etc., without modification to the application,
and is out of scope for this guide.

Compiling
---------

To compile libcamera with tracing support, it must be enabled through the
meson ``tracing`` option. It depends on the lttng-ust library (available in the
``liblttng-ust-dev`` package for Debian-based distributions).
By default the tracing option in meson is set to ``auto``, so if
liblttng is detected, it will be enabled by default. Conversely, if the option
is set to disabled, then libcamera will be compiled without tracing support.

Defining tracepoints
--------------------

libcamera already contains a set of tracepoints. To define additional
tracepoints, create a file
``include/libcamera/internal/tracepoints/{file}.tp``, where ``file`` is a
reasonable name related to the category of tracepoints that you wish to
define. For example, the tracepoints file for the Request object is called
``request.tp``. An entry for this file must be added in
``include/libcamera/internal/tracepoints/meson.build``.

In this tracepoints file, define your tracepoints `as mandated by lttng
<https://lttng.org/man/3/lttng-ust>`_. The header boilerplate must *not* be
included (as it will conflict with the rest of our infrastructure), and
only the tracepoint definitions (with the ``TRACEPOINT_*`` macros) should be
included.

All tracepoint providers shall be ``libcamera``. According to lttng, the
tracepoint provider should be per-project; this is the rationale for this
decision. To group tracepoint events, we recommend using
``{class_name}_{tracepoint_name}``, for example, ``request_construct`` for a
tracepoint for the constructor of the Request class.

Tracepoint arguments may take C++ objects pointers, in which case the usual
C++ namespacing rules apply. The header that contains the necessary class
definitions must be included at the top of the tracepoint provider file.

Note: the final parameter in ``TP_ARGS`` *must not* have a trailing comma, and
the parameters to ``TP_FIELDS`` are *space-separated*. Not following these will
cause compilation errors.

Using tracepoints (in libcamera)
--------------------------------

To use tracepoints in libcamera, first the header needs to be included:

``#include "libcamera/internal/tracepoints.h"``

Then to use the tracepoint:

``LIBCAMERA_TRACEPOINT({tracepoint_event}, args...)``

This macro must be used, as opposed to lttng's macros directly, because
lttng is an optional dependency of libcamera, so the code must compile and run
even when lttng is not present or when tracing is disabled.

The tracepoint provider name, as declared in the tracepoint definition, is not
included in the parameters of the tracepoint.

There are also two special tracepoints available for tracing IPA calls:

``LIBCAMERA_TRACEPOINT_IPA_BEGIN({pipeline_name}, {ipa_function})``

``LIBCAMERA_TRACEPOINT_IPA_END({pipeline_name}, {ipa_function})``

These shall be placed where an IPA function is called from the pipeline handler,
and when the pipeline handler receives the corresponding response from the IPA,
respectively. These are the tracepoints that our sample analysis script
(see "Analyzing a trace") scans for when computing statistics on IPA call time.

Using tracepoints (from an application)
---------------------------------------

As applications are not part of libcamera, but rather users of libcamera,
applications should seek their own tracing mechanisms. For ease of tracing
the application alongside tracing libcamera, it is recommended to also
`use lttng <https://lttng.org/docs/#doc-tracing-your-own-user-application>`_.

Using tracepoints (from closed-source IPA)
------------------------------------------

Similar to applications, closed-source IPAs can simply use lttng on their own,
or any other tracing mechanism if desired.

Collecting a trace
------------------

To collect a trace from lttng, we first start by creating a session:

``lttng create $SESSION_NAME``

Next we enable events that we want to listen to. To enable the libcamera
events:

``lttng enable-event -u libcamera:\*``

We can also enable the events for
`function tracing <https://lttng.org/man/3/lttng-ust-cyg-profile>`_:

``lttng enable-event -u lttng_ust_cyg_profile:\*``

Or for the fast version:

``lttng enable-event -u lttng_ust_cyg_profile_fast:\*``

Note that function tracing requires libcamera to be built in either the
``debug`` or ``debugoptimized`` build types (which can be specified in
``meson`` with the ``--buildtype`` option).

Lastly, we can
`add context fields <https://lttng.org/man/1/lttng-add-context>`_
that allow the flame graph to work in tracecompass:

``lttng add-context -u -t vpid -t vtid -t procname``

Now we can start lttng:

``lttng start``

And run the libcamera application. For example:

``cam -c 1 --capture=10``

We also have the option to LD_PRELOAD the function tracer:

``LD_PRELOAD=/usr/lib/liblttng-ust-cyg-profile.so cam -c 1 --capture=10``

For for the fast function tracer:

``LD_PRELOAD=/usr/lib/liblttng-ust-cyg-profile-fast.so cam -c 1 --capture=10``

After running the application, we probably want to stop the tracing capture:

``lttng stop``

Optionally we can view the trace in text form:

``lttng view``

And finally we can destroy the session:

``lttng destroy $SESSION_NAME``

Tracing capture can be restarted without destruction, but it will end up in the
same log. The session can also be saved by ``lttng save`` before destruction,
and then loaded again by ``lttng load $SESSION_NAME``.

The location of the trace file is printed when running
``lttng create $SESSION_NAME``. After destroying the session, it can still be
viewed by: ``lttng view -t $PATH_TO_TRACE``, where ``$PATH_TO_TRACE`` is the
path that was printed when the session was created. This is the same path that
is used when analyzing traces programatically, as described in the next section.

See the `lttng documentation <https://lttng.org/docs/>`_ for further details.

Analyzing a trace
-----------------

As mentioned above, while an lttng tracing session exists and the trace is not
running, the trace output can be viewed as text by ``lttng view``.

The trace log can also be viewed as text using babeltrace2.  See the
`lttng trace analysis documentation
<https://lttng.org/docs/#doc-viewing-and-analyzing-your-traces-bt>`_
for further details.

babeltrace2 also has a C API and python bindings that can be used to process
traces. See the
`lttng python bindings documentation <https://babeltrace.org/docs/v2.0/python/bt2/>`_
and the
`lttng C API documentation <https://babeltrace.org/docs/v2.0/libbabeltrace2/>`_
for more details.

As an example, there is a script ``utils/tracepoints/analyze-ipa-trace.py``
that gathers statistics for the time taken for an IPA function call, by
measuring the time difference between pairs of events
``libcamera:ipa_call_start`` and ``libcamera:ipa_call_finish``.

Tracecompass
------------

`Tracecompass <https://eclipse.dev/tracecompass>`_
is an easy way to visualize and explore traces without having to first write a
script with babeltrace.

To open the trace, go through File -> "Open Trace...", then navigate into the
``lttng-traces`` directory and go into the directory of ``$SESSION_NAME`` from
above. Keep going down through ``ust`` and ``uid`` and keep going down until
you get to the channel files, and opening any of them should load the full
trace into tracecompass. It will take some time to load, especially the flame
graph (if applicable).

The flame graph loads symbol names from the current system's root directory,
and thus if the trace was generated on the same system as the system running
tracecompass, the symbol names should load fine. This will not be the case if
the trace is from another system.

In this case the easiest solution is the mount the root filesystem of the
system that the trace was gathered on, and the path to that system needs to be
configured. Click on the "Configure how addresses are mapped to function names"
in the flame graph view, and under the "LTTng" tab, select "Use custom target
root directory", and then "Browse" for the root filesystem of the target
system. For example, if the root filesystem of the device is mounted at
``/mnt/arm64``, then that would be the directory to open. Then the symbol names
in the flame graph should load correctly.

See the
`tracecompass lttng documentation <https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/LTTng-UST-Analyses.html>`_
for further details.
