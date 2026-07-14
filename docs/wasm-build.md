# The WebAssembly build

This note records what is required to compile the bridge and the engine it depends on to
WebAssembly, and what currently stands in the way. It reflects an investigation carried out
with Emscripten 6.0.3 against the engine's amalgamated headers. The findings are factual and
the plan follows from them.

## What already holds

The bridge sources themselves are compatible with Emscripten. Compiling a bridge translation
unit with `em++` and the C++23 standard produces no error that originates in the bridge. Every
error observed originates in a third party header that the engine's amalgamated headers include,
where Emscripten's `libc++` differs from the `libstdc++` used by the native build. This means the
boundary code, the handle registry, the monitor, and the controller need no change to target the
web; the work is confined to the dependencies and the link.

## What stands in the way

There are two source level incompatibilities in header only dependencies and two libraries that
must be cross compiled.

The `cnl` fixed point library streams a number by calling its internal `to_chars_natural` with the
begin and end iterators of a local `std::array` of characters. Under `libstdc++` the iterator of a
`std::array` is a raw character pointer, which is exactly what `to_chars_natural` accepts, so the
native build compiles. Under `libc++` the same iterator is a wrapper type rather than a raw
pointer, and no conversion exists, so the call fails to resolve. The fix is to pass raw pointers,
for example the array's data pointer and that pointer advanced by the array's size, in the fetched
copy of `cnl`.

The `strutil` string utility sorts with the parallel execution policy `std::execution::par_unseq`.
The `libc++` shipped with Emscripten does not implement the parallel algorithms, so the policy is
undefined. The fix is to drop the execution policy argument in the fetched copy of `strutil`, which
leaves an ordinary sequential sort.

Xerces C++ and bpmn++ must be built for WebAssembly. The bridge reaches xerces only indirectly,
because `bpmn++.h` includes `<xercesc/dom/DOM.hpp>`, so the xerces headers are needed to compile
and the xerces library is needed to link. Xerces is the larger of the two efforts, since it carries
platform specific facilities for file access, transcoding, and threading that must either build
under Emscripten or be configured out. The bpmn++ library is small and depends only on xerces, so it
follows once xerces is in place.

The schematic++ code generator does not need to be ported. It is a build time tool that reads the
BPMNOS schema and emits parser sources, and it runs on the host. Only the sources it emits are
compiled for WebAssembly, and those compile like any other engine source.

## The plan

The WebAssembly build fetches and patches its own copies of the header only dependencies rather than
reusing the engine's, so the native engine build is left untouched. It cross compiles xerces and
then bpmn++, keeps schematic++ as a native tool, builds the engine's model and execution libraries
for WebAssembly with the patched dependencies, and links the bridge together with the embind
bindings. Exceptions are compiled in native WebAssembly form with the same flag across every object,
so a throw from the engine crosses the boundary as a caught error rather than a trap. Inputs reach
the engine through the in memory file system, exactly as the native build writes them to a temporary
directory, so no engine code that reads a model or a data file needs to change.

## State

The embind bindings and the Emscripten path in the build description are written and ready. The two
dependency patches and the xerces and bpmn++ cross compilation remain, and until they are done the
link cannot complete. This branch therefore holds the WebAssembly scaffolding but not yet a built
module, and it is deliberately kept separate from the integration branch until it produces one.
