// node web/tests/test_pm_protocol.js
const assert = require('assert');
const PM = require('../pm_protocol.js');

assert.strictEqual(PM.crc16('123456789'), 0x29B1);
assert.strictEqual(PM.frame('PING n=1'), '$PING n=1*' + PM.crc16('PING n=1').toString(16).toUpperCase().padStart(4, '0') + '\n');
assert.throws(() => PM.frame('bad $'));
assert.throws(() => PM.frame('x'.repeat(401)));

const p = new PM.Parser();
const good = PM.frame('PONG n=1');
assert.deepStrictEqual(p.feed('junk' + good.slice(0, 4)).concat(p.feed(good.slice(4))), ['PONG n=1']);
const bad = good.slice(0, -3) + (good[good.length - 3] === 'A' ? 'B' : 'A') + good.slice(-2);
assert.deepStrictEqual(p.feed(bad), []);
assert.strictEqual(p.bad, 1);
assert.deepStrictEqual(p.feed('$ST x=1$' + good.slice(1)), ['PONG n=1']);
assert.strictEqual(p.bad, 2);

const f = PM.fields('PMENT gen=1 slot=3 id=reverb-fx name=Ambient%20Reverb cat=effect ver=0.3.1 hash=' + 'B'.repeat(64));
assert.strictEqual(f._type, 'PMENT');
assert.strictEqual(f.slot, '3');
assert.strictEqual(PM.decodeName(f.name), 'Ambient Reverb');
assert.strictEqual(PM.decodeName('Toy%20Piano%20%C3%A4'), 'Toy Piano ä');
assert.deepStrictEqual(PM.slotsOf('0005'), [1, 3]);
assert.deepStrictEqual(PM.slotsOf('0000'), []);
assert.deepStrictEqual(PM.slotsOf('7FFF').length, 15);
assert.ok(PM.isManager(PM.fields('HELLO proto=1 fw=0.1.0 app=DaisyProgramManager dev=seed3-desktop')));
assert.ok(PM.isManager(PM.fields('HELLO proto=1 app=DaisyProgramManagerProbe')));
assert.ok(!PM.isManager(PM.fields('HELLO proto=1 app=DaisyDiagnostic')));
assert.ok(!PM.isManager(PM.fields('HELLO proto=2 app=DaisyProgramManager')));
assert.strictEqual(PM.startBody({ slot: 3, id: 'reverb-fx', hash: 'b'.repeat(64) }, 4, 99),
  'PMSTART slot=3 id=reverb-fx hash=' + 'B'.repeat(32) + ' gen=4 rid=99');
assert.throws(() => PM.startBody({ slot: 1, id: 'x', hash: 'abc' }, 1, 1));
console.log('test_pm_protocol: OK');
