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

    let _e20 = (*x);
    if (_e20 < 0i) {
        let _e22 = (*size);
        let _e24 = (*x);
        let _e28 = (*size);
        local = ((_e22 - 1u) - (bitcast<u32>((-(_e24) - 1i)) % _e28));
    } else {
        let _e31 = (*x);
        let _e33 = (*size);
        local = (bitcast<u32>(_e31) % _e33);
    }
    let _e35 = local;
    return _e35;
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
    var step: u32;
    var local_1: u32;
    var candidate: i32;

    let _e34 = gl_GlobalInvocationID_1[0u];
    i = _e34;
    let _e35 = i;
    let _e37 = unnamed.width;
    let _e39 = unnamed.height;
    if (_e35 >= (_e37 * _e39)) {
        return;
    }
    let _e42 = i;
    let _e45 = unnamed_1.a[_e42];
    value = _e45;
    let _e46 = value;
    old = _e46;
    let _e47 = value;
    if (_e47 != 0u) {
        let _e49 = i;
        let _e51 = unnamed.width;
        x_1 = bitcast<i32>((_e49 % _e51));
        let _e54 = i;
        let _e56 = unnamed.width;
        y = bitcast<i32>((_e54 / _e56));
        let _e60 = unnamed.cap;
        threshold = max(2i, (65535i - bitcast<i32>(_e60)));
        dy = -1i;
        loop {
            let _e64 = dy;
            if (_e64 <= 1i) {
                dx = -1i;
                loop {
                    let _e66 = dx;
                    if (_e66 <= 1i) {
                        let _e68 = dx;
                        let _e70 = dy;
                        if ((_e68 != 0i) || (_e70 != 0i)) {
                            let _e73 = y;
                            let _e74 = dy;
                            param = (_e73 + _e74);
                            let _e77 = unnamed.height;
                            param_1 = _e77;
                            let _e78 = wrap_u0028_i1_u003b_u1_u003b((&param), (&param_1));
                            let _e80 = unnamed.width;
                            let _e82 = x_1;
                            let _e83 = dx;
                            param_2 = (_e82 + _e83);
                            let _e86 = unnamed.width;
                            param_3 = _e86;
                            let _e87 = wrap_u0028_i1_u003b_u1_u003b((&param_2), (&param_3));
                            k = ((_e78 * _e80) + _e87);
                            let _e89 = dx;
                            let _e91 = dy;
                            if ((_e89 != 0i) && (_e91 != 0i)) {
                                let _e94 = k;
                                let _e97 = unnamed_2.costs[_e94];
                                local_1 = (_e97 >> bitcast<u32>(16i));
                            } else {
                                let _e100 = k;
                                let _e103 = unnamed_2.costs[_e100];
                                local_1 = (_e103 & 65535u);
                            }
                            let _e105 = local_1;
                            step = _e105;
                            let _e106 = k;
                            let _e109 = unnamed_1.a[_e106];
                            let _e111 = step;
                            candidate = (bitcast<i32>(_e109) - bitcast<i32>(_e111));
                            let _e114 = candidate;
                            let _e115 = threshold;
                            if (_e114 >= _e115) {
                                let _e117 = value;
                                let _e118 = candidate;
                                value = max(_e117, bitcast<u32>(_e118));
                            }
                        }
                        continue;
                    } else {
                        break;
                    }
                    continuing {
                        let _e121 = dx;
                        dx = (_e121 + 1i);
                    }
                }
                continue;
            } else {
                break;
            }
            continuing {
                let _e123 = dy;
                dy = (_e123 + 1i);
            }
        }
    }
    let _e125 = i;
    let _e126 = value;
    unnamed_3.b[_e125] = _e126;
    let _e129 = value;
    let _e130 = old;
    if (_e129 != _e130) {
        let _e134 = atomicOr((&unnamed_4.changed[0i]), 1u);
    }
    return;
}

@compute @workgroup_size(128, 1, 1) 
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}
