struct Params {
    width: u32,
    height: u32,
    cap: u32,
    pitch: u32,
}

struct Input {
    a: array<u32>,
}

struct Costs {
    costs: array<u32>,
}

struct Output {
    b: array<u32>,
}

struct Changed {
    changed: array<u32>,
}

struct Changed_1 {
    changed: array<atomic<u32>>,
}

var<private> gl_GlobalInvocationID_1: vec3<u32>;
@group(0) @binding(4)
var<storage> unnamed: Params;
@group(0) @binding(0)
var<storage> unnamed_1: Input;
@group(0) @binding(2)
var<storage> unnamed_2: Costs;
@group(0) @binding(1)
var<storage, read_write> unnamed_3: Output;
@group(0) @binding(3)
var<storage, read_write> unnamed_4: Changed_1;

fn wrap_u0028_i1_u003b_u1_u003b(x: ptr<function, i32>, size: ptr<function, u32>) -> u32 {
    var local: u32;

    let _e22 = (*x);
    if (_e22 < 0i) {
        let _e24 = (*size);
        let _e26 = (*x);
        let _e30 = (*size);
        local = ((_e24 - 1u) - (bitcast<u32>((-(_e26) - 1i)) % _e30));
    } else {
        let _e33 = (*x);
        let _e35 = (*size);
        local = (bitcast<u32>(_e33) % _e35);
    }
    let _e37 = local;
    return _e37;
}

fn main_1() {
    var i: u32;
    var value: u32;
    var old: u32;
    var x_1: i32;
    var y: i32;
    var threshold: i32;
    var dy: i32;
    var dx: i32;
    var k: u32;
    var param: i32;
    var param_1: u32;
    var param_2: i32;
    var param_3: u32;
    var slot: i32;
    var step: u32;
    var local_1: u32;
    var candidate: i32;

    let _e37 = gl_GlobalInvocationID_1[0u];
    i = _e37;
    let _e38 = i;
    let _e40 = unnamed.width;
    let _e42 = unnamed.height;
    if (_e38 >= (_e40 * _e42)) {
        return;
    }
    let _e45 = i;
    let _e48 = unnamed_1.a[_e45];
    value = _e48;
    let _e49 = value;
    old = _e49;
    let _e50 = value;
    if (_e50 != 0u) {
        let _e52 = i;
        let _e54 = unnamed.width;
        x_1 = bitcast<i32>((_e52 % _e54));
        let _e57 = i;
        let _e59 = unnamed.width;
        y = bitcast<i32>((_e57 / _e59));
        let _e63 = unnamed.cap;
        threshold = max(2i, (65535i - bitcast<i32>(_e63)));
        dy = -1i;
        loop {
            let _e67 = dy;
            if (_e67 <= 1i) {
                dx = -1i;
                loop {
                    let _e69 = dx;
                    if (_e69 <= 1i) {
                        let _e71 = dx;
                        let _e73 = dy;
                        if ((_e71 != 0i) || (_e73 != 0i)) {
                            let _e76 = y;
                            let _e77 = dy;
                            param = (_e76 + _e77);
                            let _e80 = unnamed.height;
                            param_1 = _e80;
                            let _e81 = wrap_u0028_i1_u003b_u1_u003b((&param), (&param_1));
                            let _e83 = unnamed.width;
                            let _e85 = x_1;
                            let _e86 = dx;
                            param_2 = (_e85 + _e86);
                            let _e89 = unnamed.width;
                            param_3 = _e89;
                            let _e90 = wrap_u0028_i1_u003b_u1_u003b((&param_2), (&param_3));
                            k = ((_e81 * _e83) + _e90);
                            let _e92 = dy;
                            let _e96 = dx;
                            slot = ((((1i - _e92) * 3i) + 1i) - _e96);
                            let _e98 = slot;
                            let _e101 = slot;
                            slot = (_e101 - select(0i, 1i, (_e98 > 4i)));
                            let _e104 = unnamed.width;
                            let _e106 = unnamed.height;
                            let _e108 = k;
                            let _e112 = unnamed_2.costs[((_e104 * _e106) + _e108)];
                            let _e113 = slot;
                            if ((_e112 & (1u << bitcast<u32>(_e113))) == 0u) {
                                continue;
                            }
                            let _e118 = dx;
                            let _e120 = dy;
                            if ((_e118 != 0i) && (_e120 != 0i)) {
                                let _e123 = k;
                                let _e126 = unnamed_2.costs[_e123];
                                local_1 = (_e126 >> bitcast<u32>(16i));
                            } else {
                                let _e129 = k;
                                let _e132 = unnamed_2.costs[_e129];
                                local_1 = (_e132 & 65535u);
                            }
                            let _e134 = local_1;
                            step = _e134;
                            let _e135 = k;
                            let _e138 = unnamed_1.a[_e135];
                            let _e140 = step;
                            candidate = (bitcast<i32>(_e138) - bitcast<i32>(_e140));
                            let _e143 = candidate;
                            let _e144 = threshold;
                            if (_e143 >= _e144) {
                                let _e146 = value;
                                let _e147 = candidate;
                                value = max(_e146, bitcast<u32>(_e147));
                            }
                        }
                        continue;
                    } else {
                        break;
                    }
                    continuing {
                        let _e150 = dx;
                        dx = (_e150 + 1i);
                    }
                }
                continue;
            } else {
                break;
            }
            continuing {
                let _e152 = dy;
                dy = (_e152 + 1i);
            }
        }
    }
    let _e154 = i;
    let _e155 = value;
    unnamed_3.b[_e154] = _e155;
    let _e158 = value;
    let _e159 = old;
    if (_e158 != _e159) {
        let _e163 = atomicOr((&unnamed_4.changed[0i]), 1u);
    }
    return;
}

@compute @workgroup_size(128, 1, 1)
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}
