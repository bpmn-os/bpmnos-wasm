cmake_minimum_required(VERSION 3.26.4)

project(bpmnos_wasm CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cache compilation across rebuilds when ccache is present; set before any fetch so it also
# covers the fetched engine and its dependencies.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Repositories the build fetches from. Everything is fetched into this project's build tree;
# nothing outside the repository is referenced. The engine now cross compiles unchanged, so no
# patch is applied to any fetched source.
set(BPMNOS_ENGINE_REPO "https://github.com/bpmn-os/bpmnos-engine.git" CACHE STRING "engine repository")
set(BPMNOS_ENGINE_TAG  "main" CACHE STRING "engine tag")
set(BPMNPP_REPO  "https://github.com/bpmn-os/bpmnpp.git" CACHE STRING "bpmn++ repository")
set(BPMNPP_TAG   "main" CACHE STRING "bpmn++ tag")
set(XERCES_REPO  "https://github.com/apache/xerces-c.git" CACHE STRING "xerces-c repository")
set(XERCES_TAG   "v3.2.4" CACHE STRING "xerces-c tag")

# The bridge library. Its sources are toolchain independent; the include paths and the way the
# engine is obtained differ between the native build and the WebAssembly build below.
add_library(bpmnos_wasm STATIC
  src/Monitor.cpp
  src/Controller.cpp
  src/Engine.cpp
)
target_include_directories(bpmnos_wasm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT EMSCRIPTEN)
  # ------------------------------------------------------------------------------------------
  # Native build. The engine is fetched and built as a subproject, exactly as the sibling
  # bpmnos-rollout project does; xerces and bpmn++ are taken from the system. This build is for
  # developing and testing the bridge; it produces no WebAssembly.
  # ------------------------------------------------------------------------------------------
  include(FetchContent)
  set(FETCHCONTENT_QUIET FALSE)
  set(BPMNOS_TESTS OFF CACHE BOOL "Disable bpmnos engine tests")
  FetchContent_Declare(bpmnos GIT_REPOSITORY ${BPMNOS_ENGINE_REPO} GIT_TAG ${BPMNOS_ENGINE_TAG})
  FetchContent_MakeAvailable(bpmnos)

  target_include_directories(bpmnos_wasm SYSTEM PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/bpmnos-src/lib
    ${CMAKE_BINARY_DIR}/_deps/cnl-src/include
    ${CMAKE_BINARY_DIR}/_deps/json-src
    ${CMAKE_BINARY_DIR}/_deps/strutil-src
    ${CMAKE_BINARY_DIR}/_deps/limex-src
  )
  add_dependencies(bpmnos_wasm model_header execution_header)

  add_executable(native_drive_test test/native/drive_test.cpp)
  target_link_libraries(native_drive_test PRIVATE
    bpmnos_wasm bpmnos-execution bpmnos-model bpmn++ xerces-c pthread)

  add_executable(native_message_test test/native/message_test.cpp)
  target_link_libraries(native_message_test PRIVATE
    bpmnos_wasm bpmnos-execution bpmnos-model bpmn++ xerces-c pthread)

  enable_testing()
  add_test(NAME drive COMMAND native_drive_test ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures)
  add_test(NAME message COMMAND native_message_test ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures)

else()
  # ------------------------------------------------------------------------------------------
  # WebAssembly build. xerces, bpmn++, and the engine are each fetched from source and cross
  # compiled here; nothing is patched, because the fixes that a cross compile once needed are now
  # upstream in the engine. xerces and bpmn++ install into a staging prefix in the build tree so
  # the engine finds them exactly as it finds installed libraries. Exceptions use the native
  # WebAssembly form so a throw from the engine is caught at the boundary rather than trapping.
  # ------------------------------------------------------------------------------------------
  include(ExternalProject)

  set(STAGE ${CMAKE_BINARY_DIR}/stage)
  set(ENGINE_SRC ${CMAKE_BINARY_DIR}/_deps/bpmnos-src)
  set(EP_TOOLCHAIN -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
  # The engine supports the clang compiler that Emscripten uses, so it compiles with its own
  # warnings on. bpmn++ does not, so its compile mutes warnings.
  set(WASM_ENGINE_FLAGS "-fwasm-exceptions")
  set(WASM_DEP_FLAGS "-fwasm-exceptions -w")

  # xerces-c cross compiled and installed into the staging prefix.
  ExternalProject_Add(ep_xerces
    GIT_REPOSITORY ${XERCES_REPO} GIT_TAG ${XERCES_TAG}
    UPDATE_DISCONNECTED TRUE
    CMAKE_ARGS ${EP_TOOLCHAIN}
      -DCMAKE_INSTALL_PREFIX=${STAGE}
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
      -Dthreads=OFF
      -Dtranscoder=iconv
      -Dmessage-loader=inmemory
    BUILD_BYPRODUCTS ${STAGE}/lib/libxerces-c.a
  )

  # bpmn++ cross compiled against the staged xerces and installed into the staging prefix.
  ExternalProject_Add(ep_bpmnpp
    GIT_REPOSITORY ${BPMNPP_REPO} GIT_TAG ${BPMNPP_TAG}
    UPDATE_DISCONNECTED TRUE
    DEPENDS ep_xerces
    CMAKE_ARGS ${EP_TOOLCHAIN}
      -DCMAKE_INSTALL_PREFIX=${STAGE}
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_FIND_ROOT_PATH=${STAGE}
      -DXERCES_LIB=${STAGE}/lib/libxerces-c.a
      "-DCMAKE_CXX_FLAGS=-isystem ${STAGE}/include ${WASM_DEP_FLAGS}"
    BUILD_BYPRODUCTS ${STAGE}/lib/libbpmn++.a
  )

  # The engine, fetched into _deps and cross compiled unchanged. Only the two libraries are built.
  ExternalProject_Add(ep_engine
    GIT_REPOSITORY ${BPMNOS_ENGINE_REPO} GIT_TAG ${BPMNOS_ENGINE_TAG}
    SOURCE_DIR ${ENGINE_SRC}
    UPDATE_DISCONNECTED TRUE
    DEPENDS ep_bpmnpp
    CMAKE_ARGS ${EP_TOOLCHAIN}
      -DNDEBUG=ON
      -DBPMNOS_TESTS=OFF
      -DCMAKE_FIND_ROOT_PATH=${STAGE}
      -DXERCES_LIB=${STAGE}/lib/libxerces-c.a
      -DBPMNPP_LIB=${STAGE}/lib/libbpmn++.a
      "-DCMAKE_CXX_FLAGS=-isystem ${STAGE}/include ${WASM_ENGINE_FLAGS}"
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target model_header execution_header bpmnos-model bpmnos-execution
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${ENGINE_SRC}/lib/libbpmnos-model.a ${ENGINE_SRC}/lib/libbpmnos-execution.a
  )
  ExternalProject_Get_Property(ep_engine BINARY_DIR)

  # The bridge compiles against the engine's single headers and the header-only dependencies.
  target_compile_options(bpmnos_wasm PUBLIC -fwasm-exceptions)
  target_include_directories(bpmnos_wasm SYSTEM PUBLIC
    ${ENGINE_SRC}/lib
    ${BINARY_DIR}/_deps/cnl-src/include
    ${BINARY_DIR}/_deps/limex-src
    ${BINARY_DIR}/_deps/json-src
    ${STAGE}/include
  )
  add_dependencies(bpmnos_wasm ep_engine)

  # The WebAssembly module: the embind entry linked against the bridge and the staged libraries.
  add_executable(bpmnos_module src/Bindings.cpp)
  add_dependencies(bpmnos_module ep_engine)
  target_link_libraries(bpmnos_module PRIVATE
    bpmnos_wasm
    ${ENGINE_SRC}/lib/libbpmnos-execution.a
    ${ENGINE_SRC}/lib/libbpmnos-model.a
    ${STAGE}/lib/libbpmn++.a
    ${STAGE}/lib/libxerces-c.a
  )
  target_link_options(bpmnos_module PRIVATE
    -fwasm-exceptions
    -lembind
    -sMODULARIZE=1
    -sEXPORT_NAME=createBpmnos
    -sALLOW_MEMORY_GROWTH=1
    -sFORCE_FILESYSTEM=1
  )
  set_target_properties(bpmnos_module PROPERTIES OUTPUT_NAME "bpmnos" SUFFIX ".js")
endif()
