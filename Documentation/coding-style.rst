.. SPDX-License-Identifier: CC-BY-SA-4.0

.. include:: documentation-contents.rst

.. _coding-style-guidelines:

Coding Style Guidelines
=======================

These coding guidelines are meant to ensure code quality. As a contributor
you are expected to follow them in all code submitted to the project. While
strict compliance is desired, exceptions are tolerated when justified with
good reasons. Please read the whole coding guidelines and use common sense
to decide when departing from them is appropriate.

libcamera is written in C++, a language that has seen many revisions and
offers an extensive set of features that are easy to abuse. These coding
guidelines establish the subset of C++ used by the project.


Coding Style
------------

Even if the programming language in use is different, the project embraces the
`Linux Kernel Coding Style`_ with a few exception and some C++ specificities.

.. _Linux Kernel Coding Style: https://www.kernel.org/doc/html/latest/process/coding-style.html

In particular, from the kernel style document, the following section are adopted:

* 1 "Indentation"
* 2 "Breaking Long Lines" striving to fit code within 80 columns and
  accepting up to 120 columns when necessary
* 3 "Placing Braces and Spaces"
* 3.1 "Spaces"
* 8 "Commenting" with the exception that in-function comments are not
  always un-welcome.

While libcamera uses the kernel coding style for all typographic matters, the
project is a user space library, developed in a different programming language,
and the kernel guidelines fall short for this use case.

For this reason, rules and guidelines from the `Google C++ Style Guide`_ have
been adopted as well as most coding principles specified therein, with a
few exceptions and relaxed limitations on some subjects.

.. _Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html

The following exceptions apply to the naming conventions specified in the
document:

* File names: libcamera uses the .cpp extensions for C++ source files and
  the .h extension for header files
* Variables, function parameters, function names and class members use
  camel case style, with the first letter in lower-case (as in 'camelCase'
  and not 'CamelCase')
* Types (classes, structs, type aliases, and type template parameters) use
  camel case, with the first letter in capital case (as in 'CamelCase' and
  not 'camelCase')
* Enum members use 'CamelCase', while macros are in capital case with
  underscores in between
* All formatting rules specified in the selected sections of the Linux kernel
  Code Style for indentation, braces, spacing, etc
* Headers are guarded by the use of '#pragma once'

Order of Includes
~~~~~~~~~~~~~~~~~

Headers shall be included at the beginning of .c, .cpp and .h files, right
after the file description comment block and, for .h files, the header guard
macro. For .cpp files, if the file implements an API declared in a header file,
that header file shall be included first in order to ensure it is
self-contained.

While the following list is extensive, it documents the expected behaviour
defined by the clang-format configuration and tooling should assist with
ordering.

The headers shall be grouped and ordered as follows:

1. The header declaring the API being implemented (if any)
2. The C and C++ system and standard library headers
3. Linux kernel headers
4. The libcamera base private header if required
5. The libcamera base library headers
6. The libcamera public API headers
7. The libcamera IPA interfaces
8. The internal libcamera headers
9. Other libraries' headers, with one group per library
10. Local headers grouped by subdirectory
11. Any local headers

Groups of headers shall be separated by a single blank line. Headers within
each group shall be sorted alphabetically.

System and library headers shall be included with angle brackets. Project
headers shall be included with angle brackets for the libcamera public API
headers, and with double quotes for internal libcamera headers.


C++ Specific Rules
------------------

The code shall be implemented in C++17, with the following caveats:

* Type inference (auto and decltype) shall be used with caution, to avoid
  drifting towards an untyped language.
* The explicit, override and final specifiers are to be used where applicable.
* Smart pointers, as well as shared pointers and weak pointers, shall not be
  overused.
* Classes are encouraged to define move constructors and assignment operators
  where applicable, and generally make use of the features offered by rvalue
  references.

Object Ownership
~~~~~~~~~~~~~~~~

libcamera creates and destroys many objects at runtime, for both objects
internal to the library and objects exposed to the user. To guarantee proper
operation without use after free, double free or memory leaks, knowing who owns
each object at any time is crucial. The project has enacted a set of rules to
make object ownership tracking as explicit and fool-proof as possible.

In the context of this section, the terms object and instance are used
interchangeably and both refer to an instance of a class. The term reference
refers to both C++ references and C++ pointers in their capacity to refer to an
object. Passing a reference means offering a way to a callee to obtain a
reference to an object that the caller has a valid reference to. Borrowing a
reference means using a reference passed by a caller without ownership transfer
based on the assumption that the caller guarantees the validity of the
reference for the duration of the operation that borrows it.

1. Single Owner Objects

   * By default an object has a single owner at any time.
   * Storage of single owner objects varies depending on how the object
     ownership will evolve through the lifetime of the object.

     * Objects whose ownership needs to be transferred shall be stored as
       std::unique_ptr<> as much as possible to emphasize the single ownership.
     * Objects whose owner doesn't change may be embedded in other objects, or
       stored as pointer or references. They may be stored as std::unique_ptr<>
       for automatic deletion if desired.

   * Ownership is transferred by passing the reference as a std::unique_ptr<>
     and using std::move(). After ownership transfer the former owner has no
     valid reference to the object anymore and shall not access it without first
     obtaining a valid reference.
   * Objects may be borrowed by passing an object reference from the owner to
     the borrower, providing that

     * the owner guarantees the validity of the reference for the whole duration
       of the borrowing, and
     * the borrower doesn't access the reference after the end of the borrowing.

     When borrowing from caller to callee for the duration of a function call,
     this implies that the callee shall not keep any stored reference after it
     returns. These rules apply to the callee and all the functions it calls,
     directly or indirectly.

     When the object is stored in a std::unique_ptr<>, borrowing passes a
     reference to the object, not to the std::unique_ptr<>, as

     * a 'const &' when the object doesn't need to be modified and may not be
       null.
     * a pointer when the object may be modified or may be null. Unless
       otherwise specified, pointers passed to functions are considered as
       borrowed references valid for the duration of the function only.

2. Shared Objects

   * Objects that may have multiple owners at a given time are called shared
     objects. They are reference-counted and live as long as any references to
     the object exist.
   * Shared objects are created with std::make_shared<> or
     std::allocate_shared<> and stored in an std::shared_ptr<>.
   * Ownership is shared by creating and passing copies of any valid
     std::shared_ptr<>. Ownership is released by destroying the corresponding
     std::shared_ptr<>.
   * When passed to a function, std::shared_ptr<> are always passed by value,
     never by reference. The caller can decide whether to transfer its ownership
     of the std::shared_ptr<> with std::move() or retain it. The callee shall
     use std::move() if it needs to store the shared pointer.
   * Do not over-use std::move(), as it may prevent copy-elision. In particular
     a function returning a std::shared_ptr<> value shall not use std::move() in
     its return statements, and its callers shall not wrap the function call
     with std::move().
   * Borrowed references to shared objects are passed as references to the
     objects themselves, not to the std::shared_ptr<>, with the same rules as
     for single owner objects.

These rules match the `object ownership rules from the Chromium C++ Style Guide`_.

.. _object ownership rules from the Chromium C++ Style Guide: https://chromium.googlesource.com/chromium/src/+/master/styleguide/c++/c++.md#object-ownership-and-calling-conventions

.. attention:: Long term borrowing of single owner objects is allowed. Example
   use cases are implementation of the singleton pattern (where the singleton
   guarantees the validity of the reference forever), or returning references
   to global objects whose lifetime matches the lifetime of the application. As
   long term borrowing isn't marked through language constructs, it shall be
   documented explicitly in details in the API.

Global Variables
~~~~~~~~~~~~~~~~

The order of initializations and destructions of global variables cannot be
reasonably controlled. This can cause problems (including segfaults) when global
variables depend on each other, directly or indirectly.  For example, if the
declaration of a global variable calls a constructor which uses another global
variable that hasn't been initialized yet, incorrect behavior is likely.
Similar issues may occur when the library is unloaded and global variables are
destroyed.

Global variables that are statically initialized and have trivial destructors
(such as an integer constant) do not cause any issue. Other global variables
shall be avoided when possible, but are allowed when required (for instance to
implement factories with auto-registration). They shall not depend on any other
global variable, should run a minimal amount of code in the constructor and
destructor, and code that contains dependencies should be moved to a later
point in time.

Error Handling
~~~~~~~~~~~~~~

Proper error handling is crucial to the stability of libcamera. The project
follows a set of high-level rules:

* Make errors impossible through API design. The best way to handle errors is
  to prevent them from happening in the first place. The preferred option is
  thus to prevent error conditions at the API design stage when possible.
* Detect errors at compile time. Compile-test checking of errors not only
  reduces the runtime complexity, but also ensures that errors are caught early
  on during development instead of during testing or, worse, in production. The
  static_assert() declaration should be used where possible for this purpose.
* Validate all external API contracts. Explicit pre-condition checks shall be
  used to validate API contracts. Whenever possible, appropriate errors should
  be returned directly. As libcamera doesn't use exceptions, errors detected in
  constructors shall result in the constructed object being marked as invalid,
  with a public member function available to check validity. The checks should
  be thorough for the public API, and may be lighter for internal APIs when
  pre-conditions can reasonably be considered to be met through other means.
* Use assertions for fatal issues only. The ASSERT() macro causes a program
  abort when compiled in debug mode, and is a no-op otherwise. It is useful to
  abort execution synchronously with the error check instead of letting the
  error cause problems (such as segmentation faults) later, and to provide a
  detailed backtrace. Assertions shall only be used to catch conditions that are
  never supposed to happen without a serious bug in libcamera that would prevent
  safe recovery. They shall never be used to validate API contracts. The
  assertion conditions shall not cause any side effect as they are compiled out
  in non-debug mode.

C Compatibility Headers
~~~~~~~~~~~~~~~~~~~~~~~

The C++ standard defines a set of C++ standard library headers, and for some of
them, defines C compatibility headers. The former have a name of the form
<cxxx> while the later are named <xxx.h>. The C++ headers declare names in the
std namespace, and may declare the same names in the global namespace. The C
compatibility headers declare names in the global namespace, and may declare
the same names in the std namespace. Code shall not rely on the optional
declaration of names in the global or std namespace.

Usage of the C compatibility headers is preferred, except for the math.h header.
Where math.h defines separate functions for different argument types (e.g.
abs(int), labs(long int), fabs(double) and fabsf(float)) and requires the
developer to pick the right function, cmath defines overloaded functions
(std::abs(int), std::abs(long int), std::abs(double) and std::abs(float) to let
the compiler select the right function. This avoids potential errors such as
calling abs(int) with a float argument, performing an unwanted implicit integer
conversion. For this reason, cmath is preferred over math.h.

Strings
~~~~~~~

This section focusses on strings as a sequence of characters represented by the
`char` type, as those are the only strings used in libcamera. The principles
are however equally applicable to other types of strings.

C++ includes multiple standard ways to represent and handle strings. Each of
them have pros and cons and different usage patterns.

1. C-style strings

   C-style strings are null-terminated arrays of characters of type `char`.
   They are represented by a `char *` pointing to the first character. C string
   literals (`"Hello world!"`) produce read-only global null-terminated arrays
   of type `const char`.

   Handling strings through `char *` relies on assumptions that are not
   enforced at compile time: the memory must not be freed as long as the
   pointer remains valid, and the string must be null-terminated. This causes a
   risk of use-after-free or buffer out-of-bounds reads. Furthermore, as the
   size of the underlying memory is not bundled with the pointer, handling
   writable strings as a `char *` risks buffer out-of-bounds writes.

2. `std::string` class

   The `std::string` class is the main C++ data type to represent strings. The
   class holds the string data as a null-terminated array of characters, as
   well as the length of the array. `std::string` literals (`"Hello world!"s`)
   converts the character array literal into a temporary `std::string` instance
   whose lifetime ends with the statement.

   Usage of `std::string` makes string handling safer and convenient thanks to
   the integration with the rest of the C++ standard library. This comes at a
   cost, as making copies of the class copies the underlying data, requiring
   dynamic allocation of memory. This is partially offset by usage of move
   constructors or assignment operators.

3. `std::string_view` class

   This newer addition to the C++ standard library is a non-owning reference to
   a characters array. Like the `std::string` class, the `std::string_view`
   stores a pointer to the array and a length, but unlike C-style strings and
   the `std::string` class, the array is not guaranteed to be null-terminated.
   `std::string_view` literals (`"Hello world!"sv`) create a temporary
   `std::string_view` instance whose lifetime ends with the statement, but the
   underlying character array has global static storage and lifetime.

   String views are useful to represent in a single way strings with
   heterogenous underlying storage, as they can be constructed from a `char *`
   or a `std::string`. They reduce the risk of buffer out-of-bounds accesses as
   they carry the array length, and they speed up handling of substrings as
   they don't cause copies, unlike `std::string`. As the string views store a
   borrowed reference to the underlying storage, they are prone to
   use-after-free issues. The lack of a null terminator furthermore makes them
   unsafe to handle strings that need to be passed to C functions that assume
   null-terminated strings (such as most of the C standard library functions).

   C++17 lacks many functions that make `std::string_view` usage convenient.
   This includes `operator+()` to concatenate a `std::string` and a
   `std::string_view`, as well as heterogenous lookup functions for containers
   (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2363r5.html).
   libcamera works around the former by defining the missing operators, and the
   latter by using the `find()` function instead of `at()` or `operator[]()`.
   Those issues are properly addressed in C++26.

With those pros, cons are caveats in mind, libcamera follows a set of rules
that governs selection of appropriate string types for function arguments:

* If the function is internal to a compilation unit, and all callers use the
  same string type for the function argument, use that type (by reference or
  value, as appropriate).
* If the function needs to modify the string, make a copy, or convert it to a
  `std::string` for any other reason, pass a `std::string` by value. Use
  `std::move()` in the caller if the string is not needed after the function
  call, as well as inside the function to move the string to a local copy if
  needed. This minimizes the number of copies in all use cases.
* If the function needs to pass the string to a C function that takes a
  `const char *`, and the caller may reasonably use a C string literal, pass a
  `const char *`. This avoids copies when the caller uses a `char *` pointer or
  a C string literal, and avoids buffer out-of-bound reads that could occur with
  a non null-terminated `std::string_view`.
* If the function only needs to pass the string to other functions that take a
  `const std::string &` reference, pass a `const std::string &`. This minimizes
  the construction of `std::string` instances when the caller already holds an
  instance.
* If the function only reads from the string, doesn't rely on it being
  null-terminated, and can reasonably be called with either C strings, C string
  literals, or `std::string` instances, pass a `std::string_view` instance. This
  improves safety of the code without impacting performance.
* Do not use `std::string_view` in public API function parameters. Previous
  rules showed that selection of the optimal string type for function
  parameters depends on the internal implementation of the function, which
  could change over time. This conflicts with the stability requirements of the
  libcamera public API. Furthermore, as C++17 makes `std::string_view` usage
  inconvenient in many cases, we don't want to force that inconvenience upon
  libcamera users.


Documentation
-------------

All public and protected classes, structures, enumerations, macros, functions
and variables shall be documented with a Doxygen comment block, using the
Javadoc style with C-style comments. When documenting private member functions
and variables the same Doxygen style shall be used as for public and protected
members.

Documentation relates to header files, but shall be stored in the .cpp source
files in order to group the implementation and documentation. Every documented
header file shall have a \file documentation block in the .cpp source file.

The following comment block shows an example of correct documentation for a
member function of the PipelineHandler class.

::

  /**
   * \fn PipelineHandler::start()
   * \brief Start capturing from a group of streams
   * \param[in] camera The camera to start
   *
   * Start the group of streams that have been configured for capture by
   * \a configureStreams(). The intended caller of this function is the Camera
   * class which will in turn be called from the application to indicate that
   * it has configured the streams and is ready to capture.
   *
   * \return 0 on success or a negative error code otherwise
   */

The comment block shall be placed right before the function it documents. If
the function is defined inline in the class definition in the header file, the
comment block shall be placed alone in the .cpp source file in the same order
as the function definitions in the header file and shall start with an \fn
line. Otherwise no \fn line shall be present.

The \brief directive shall be present. If the function takes parameters, \param
directives shall be present, with the appropriate [in], [out] or [inout]
specifiers. Only when the direction of the parameters isn't known (for instance
when defining a template function with variadic arguments) the direction
specifier shall be omitted. The \return directive shall be present when the
function returns a value, and shall be omitted otherwise.

The long description is optional. When present it shall be surrounded by empty
lines and may span multiple paragraphs. No blank lines shall otherwise be added
between the \fn, \brief, \param and \return directives.


Tools
-----

The 'clang-format' code formatting tool can be used to reformat source files
with the libcamera coding style, defined in the .clang-format file at the root
of the source tree.

As clang-format is a code formatter, it operates on full files and outputs
reformatted source code. While it can be used to reformat code before sending
patches, it may generate unrelated changes. To avoid this, libcamera provides a
'checkstyle.py' script wrapping the formatting tools to only retain related
changes. This should be used to validate modifications before submitting them
for review.

The script operates on one or multiple git commits specified on the command
line. It does not modify the git tree, the index or the working directory and
is thus safe to run at any point.

Commits are specified using the same revision range syntax as 'git log'. The
most usual use cases are to specify a single commit by sha1, branch name or tag
name, or a commit range with the <from>..<to> syntax. When no arguments are
given, the topmost commit of the current branch is selected.

::

	$ ./utils/checkstyle.py cc7d204b2c51
	----------------------------------------------------------------------------------
	cc7d204b2c51853f7d963d144f5944e209e7ea29 libcamera: Use the logger instead of cout
	----------------------------------------------------------------------------------
	No style issue detected

When operating on a range of commits, style checks are performed on each commit
from oldest to newest.

::

	$ ../utils/checkstyle.py 3b56ddaa96fb~3..3b56ddaa96fb
	----------------------------------------------------------------------------------
	b4351e1a6b83a9cfbfc331af3753602a02dbe062 libcamera: log: Fix Doxygen documentation
	----------------------------------------------------------------------------------
	No style issue detected
	
	--------------------------------------------------------------------------------------
	6ab3ff4501fcfa24db40fcccbce35bdded7cd4bc libcamera: log: Document the LogMessage class
	--------------------------------------------------------------------------------------
	No style issue detected
	
	---------------------------------------------------------------------------------
	3b56ddaa96fbccf4eada05d378ddaa1cb6209b57 build: Add 'std=c++11' cpp compiler flag
	---------------------------------------------------------------------------------
	Commit doesn't touch source files, skipping

Commits that do not touch any .c, .cpp or .h files are skipped.

::

	$ ./utils/checkstyle.py edbd2059d8a4
	----------------------------------------------------------------------
	edbd2059d8a4bd759302ada4368fa4055638fd7f libcamera: Add initial logger
	----------------------------------------------------------------------
	--- src/libcamera/include/log.h
	+++ src/libcamera/include/log.h
	@@ -21,11 +21,14 @@
	 {
	 public:
	        LogMessage(const char *fileName, unsigned int line,
	-                 LogSeverity severity);
	-       LogMessage(const LogMessage&) = delete;
	+                  LogSeverity severity);
	+       LogMessage(const LogMessage &) = delete;
	        ~LogMessage();
	 
	-       std::ostream& stream() { return msgStream; }
	+       std::ostream &stream()
	+       {
	+               return msgStream;
	+       }
	 
	 private:
	        std::ostringstream msgStream;
	 
	--- src/libcamera/log.cpp
	+++ src/libcamera/log.cpp
	@@ -42,7 +42,7 @@
	 
	 static const char *log_severity_name(LogSeverity severity)
	 {
	-       static const char * const names[] = {
	+       static const char *const names[] = {
	                "INFO",
	                "WARN",
	                " ERR",
	
	---
	2 potential style issues detected, please review

When potential style issues are detected, they are displayed in the form of a
diff that fixes the issues, on top of the corresponding commit. As the script is
in early development false positive are expected. The flagged issues should be
reviewed, but the diff doesn't need to be applied blindly.

Execution of checkstyle.py can be automated through git commit hooks. Example
of pre-commit and post-commit hooks are available in `utils/hooks/pre-commit`
and `utils/hooks/post-commit`. You can install either hook by copying it to
`.git/hooks/`. The post-commit hook is easier to start with as it will only flag
potential issues after committing, while the pre-commit hook will abort the
commit if issues are detected and requires usage of `git commit --no-verify` to
ignore false positives.

Happy hacking, libcamera awaits your patches!
