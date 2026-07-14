# The WebAssembly build

The WebAssembly build is a self contained superbuild driven entirely by this repository's CMake
under the Emscripten toolchain. It fetches xerces, bpmn++, and the engine from their source
repositories into this project's build tree and cross compiles them there. Nothing outside the
repository is referenced, so the module is reproduced from nothing by a single build.

## How it is built

Configure and build with the Emscripten toolchain.

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm
```

This produces `build-wasm/bpmnos.js` and `build-wasm/bpmnos.wasm`.

xerces and bpmn++ are each cross compiled and installed into a staging prefix inside the build
tree, so that the engine discovers them exactly as it discovers installed libraries, through its
own find_library calls, without any change to the engine's build. The engine is then fetched into
the build tree and its two libraries are cross compiled against the staged dependencies. Finally the
bridge and the embind bindings are compiled and linked against those libraries into the module.

Exceptions are compiled in the native WebAssembly form, so a throw from the engine is caught at the
boundary and returned as an error rather than trapping the module. The engine compiles with its own
strict warnings on, because it supports the clang compiler that Emscripten uses. bpmn++ does not, so
its compile alone mutes warnings.

## No patches

No patch is applied to any fetched source. The engine cross compiles from its upstream source
unchanged. Three fixes that a cross compile once needed have all been made upstream: a clock mismatch
in the Metronome dispatcher, a parallel sort in a string utility that the Emscripten standard library
does not provide, and, most recently, a narrowing of the cnl include so that the engine no longer
pulls in a cnl header whose integer streaming does not compile under that standard library. Because
the engine now needs no change, the build tracks upstream and reproduces from nothing.

## The locale

The module selects a UTF-8 locale when it instantiates. Model attributes may contain multi byte
characters, for instance the set membership sign in a choice condition, and xerces transcodes such
an attribute to bytes through the runtime locale. Without a UTF-8 locale that transcoding drops the
whole value, which the engine then reports as a choice with neither an enumeration nor bounds.
Selecting a UTF-8 locale makes the transcoding preserve the characters.

## The interface

The module exposes the three bridge classes, the engine, the controller, and the monitor, through
embind. Every value that the C++ side expresses as JSON crosses the boundary as a JSON string, so
the caller constructs the objects, attaches the monitor and the controller to the engine, and drives
execution by passing and receiving JSON text. The Node tests under `test/wasm` load the module and
drive the same fixtures as the native tests, confirming that the engine executes inside WebAssembly.
