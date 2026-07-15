# In-memory model and lookup inputs for the engine

> **Status: implemented and shipped** in the engine (`~/Code/bpmnos/engine`) and bpmn++
> (`~/Code/bpmnpp`), builds and tests passing. This document now records the interfaces as built; the
> `bpmnos-wasm` side (see the end) is the remaining follow-up.

This describes work in the engine repository (`~/Code/bpmnos/engine`) and the
bpmn++ repository (`~/Code/bpmnpp`). It adds in-memory entry points for the BPMN model and the lookup
tables so that a consumer can pass their contents directly rather than as files. The consumer that
needs this is `bpmnos-wasm`; that side of the work is not part of this work and is noted only for
context at the end.

## Purpose

The engine's model layer reads the BPMN model and the lookup tables from the filesystem. A consumer
that only has the contents in memory, such as a WebAssembly build receiving them across a language
boundary, must first write them to temporary files. The goal here is to let such a consumer pass the
contents directly. The instance data already has a string entry point (`instanceFileOrString`) and
needs no change; only the model and the lookup tables are file based today.

The model is handed over as an **already-parsed XML tree** (`XML::XMLObject`), not as XML text. The
consumer links schematic++/xerces already, so it parses its in-memory string into a tree itself with a
single `XML::XMLObject::createFromString(...)` call and passes the tree in. The lookup tables and the
instance stay as content strings (CSV), since they are not XML.

## Design decisions (already discussed and agreed)

1. A single input struct groups the three inputs at the data provider boundary. It lives in its own
   header, `model/data/src/Input.h`, so a consumer can fill it without pulling in the whole
   `DataProvider`/`Scenario` machinery:

   ```cpp
   namespace BPMNOS::Model {
     struct Input {
       std::unique_ptr<XML::XMLObject> model;                      // parsed BPMN tree (root)
       std::unordered_map<std::string, std::string> lookupTables;  // source file name ("costs.csv") -> CSV content
       std::string instance;                                       // instance CSV content
     };
   }
   ```

   The map is a plain lookup (`createLookupTable` only does `.find(source)`; it is never iterated in
   order), so `std::unordered_map` is used. The struct is move-only (it owns the tree). Pass it by value
   and move its members onward. Using a parsed `XML::XMLObject` for the model is a deliberately strong
   contract: a tree can only ever mean
   "a parsed model", never a filename and never content-still-to-be-parsed, so there is no same type
   overload where a `std::string` silently means a path in one place and content in another. This type
   is not new to the public surface — bpmn++ already returns `std::unique_ptr<XML::XMLObject>` from its
   `createRoot`. New fields can be added later without changing any signature.

2. The model-building pipeline is shared; only the per-source lookup resolution has two small modes.
   `readBPMNFile` -> `processRoot` -> process construction runs identically no matter how the model was
   supplied. The single point that differs is where `createLookupTable` gets a table's CSV: from the
   content map supplied in the `Input` (content mode), or via the existing folder search (file mode).
   Both are one-liners over the same `LookupTable`, so there is no duplicated wiring — and the file and
   folder path keeps its current lookup I/O untouched, which is what makes it 100% backward compatible.

3. Parsing and building are separated into honestly-named methods. Today one method, `readBPMNFile`,
   both parses the file and builds the object model, and the engine hooks the parse step (`createRoot`)
   to also build lookup tables, register limex callables, and build global variables. The refactor
   gives each responsibility its own name:
   - `createRoot(std::istream& stream)` — **parse**: XML text -> DOM tree (unchanged name, now from a
     stream). This is the only step that parses; it can fail on malformed XML.
   - `buildModel(std::unique_ptr<XML::XMLObject> root)` — **build**: DOM tree -> BPMN object model
     (processes, nodes, flows, links). It receives an already-parsed tree, so it never parses. This is
     the new primitive and the seam for the derived post-parse hook.
   - `readBPMNFile(const std::string& filename)` — the file convenience, **unchanged name and
     signature**: `buildModel(createRoot(stream))`.

   `buildModel` calls a new `processRoot()` hook after installing the tree and before building the
   processes; the engine overrides `processRoot()` (not `createRoot`) to do its lookup/globals setup.
   Because `readBPMNFile` keeps its name and `const std::string&` signature, the file path in both
   bpmn++ and the engine is untouched.

4. A lookup `source` is a bare filename, never a path. A shared traversal over the parsed tree
   enumerates the referenced lookups and validates this, throwing on any `source` that contains a path
   separator. This makes a map keyed by filename unambiguous and, because the referenced set is known
   up front, lets the folder adapter read only the referenced sources rather than every CSV in the
   folders. (All shipped example and test models already use bare filenames, so this rejects nothing
   that exists; it only forbids sub-path sources, which `openCsv` technically resolved before.)

5. The folder adapter preserves today's resolution exactly. For each referenced source it uses the
   existing `LookupTable::openCsv` order — the current working directory (or absolute path) first, then
   each folder with first-folder-wins precedence — so the file and folder constructors stay
   behaviourally identical, reading only the referenced files and producing the same errors as today.

## Layer 1: bpmn++ (`~/Code/bpmnpp`)

Today (`src/Model.cpp`): the constructor calls `readBPMNFile(filename)` (`:22`); `readBPMNFile` (`:25`)
calls `root = createRoot(filename)` and then builds the processes, child nodes, sequence flows,
message flows, and links; `createRoot` (`:46`) is `createFromFile(filename)`. `readBPMNFile`,
`createRoot`, and the default constructor `Model()` are `protected`; `root` is a public member.
schematic++ already provides `createFromStream`, `createFromString`, `createFromFile`
(`schematicpp/lib/XMLObject.h:124-142`).

Changes (`readBPMNFile` keeps its name and signature — it still reads a file — so nothing on the file
path breaks):

- `src/Model.h` (protected section) / `src/Model.cpp`: add the new build primitive
  `virtual void buildModel(std::unique_ptr<XML::XMLObject> root);`. Its body is the current
  `readBPMNFile` body with the parse line replaced: `this->root = std::move(root);`, then `processRoot();`,
  then the existing process/node/flow/link construction unchanged (it already reads from `this->root`).
- `src/Model.h` (protected section): add `virtual void processRoot() {}` — a no-op hook in the base,
  called by `buildModel` immediately after the tree is installed and before the processes are built.
  This is the seam derived classes use for post-parse setup.
- `src/Model.cpp:25`: `readBPMNFile(const std::string& filename)` keeps its signature; its body becomes
  an adapter — open an `std::ifstream` and `buildModel(createRoot(stream));` (throw if the stream fails
  to open, to preserve today's missing-file failure).
- `src/Model.h:181` / `src/Model.cpp:46`: change `createRoot` to parse from a stream —
  `std::unique_ptr<XML::XMLObject> createRoot(std::istream& stream);` implemented as
  `return std::unique_ptr<XML::XMLObject>(XML::XMLObject::createFromStream(stream));`. It no longer
  needs to be `virtual` (no derived class overrides it now); keep it `protected`.
- `src/Model.h:174` / `src/Model.cpp:20`: `Model(const std::string& filename)` is unchanged — it still
  calls `readBPMNFile(filename)`.
- `src/Model.h` / `src/Model.cpp`: add the primitive `Model(std::unique_ptr<XML::XMLObject> root)` that
  calls `buildModel(std::move(root))`.
- `src/Model.cpp`: add `#include <fstream>` for the `std::ifstream` in `readBPMNFile`.

## Layer 2: engine `BPMNOS::Model` and `LookupTable` (`engine/model/bpmnos/src`)

- `LookupTable.h:43`: add a content constructor beside the existing
  `LookupTable(const std::string& name, const std::string& source, const std::vector<std::string>& folders)`:

  ```cpp
  LookupTable(const std::string& name, const std::string& csvContent);
  ```

  It parses the CSV from the string with the same parsing the file path uses (`CSVReader` already
  accepts content or a filename, so this is `CSVReader(csvContent).read()` — no `std::filesystem`).

- Add a public `static std::vector<LookupRef> referencedLookups(const XML::XMLObject& root);` (with
  `struct LookupRef { std::string name; std::string source; };`) that walks the parsed tree — data
  store references -> `tExtensionElements` -> `tTables` -> `tTable` — and returns each table's `name`
  and `source`. It **borrows** the root (takes `const&`, does not consume it), so a consumer can call it
  for discovery and still move the same tree into an `Input`. It **throws** if any `source` contains a
  path separator (`/` or `\\`), enforcing the bare-filename contract. This is the enumeration a consumer
  uses to learn which lookup files to ask the user for.

- `Model.h:33` / `Model.cpp:65`: replace the `createRoot` override with a `processRoot() override`. Its
  body is the current override's body **minus** the `root = BPMN::Model::createRoot(...)` line (the tree
  is already installed by the base `buildModel`). Drive the lookup-table build from
  `referencedLookups(*this->root)` so the traversal and its filename validation are shared with the
  public finder above (rather than a second, divergent walk). For each returned lookup, call
  `createLookupTable`, register the limex callable, and then build the global variables from the
  collaboration attributes (`Model.cpp:91-98`). The engine no longer overrides `createRoot`.

- `Model.h:32` / `Model.cpp:59`: `createLookupTable` currently ends with
  `make_unique<LookupTable>(lookupName, source, folders)` (`Model.cpp:62`). Make it resolve the table's
  `source` per mode. Add a member `std::optional<std::unordered_map<std::string, std::string>> lookupContents;`
  set by the content constructor. When it holds a value (content mode), build
  `LookupTable(lookupName, lookupContents->at(source))` and throw a clear error when the source is
  missing — the same failure mode as a missing file today. Otherwise (file mode) keep the existing
  `LookupTable(lookupName, source, folders)`, i.e. the current `openCsv` search, **unchanged**. This
  two-line branch is the only difference between the paths; the file path's lookup I/O and errors are
  untouched.

- `Model.h:25`: remove the `const std::string filename;` member. It is write-only — assigned in the
  constructor (`Model.cpp:22`) and never read anywhere in the engine or any consumer — and the base
  class does not store it either. Removing it also removes the current move-then-reuse of the ctor
  parameter (`filename(std::move(filename))` at `:22` followed by `readBPMNFile(filename)` at `:26`).

- `Model.h:24` / `Model.cpp:21`:
  - Add the content constructor as the primitive:
    `Model(std::unique_ptr<XML::XMLObject> model, std::unordered_map<std::string, std::string> lookupTables)`.
    Set `lookupContents` to the map (so it holds a value — content mode) and initialize the existing
    members (`limexHandle`, `attributeRegistry(limexHandle)`) as today, then call the inherited
    `buildModel(std::move(model))`. That runs `processRoot()` (which reads `lookupContents`) and then
    the base process/node/flow construction. Setting `lookupContents` before `buildModel` is required,
    because `createLookupTable` runs inside `processRoot()`.
  - Keep `Model(const std::string filename, const std::vector<std::string> folders = {})` unchanged in
    behaviour: leave `lookupContents` empty (file mode), store `folders`, and call the inherited
    `readBPMNFile(filename)` exactly as today (which parses the file and builds the model via
    `buildModel`). `processRoot` then builds each referenced lookup via the existing
    `openCsv(source, folders)` search — no eager folder scan, and only the referenced files are read,
    exactly as today.

## Layer 3: engine data providers (`engine/model/data/src`)

The base `DataProvider` builds the model into `std::unique_ptr<Model> model` (`DataProvider.h`) and
then walks it to populate the `attributes` map. Add the content path here.

- Define `BPMNOS::Model::Input` in its own header, `model/data/src/Input.h` (added to the data header
  list before `DataProvider.h` for the single-header amalgamation). It needs only `<memory>`,
  `<string>`, `<unordered_map>`, and `bpmn++.h` (for `XML::XMLObject`). `DataProvider.h` includes it.
- Base: refactor `DataProvider` to a **private delegating constructor**
  `explicit DataProvider(std::unique_ptr<Model> model)` that holds the shared post-build work (the
  attribute-map population). Both public constructors just build the `Model` and delegate to it:
  - `DataProvider(const std::string& modelFile, const std::vector<std::string>& folders)`
    → `DataProvider(std::make_unique<Model>(modelFile, folders))`
  - `DataProvider(std::unique_ptr<XML::XMLObject> model, std::unordered_map<std::string, std::string> lookupTables)`
    → `DataProvider(std::make_unique<Model>(std::move(model), std::move(lookupTables)))`
  This keeps the attribute collection in one place with no `collectAttributes()` helper and no way to
  skip it.
- Add an `Input` constructor to each derived provider. The instance is read via `CSVReader`, which
  already accepts content or a filename, so `Input.instance` needs no new entry point:
  - `StaticDataProvider(Input input)` — public; also add a protected content constructor
    `StaticDataProvider(std::unique_ptr<XML::XMLObject> model, std::unordered_map<...> lookupTables)`
    (mirroring the existing protected `(modelFile, folders)` ctor) so derived classes can build the
    model without reading instances.
  - `ExpectedValueDataProvider(Input input)` — delegates to the `StaticDataProvider` protected content
    constructor (it derives from `StaticDataProvider`), then initializes its handle and reads instances.
  - `DynamicDataProvider(Input input)` — delegates to the base content constructor; `reader` is a value
    member built from `input.instance`.
  - `StochasticDataProvider(Input input, unsigned int seed = 0)` — delegates to the base content
    constructor and keeps the `seed` parameter and RNG setup.
- Keep the existing `(modelFile, folders, instanceFileOrString)` constructors unchanged. They stay file
  based and build the engine `Model` through its file/folder constructor.

## Backward compatibility

Keep every existing constructor working. The existing engine tests construct providers with the file
and folder forms and must pass unchanged. `readBPMNFile` keeps its name and `const std::string&`
signature (so the file path is untouched); the internal changes are: a new `buildModel(std::unique_ptr<
XML::XMLObject>)` primitive, a new `processRoot()` hook, `createRoot` moving to `std::istream&` and no
longer virtual, the engine's post-parse work moving from a `createRoot` override to a `processRoot()`
override, and the removal of the write-only `BPMNOS::Model::filename` member. No public constructor
signatures change.

## Acceptance criteria

1. All existing engine tests pass unchanged (file and folder forms build and run byte-for-byte as
   before — the file-mode lookup path is untouched).
2. A new test constructs a `StaticDataProvider` from an `Input` for the assignment problem, with the
   model parsed from XML via `XML::XMLObject::createFromString`, the one lookup table as
   `{"costs.csv": <csv content>}`, and the instance CSV as a string, and runs it to the same result as
   the file and folder form.
3. The content path creates no temporary files.
4. `referencedLookups` on the assignment-problem tree returns the expected `{name, source}` set (with
   `source == "costs.csv"`), and throws on a model whose lookup `source` contains a path separator.

## After this lands (bpmnos-wasm side, not this work)

The bridge's `Engine` will hold an `Input`. `loadModel` parses the incoming XML string into
`input.model` with `XML::XMLObject::createFromString(...)` (the bridge already links schematic++,
xerces, and bpmn++). It can then call `BPMNOS::Model::referencedLookups(*input.model)` to discover which
lookup files the model needs and surface those source names to the web user, who supplies each CSV; the
bridge stores them into `input.lookupTables` (`loadLookupTable`). `loadInstances` fills
`input.instance`. `build()` passes the `Input` to the provider, and `workDir`, `modelPath`,
`create_directories`, and `remove_all` are removed. The module will likely drop `-sFORCE_FILESYSTEM`.
This is done separately in the `bpmnos-wasm` repository once the interfaces above exist.

The intended UX flow: the web user selects a `.bpmn` file; the bridge parses it to a tree and calls
`referencedLookups` to enumerate the required lookup sources; it prompts the user for each; then it
builds the `Input` (moving in the already-parsed tree) and runs. No files, no re-parsing.
