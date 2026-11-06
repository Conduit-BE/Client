// Exact-build, bounded observation of natural screen accessors. No NativeFunction,
// game writes, pointer scans, resource reloads, or control-tree traversal.
// Static evidence: docs/reverse-engineering/1.26.60-native-screen.md.
const image = Process.getModuleByName('Minecraft.Windows.exe');
const base = image.base;
const hooks = [];
let stopped = false, events = 0, visibleCalls = 0, nameCalls = 0;
const lastByThread = new Map();
const lastNameByThread = new Map();
const relative = p => p.compare(base) >= 0 && p.compare(base.add(image.size)) < 0 ? p.sub(base).toString() : null;
function readable(p, bytes) {
    if (p.isNull()) return false;
    const range = Process.findRangeByAddress(p);
    return range !== null && range.protection.includes('r') && p.add(bytes).compare(range.base.add(range.size)) <= 0;
}
function readPointer(p) {
    if (!readable(p, Process.pointerSize)) throw Error('Unreadable pointer slot');
    return p.readPointer();
}
function msvcString(p) {
    if (!readable(p, 32)) throw Error('Unreadable string object');
    const size = p.add(16).readU64(), capacity = p.add(24).readU64();
    if (size.compare(192) > 0 || capacity.compare(size) < 0 || capacity.compare(1048576) > 0) throw Error('Invalid/bounded string');
    const count = size.toNumber();
    if (!count) return '';
    const text = capacity.compare(16) < 0 ? p : readPointer(p);
    if (!readable(text, count + 1) || text.add(count).readU8() !== 0) throw Error('Unreadable string storage');
    return text.readUtf8String(count);
}
function topOfStack(stack) {
    if (!readPointer(stack).equals(base.add(0xf0829b0))) throw Error('Unexpected SceneStack table');
    const begin = readPointer(stack.add(0x18)), end = readPointer(stack.add(0x20));
    if (begin.equals(end)) return {stack: stack.toString(), empty: true};
    if (end.compare(begin) < 0 || end.sub(begin).compare(32 * 128) > 0) throw Error('Invalid/bounded scene vector');
    const span = end.sub(begin).toUInt32();
    if (span === 0 || span % 32 !== 0) throw Error('Invalid/bounded scene vector');
    const scene = readPointer(end.sub(24));
    if (scene.isNull()) return {stack: stack.toString(), empty: true};
    const table = readPointer(scene);
    const result = {stack: stack.toString(), depth: span / 32, scene: scene.toString(), tableRva: relative(table)};
    // This conditional is exact UIScene, not a guessed common layout for Ore UI.
    if (table.equals(base.add(0xf20ec60))) {
        const view = readPointer(scene.add(0x40));
        const tree = readPointer(view.add(0x50));
        if (!tree.isNull()) {
            const root = readPointer(tree.add(8));
            if (!root.isNull()) result.rootControlName = msvcString(root.add(0x20));
        }
    }
    return result;
}
function emit(kind, fields) {
    if (events >= 64 || stopped) return;
    ++events;
    send({kind, time: Date.now(), tid: Process.getCurrentThreadId(), ...fields});
}
function stop() {
    if (stopped) return;
    stopped = true;
    for (const hook of hooks) hook.detach();
    send({kind: 'screen-observation-ended', visibleCalls, nameCalls, events});
}
rpc.exports = {stop() {stop(); return true;}};
function attach(rva, expectedPrefix, callbacks) {
    const address = base.add(rva);
    const actual = [...new Uint8Array(address.readByteArray(expectedPrefix.length))];
    if (actual.some((value, index) => value !== expectedPrefix[index])) throw Error('Entry already hooked/incompatible: '+rva.toString(16));
    hooks.push(Interceptor.attach(address, callbacks));
}
try {
    attach(0x567520, [0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54], {
        onEnter(args) {
            ++visibleCalls;
            const tid = Process.getCurrentThreadId(), now = Date.now(), identity = tid+":"+args[0];
            const last = lastByThread.get(identity);
            if (last && now - last.sampled < 250) return;
            if (!last && lastByThread.size >= 8) return;
            try {
                const top = topOfStack(args[0]);
                const key = JSON.stringify(top);
                if (!last || key !== last.key) emit('screen-top-candidate', {callerRva: relative(this.returnAddress), ...top});
                lastByThread.set(identity, {sampled: now, key});
            } catch(e) {emit('screen-candidate-unavailable', {error: String(e)}); stop();}
        }
    });
    attach(0x56d000, [0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x41], {
        onEnter(args) {
            ++nameCalls;
            if (stopped || events >= 64) return;
            const tid = Process.getCurrentThreadId(), now = Date.now(), identity = tid+":"+args[0];
            const last = lastNameByThread.get(identity);
            if ((last && now-last.sampled < 250) || (!last && lastNameByThread.size >= 8)) return;
            this.identity = identity;
            this.output = args[1];
            try {this.top = topOfStack(args[0]);} catch(e) {this.error = String(e);}
            this.caller = relative(this.returnAddress);
        },
        onLeave() {
            if (stopped || events >= 64 || !this.output) return;
            try {
                if (this.error) throw Error(this.error);
                const fields = {...this.top, name: msvcString(this.output)};
                const key = JSON.stringify(fields), tid = Process.getCurrentThreadId();
                const last = lastNameByThread.get(this.identity);
                if (!last || key !== last.key) emit('screen-native-name-candidate', {callerRva: this.caller, ...fields});
                lastNameByThread.set(this.identity, {sampled: Date.now(), key});
            } catch(e) {emit('screen-name-unavailable', {error: String(e)});}
        }
    });
    send({kind: 'ready', observation: 'active-screen-candidates', durationMs: 60000, maximumEvents: 64});
    setTimeout(stop, 60000);
} catch(e) {stop(); throw e;}
