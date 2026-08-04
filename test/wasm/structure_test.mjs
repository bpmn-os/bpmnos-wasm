// WebAssembly model description test. What the model resolves is asked of the module without a run, an
// engine or instance data, which is what a caller replaying a recorded log has. This mirrors the native
// test through the module's own interface.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import createBPMNOS from '../../dist/bpmnos.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');

function check(condition, message) {
  if (!condition) {
    console.error(`FAIL: ${message}`);
    process.exit(1);
  }
  console.error(`ok: ${message}`);
}

const module = await createBPMNOS();

const describe = (fixture) => JSON.parse(module.describeModel(JSON.stringify({
  model: readFileSync(join(root, 'test', 'fixtures', fixture), 'utf8')
})));

const own = describe('AdHocSubProcess.bpmn');
check(own.sequentialPerformers.length === 1, 'one performer is reported');
check(own.sequentialPerformers[0].performer === 'AdHocSubProcess_1', 'which is the subprocess itself');
check(own.sequentialPerformers[0].activities.join() === 'Activity_1,Activity_2',
  'performing both of its activities, named as the model names them');

const enclosing = describe('SequentialPerformer.bpmn');
check(enclosing.sequentialPerformers.length === 1, 'one performer is reported for an explicit role');
check(enclosing.sequentialPerformers[0].performer === 'Process_1', 'which is the process carrying it');

const none = describe('Timer.bpmn');
check(none.sequentialPerformers.length === 0, 'a model without a sequential ad hoc subprocess reports none');

const broken = JSON.parse(module.describeModel(JSON.stringify({ model: 'not a model' })));
check('error' in broken, 'a model that cannot be parsed is reported as an error');

const withoutModel = JSON.parse(module.describeModel(JSON.stringify({})));
check('error' in withoutModel, 'and so is a description naming no model');

console.error('ALL PASSED (WebAssembly model description)');
