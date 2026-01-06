.. SPDX-License-Identifier: CC-BY-SA-4.0

Design of the metadata list
===========================

This document explains the design and rationale of the :doxy-int:`MetadataList` type.

Description of the problem
--------------------------

Early metadata
^^^^^^^^^^^^^^

A pipeline handler might report numerous metadata items to the application about
a single request. It is likely that different metadata items become available at
different points in time while a request is being processed.

Simultaneously, an application might desire to carry out potentially non-trivial
extra processing on the image, etc. using certain metadata items. For such an
application it is likely best if the value of each metadata item is reported as
soon as possible, thus allowing it to start processing as soon as possible.

For this reason, libcamera provides the ``Camera::metadataAvailable`` signal.
This signal is dispatched whenever new metadata items become available for a
queued request. This mechanism is completely optional, only interested applications
need to subscribe, others are free to ignore it completely. :doxy-int:`Request::metadata`
will contain the sum of all early metadata items at request completion.

Thread safety
^^^^^^^^^^^^^

The application and libcamera are operating in separate threads. This means that while
a request is being processed, accessing the request's metadata list brings up the
question of concurrent access. Previously, the metadata list was implemented using a
:doxy-int:`ControlList`, which uses ``std::unordered_map`` as its backing storage. That type
does not provide strong thread-safety guarantees. As a consequence, accessing the
metadata list was only allowed in certain contexts:

1. before request submission
2. after request completion
3. in libcamera signal handler

Contexts (1) and (2) are most likely assumed (and expected) by users of libcamera, and
they are not too interesting because they do not overlap with request processing, where a
pipeline handler could be modifying the list in parallel.

Context (3) is merely an implementation detail of the libcamera signal-slot event handling
mechanism (libcamera users cannot use the asynchronous event delivery mechanism).

Naturally, in a context where accessing the metadata list is safe, querying the metadata
items of interest and storing them in an application specific manner is a good and safe
approach. However, in (3) keeping the libcamera internal thread blocked for too long
will have detrimental effects, so processing must be kept to a minimum.

As a consequence, if an application is unable to query the metadata items of interest
in a safe context (potentially because it does not know) but wants delegate work (that
needs access to metadata) to separate worker threads, it is forced to create a copy of
the entire metadata list, which is hardly optimal. The introduction of early metadata
completion only makes it worse (due to potentially multiple completion events).

Requirements
------------

We wish to provide a simple, easy-to-use, and hard-to-misuse interface for
applications. Notably, applications should be able to perform early metadata
processing safely wrt. any concurrent modifications libcamera might perform.

Secondly, efficiency should be considered: copies, locks, reference counting,
etc. should be avoided if possible.

Preferably, it should be possible to refer to a contiguous (in insertion order)
subset of values reasonably efficiently so that applications can be notified
about the just inserted metadata items without creating separate data structures
(i.e. a set of numeric ids).

Options
-------

Several options have been considered for making use of already existing mechanisms,
to avoid the introduction of a new type. These all fell short of some or all of the
requirements proposed above. Some ideas that use the existing ``ControlList`` type
are discussed below.

Send a copy
^^^^^^^^^^^

Passing a separate ``ControlList`` containing the just completed metadata, and
disallowing access to the request's metadata list until completion works fine, and
avoids the synchronization issues on the libcamera side. Nonetheless, it has two
significant drawbacks:

1. It moves the issue of synchronization from libcamera to the application: the
   application still has to access its own data in a thread-safe manner and/or
   transfer the partial metadata list to its intended thread of execution.
2. The metadata list may contain potentially large data, copying which may be
   a non-negligible performance cost (especially if it does not even end up needed).

Keep using ``ControlList``
^^^^^^^^^^^^^^^^^^^^^^^^^^

Using a ``ControlList`` (and hence ``std::unordered_map``) with early metadata completion
would be possible, but it would place a number of potentially non-intuitive and
easy to violate restrictions on applications, making it harder to use safely.
Specifically, the application would have to retrieve a pointer to the :doxy-int:`ControlValue`
object in the metadata ``ControlList``, and then access it only through that pointer.
(This is guaranteed to work since ``std::unordered_map`` provides pointer stability
wrt. insertions.)

However, it wouldn't be able to do lookups on the metadata list outside the event
handler, thus a pointer or iterator to every potentially accessed metadata item
has to be retrieved and saved in the event handler. Additionally, the usual way
of retrieving metadata using the pre-defined ``Control<T>`` objects would no longer
be possible, losing type-safety. (Although the ``ControlValue`` type could be extended
to support that.)

Design
------

A separate data structure is introduced to contain the metadata items pertaining
to a given request. It is referred to as "metadata list" from now on.

A metadata list is backed by a pre-allocated (at construction time) contiguous
block of memory sized appropriately to contain all possible metadata items. This
means that the number and size of metadata items that a camera can report must
be known in advance. The newly introduced ``MetadataListPlan`` type is used for
that purpose. At the time of writing this does not appear to be a significant
limitation since most metadata has a fixed size, and each pipeline handler (and
IPA) has a fixed set of metadata that it can report. There are, however, metadata
items that have a variably-sized array type. In those cases an upper bound on the
number of elements must be provided.

``MetadataListPlan``
^^^^^^^^^^^^^^^^^^^^

A :doxy-int:`MetadataListPlan` collects the set of possible metadata items. It maps the
numeric id of the control to a collection of static information (size, etc.). This
is most importantly used to calculate the size required to store all possible
metadata items.

Each camera has its own ``MetadataListPlan`` object similarly to its ``ControlInfoMap``.
It is used to create requests for the camera with an appropriately sized ``MetadataList``.
Pipeline handlers should fill it during camera initialization or configuration,
and they are allowed to modify it before and during camera configuration.

``MetadataList``
^^^^^^^^^^^^^^^^

The current metadata list implementation is a single-writer multiple-readers
thread-safe data structure that provides lock-free lookup and access for any number
of threads, while allowing a single thread at a time to add metadata items.

The implemented metadata list has two main parts. The first part essentially
contains a copy of the ``MetadataListPlan`` used to construct the ``MetadataList``. In
addition to the static information about the metadata item, it contains dynamic
information such as whether the metadata item has been added to the list or not.
These entries are sorted by the numeric identifier to facilitate faster lookup.

The second part of a metadata list is a completely self-contained serialized list
of metadata items. The number of bytes used for actually storing metadata items
in this second part will be referred to as the "fill level" from now on. The
self-contained nature of the second part leads to a certain level of data duplication
between the two parts, however, the end goal is to have a serialized version of
``ControlList`` with the same serialized format. This would allow a ``MetadataList``
to be "trivially" reinterpreted as a control list at any point of its lifetime,
simplifying the interoperability between the two.
TODO: do we really want that?

A metadata list, at construction time, calculates the number of bytes necessary to
store all possible metadata items according to the supplied ``MetadataListPlan``.
Storage, for all possible metadata items and the necessary auxiliary structures,
is then allocated. This allocation remains fixed for the entire lifetime of a
``MetadataList``, which is crucial to satisfy the earlier requirements.

Each metadata item can only be added to a metadata list once. This constraint
does not pose a significant limitation, instead, it simplifies the interface and
implementation; it is essentially an append-only list.

Serialization
'''''''''''''

The actual values are encoded in the "second part" of the metadata list in a fairly
simple fashion. Each control value is encoded as header + data bytes + padding.
Each value has a header, which contains information such as the size, alignment,
type, etc. of the value. The data bytes are aligned to the alignment specified
in the header, and padding may be inserted after the last data byte to guarantee
proper alignment for the next header. Padding is present even after the last entry.

The minimum amount of state needed to describe such a serialized list of values is
merely the number of bytes used. This can reasonably be limited to 4 GiB, meaning
that a 32-bit unsigned integer is sufficient to store the fill level. This makes
it possible to easily update the state in a wait-free fashion.

Lookup
''''''

Lookup in a metadata list is done using the metadata entries in the "first part".
These entries are sorted by their numeric identifiers, hence binary search is
used to find the appropriate entry. Then, it is checked whether the given control
id has already been added, and if it has, then its data can be returned in a
:doxy-int:`ControlValueView` object.

Insertion
'''''''''

Similarly to lookup, insertion also starts with binary searching the entry
corresponding to the given numeric identifier. If an entry is present for the
given id and no value has already been stored with that id, then insertion can
proceed. The value is appended to the serialized list of control values according
to the format described earlier. Then the fill level is atomically incremented,
and the entry is marked as set. After that the new value is available for readers
to consume.

Having a single writer is an essential requirement to be able to carry out insertion
in a reasonably efficient, and thread-safe manner.

Iteration
'''''''''

Iteration of a ``MetadataList`` is carried out only using the serialized list of
controls in the "second part" of the data structure. An iterator can be implemented
as a single pointer, pointing to the header of the current entry. The begin iterator
simply points to location of the header of the first value. The end iterator is
simply the end of the serialized list of values, which can be calculated from the
begin iterator and the fill level of the serialized list.

The above iterator can model a `C++ forward iterator`_, that is, only increments
of 1 are possible in constant time, and going backwards is not possible. Advancing
to the next value can be simply implemented by reading the size and alignment from
the header, and adjusting the iterator's pointer by the necessary amount.

.. _C++ forward iterator: https://en.cppreference.com/w/cpp/iterator/forward_iterator.html

Clearing
''''''''

Removing a single value is not supported, but clearing the entire metadata list
is. This should only be done when there are no readers, otherwise readers might
run into data races if they keep reading the metadata when new entries are being
added after clearing it.

Clearing is implemented by resetting each metadata entry in the "first part", as
well as resetting the stored fill level of the serialized buffer to 0.

Partial view
''''''''''''

When multiple metadata items are completed early, it is important to provide a way
for the application to know exactly which metadata items have just been added. The
serialized values in the data structure are laid out sequentially. This makes it
possible for a simple byte range to denote a range of metadata items. Hence the
completed metadata items can be transferred to the application as a simple byte
range, without needing extra data structures (such as a set of numeric ids).

The :doxy-int:`MetadataList::Checkpoint` type is used to store the state of the
serialized list (number of bytes and number of items) at a given point in time.
From such a checkpoint object a :doxy-int:`MetadataList::Diff` object can be
constructed, potentially at a later time, after some items have been added. Both
types represent are view-like non-owning references into the backing ``MetadataList``,
and are invalidated when that is destroyed or cleared. A ``MetadataList::Diff``
represents all values added since the checkpoint. This *diff* object is reasonably small,
trivially copyable, making it easy to provide to the application; it provides thread-safe
access to the data. It has much of the same features as a ``MetadataList``, e.g. it can
be iterated and one can do lookups. Naturally, both iteration and lookups only consider
the values added after the checkpoint and before the creation of the ``MetadataList::Diff`` object.
