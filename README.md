# bpmnos-wasm

[![wasm](https://github.com/bpmn-os/bpmnos-wasm/actions/workflows/build-wasm.yml/badge.svg)](https://github.com/bpmn-os/bpmnos-wasm/tree/dist/dist)
[![demo](https://github.com/bpmn-os/bpmnos-wasm/actions/workflows/pages.yml/badge.svg)](https://bpmn-os.github.io/bpmnos-wasm/)

Compiles the BPMNOS execution engine to WebAssembly and exposes a JavaScript interface that drives it. It
takes a BPMN model with its lookup tables and instance data, lets the caller act as the engine's
dispatcher, and reports the engine's token, event, message, and decision-request notifications.

The JavaScript API — the four classes, the drive loop, and the JSON shapes — is documented in
[`API.md`](API.md); the type declarations are in `types/bpmnos.d.ts`.

## Build

The WebAssembly module, the shipped artifact:

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm --target bpmnos_module
```

This writes `dist/bpmnos.mjs` and `dist/bpmnos.wasm`. The module is linked with JSPI, WebAssembly JavaScript
Promise Integration, which the host must support. The engine is fetched at the commit `BPMNOS_ENGINE_TAG`
names, a cache variable, so that after the pin is moved an existing build directory is either deleted or
reconfigured with `-DBPMNOS_ENGINE_TAG` set to the new commit; a dependency update requires a clean
`build-wasm` in any case, as the fetched dependencies are pinned on first configure.

The native build, for developing and testing the bridge, fetches the engine in the same way:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The engine libraries carry the address, undefined, and leak sanitizers, so the bridge and tests link the
same way; clear `BPMNOS_SANITIZE` for a release engine.
