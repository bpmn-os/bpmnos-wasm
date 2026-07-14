cmake_minimum_required(VERSION 3.20)

project(bpmnos_wasm CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Location of the prebuilt BPMN-OS engine. The bridge consumes the engine's amalgamated
# public headers and its static libraries; it never rebuilds or modifies the engine. The
# default assumes the engine is a sibling checkout.
set(BPMNOS_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../engine" CACHE PATH "Path to the bpmnos engine checkout")

# The amalgamated headers pull in a handful of third-party headers through angle-bracket
# includes; these are the directories where the engine's own build placed them.
set(BPMNOS_ENGINE_INCLUDE_DIRS
  ${BPMNOS_ENGINE_DIR}/lib
  ${BPMNOS_ENGINE_DIR}/build/_deps/cnl-src/include
  ${BPMNOS_ENGINE_DIR}/build/_deps/limex-src
  ${BPMNOS_ENGINE_DIR}/build/_deps/json-src
  ${BPMNOS_ENGINE_DIR}/build/_deps/strutil-src
  /usr/local/include
)

add_library(bpmnos_wasm STATIC
  src/Monitor.cpp
  src/Controller.cpp
  src/Engine.cpp
)
target_include_directories(bpmnos_wasm PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${BPMNOS_ENGINE_INCLUDE_DIRS}
)

# Native drive test. Built only for a native toolchain; the wasm build has its own harness.
if(NOT EMSCRIPTEN)
  # The prebuilt engine archives are compiled with address, undefined, and leak sanitizers,
  # so the bridge and the test must be built and linked the same way to resolve the sanitizer
  # runtime and to keep instrumentation consistent. Set to empty to link against a release
  # build of the engine instead.
  set(BPMNOS_SANITIZE "address,undefined,leak" CACHE STRING "Sanitizers matching the prebuilt engine (empty to disable)")
  if(BPMNOS_SANITIZE)
    target_compile_options(bpmnos_wasm PUBLIC -fsanitize=${BPMNOS_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(bpmnos_wasm PUBLIC -fsanitize=${BPMNOS_SANITIZE})
  endif()

  # The engine archives are placed in a linker group because the model and execution
  # libraries reference each other.
  add_executable(native_drive_test test/native/drive_test.cpp)
  target_link_libraries(native_drive_test PRIVATE
    bpmnos_wasm
    -Wl,--start-group
    ${BPMNOS_ENGINE_DIR}/lib/libbpmnos-execution.a
    ${BPMNOS_ENGINE_DIR}/lib/libbpmnos-model.a
    /usr/local/lib/libbpmn++.a
    -Wl,--end-group
    xerces-c
    pthread
  )
endif()

# WebAssembly module. Built only under the Emscripten toolchain. This path is scaffolding: it
# requires the engine and its dependencies cross-compiled to wasm, which is not yet in place;
# see docs/wasm-build.md for the remaining dependency work. Exceptions use the native
# WebAssembly form so an engine throw is caught at the boundary rather than trapping.
if(EMSCRIPTEN)
  target_compile_options(bpmnos_wasm PUBLIC -fwasm-exceptions)
  target_link_options(bpmnos_wasm PUBLIC -fwasm-exceptions)

  # The wasm-compiled engine, bpmn++, and xerces archives, produced by the dependency build.
  # Empty until that build exists; the module cannot link without them.
  set(BPMNOS_WASM_ENGINE_LIBS "" CACHE STRING "wasm-compiled engine and dependency archives")

  add_executable(bpmnos_module src/Bindings.cpp)
  target_link_libraries(bpmnos_module PRIVATE bpmnos_wasm ${BPMNOS_WASM_ENGINE_LIBS})
  target_link_options(bpmnos_module PRIVATE
    -lembind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sALLOW_MEMORY_GROWTH=1
  )
  set_target_properties(bpmnos_module PROPERTIES OUTPUT_NAME "bpmnos" SUFFIX ".js")
endif()
