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

This writes `dist/bpmnos.mjs` and `dist/bpmnos.wasm`. A dependency update requires a clean `build-wasm`,
as the fetched dependencies are pinned on first configure.

The native build, for developing the bridge, expects the engine as a sibling checkout (override with
`BPMNOS_ENGINE_DIR`):

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The engine libraries carry the address, undefined, and leak sanitizers, so the bridge and tests link the
same way; clear `BPMNOS_SANITIZE` for a release engine.
