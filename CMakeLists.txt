cmake_minimum_required(VERSION 3.26.4)

project(bpmnos_wasm CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ccache is chosen by whoever is building rather than imposed on a dependency, so it is set only when
# bpmnos-wasm is the project being built.
if(PROJECT_IS_TOP_LEVEL)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  endif()
endif()

# The engine is the only dependency this project names. bpmn++ and Xerces-C++ are the engine's, declared by
# it and fetched by it when a copy cannot be resolved, which holds for a cross compile as much as for a
# native one; neither is staged here any more.
set(BPMNOS_VERSION 0.1.0)
set(BPMNOS_ENGINE_REPO "https://github.com/bpmn-os/bpmnos-engine.git" CACHE STRING "engine repository")
set(BPMNOS_ENGINE_TAG  "2abbaf3dd4880c090df67e81b1461809b77dd1de" CACHE STRING "engine revision")

# The bridge library. Its sources are toolchain independent; the include paths and the way the
# engine is obtained differ between the native build and the WebAssembly build below.
add_library(bpmnos_wasm STATIC
  src/Input.cpp
  src/Structure.cpp
  src/Monitor.cpp
  src/EnqueuedEvents.cpp
  src/Controller.cpp
  src/Engine.cpp
)
target_include_directories(bpmnos_wasm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT EMSCRIPTEN)
  # ------------------------------------------------------------------------------------------
  # Native build. The engine is resolved from a prefix when one satisfies the requirement and built as a
  # subproject otherwise, exactly as the sibling bpmnos-rollout project does. This build is for developing
  # and testing the bridge; it produces no WebAssembly.
  # ------------------------------------------------------------------------------------------
  include(FetchContent)
  set(FETCHCONTENT_QUIET FALSE)
  FetchContent_Declare(bpmnos
    GIT_REPOSITORY ${BPMNOS_ENGINE_REPO}
    GIT_TAG        ${BPMNOS_ENGINE_TAG}
    FIND_PACKAGE_ARGS ${BPMNOS_VERSION}
  )
  FetchContent_MakeAvailable(bpmnos)

  # The engine's headers, those of the header-only libraries its amalgamated headers include, and bpmn++
  # with Xerces all arrive as usage requirements of the exported targets.
  target_link_libraries(bpmnos_wasm PUBLIC bpmnos::execution bpmnos::model)

  enable_testing()
  # Each native test drives a fixture through the bridge and takes the fixture directory as its
  # one argument, so a test does not depend on the working directory.
  foreach(scenario IN ITEMS timer choice structure sequential_entry message greedy advance optional)
    add_executable(native_${scenario}_test test/native/${scenario}_test.cpp)
    target_link_libraries(native_${scenario}_test PRIVATE bpmnos_wasm pthread)
    add_test(NAME ${scenario} COMMAND native_${scenario}_test ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures)
  endforeach()

else()
  # ------------------------------------------------------------------------------------------
  # WebAssembly build. The engine is fetched and cross compiled here, and it brings bpmn++, which brings
  # Xerces-C++: none of the three is staged through ExternalProject any more, the engine and bpmn++ now
  # declaring what they need and fetching it when it cannot be resolved. schematic++ is the exception that
  # stays a host build, being a code generator that has to run during this configure rather than in a
  # browser; the engine builds it in a sub-build of its own that inherits no toolchain file, so it is native
  # without being asked. Exceptions use the native WebAssembly form so a throw from the engine is caught at
  # the boundary rather than trapping.
  # ------------------------------------------------------------------------------------------
  include(FetchContent)
  set(FETCHCONTENT_QUIET FALSE)

  # Applied to everything compiled here, the fetched dependencies included, so that the engine's archives
  # and the bridge agree on how a throw crosses a function boundary.
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)

  # Xerces-C++ is fetched by bpmn++ rather than staged here, but its build has to be told what a browser
  # offers: no threads, no separate message catalogue, and the iconv transcoder that Emscripten provides.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(threads OFF CACHE BOOL "" FORCE)
  set(transcoder iconv CACHE STRING "" FORCE)
  set(message-loader inmemory CACHE STRING "" FORCE)

  # No FIND_PACKAGE_ARGS: a copy installed on this machine is a native one and could not be linked into a
  # WebAssembly module, so the engine is always fetched and cross compiled here.
  FetchContent_Declare(bpmnos
    GIT_REPOSITORY ${BPMNOS_ENGINE_REPO}
    GIT_TAG        ${BPMNOS_ENGINE_TAG}
  )
  FetchContent_MakeAvailable(bpmnos)

  target_link_libraries(bpmnos_wasm PUBLIC bpmnos::execution bpmnos::model)

  # The WebAssembly module: the embind entry linked against the bridge, which carries the engine with it.
  add_executable(bpmnos_module src/wasm/Bindings.cpp)
  target_link_libraries(bpmnos_module PRIVATE bpmnos_wasm)
  # Emit an ES module whose default export is the factory, so a bundler can import it and resolve
  # bpmnos.wasm through import.meta.url. The module is written into dist, which is what the package
  # ships, the demo copies, and the Node tests import.
  target_link_options(bpmnos_module PRIVATE
    -lembind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createBPMNOS
    -sALLOW_MEMORY_GROWTH=1
  )
  set_target_properties(bpmnos_module PROPERTIES
    OUTPUT_NAME "bpmnos"
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
  )
endif()
