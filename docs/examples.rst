..
    Copyright (c) The Einsums Developers. All rights reserved.
    Licensed under the MIT License. See LICENSE.txt in the project root.

.. _examples:

========
Examples
========

These pages embed the real, buildable projects under ``examples/`` in the
repository — the same files the CI builds — so nothing here can drift from
working code.

The greeter example
===================

``examples/greeter/`` is a self-contained downstream project that consumes
apiary the way any library would: ``find_package(Apiary)`` plus the ``apiary_*``
CMake helpers, with no Einsums coupling. It binds a header-only C++ library — a
class with a renamed Python type, two constructors, a method, a getter/setter
property, and a free function — into an importable ``greeter`` extension.

1. Annotate the C++
-------------------

The only apiary-specific things in the header are the ``APIARY_*`` macros from
``<apiary/Annotations.hpp>``. They expand to nothing in a normal compile; the
apiary tool reads them when it walks the AST. ``APIARY_EXPOSE`` marks what to
bind, ``APIARY_RENAME`` sets the Python name, and an ``APIARY_GETTER`` /
``APIARY_SETTER`` pair becomes a single read/write property.

.. literalinclude:: ../examples/greeter/include/greeter/Greeter.hpp
   :language: cpp
   :lines: 6-
   :caption: examples/greeter/include/greeter/Greeter.hpp

2. Supply the module body
-------------------------

You write only the ``PYBIND11_MODULE`` shell. ``apiary_aggregate_extension``
generates ``<greeter/Modules.hpp>`` (which declares ``greeter_register_all()``
and each module's register function); the entry point includes it and calls the
aggregator.

.. literalinclude:: ../examples/greeter/src/module.cpp
   :language: cpp
   :lines: 6-
   :caption: examples/greeter/src/module.cpp

3. Wire it up in CMake
----------------------

Three helpers do the work: :ref:`apiary_detect_toolchain <cmake_api>` probes the
compiler for the include paths libtooling needs, ``apiary_add_bindings``
generates the binding translation unit (plus a ``.pyi`` stub and docs JSON), and
``apiary_aggregate_extension`` assembles everything into one extension. Linking
``apiary::annotations`` puts the macro header on the include path.

.. literalinclude:: ../examples/greeter/CMakeLists.txt
   :language: cmake
   :lines: 16-
   :caption: examples/greeter/CMakeLists.txt

4. Use it from Python
---------------------

The renamed class, the property synthesized from the getter/setter pair, and the
free function are all just ordinary Python:

.. literalinclude:: ../examples/greeter/test_greeter.py
   :language: python
   :lines: 7-
   :caption: examples/greeter/test_greeter.py

Build and run
-------------

With an installed Apiary on ``CMAKE_PREFIX_PATH`` (or swap the ``find_package``
for an ``add_subdirectory`` of the apiary tree):

.. code-block:: console

   $ cmake -S examples/greeter -B examples/greeter/build \
       -DCMAKE_PREFIX_PATH=<apiary-install-prefix>
   $ cmake --build examples/greeter/build
   $ PYTHONPATH=examples/greeter/build python3 examples/greeter/test_greeter.py
   greeter example: OK
