struct Params {
    width: u32,
    height: u32,
    cap: u32,
    pitch: u32,
}

struct Input {
    a: array<vec2<u32>>,
}

struct Costs {
    costs: array<u32>,
}

struct Output {
    b: array<vec2<u32>>,
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
    var value: vec2<u32>;
    var old: vec2<u32>;
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
    var phi_205_: bool;
    var phi_206_: bool;
    var phi_207_: bool;

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
    let _e48 = value[0u];
    if (_e48 != 0u) {
        let _e50 = i;
        let _e52 = unnamed.width;
        x_1 = bitcast<i32>((_e50 % _e52));
        let _e55 = i;
        let _e57 = unnamed.width;
        y = bitcast<i32>((_e55 / _e57));
        let _e61 = unnamed.cap;
        threshold = max(2i, (65535i - bitcast<i32>(_e61)));
        dy = -1i;
        loop {
            let _e65 = dy;
            if (_e65 <= 1i) {
                dx = -1i;
                loop {
                    let _e67 = dx;
                    if (_e67 <= 1i) {
                        let _e69 = dx;
                        let _e71 = dy;
                        if ((_e69 != 0i) || (_e71 != 0i)) {
                            let _e74 = y;
                            let _e75 = dy;
                            param = (_e74 + _e75);
                            let _e78 = unnamed.height;
                            param_1 = _e78;
                            let _e79 = wrap_u0028_i1_u003b_u1_u003b((&param), (&param_1));
                            let _e81 = unnamed.width;
                            let _e83 = x_1;
                            let _e84 = dx;
                            param_2 = (_e83 + _e84);
                            let _e87 = unnamed.width;
                            param_3 = _e87;
                            let _e88 = wrap_u0028_i1_u003b_u1_u003b((&param_2), (&param_3));
                            k = ((_e79 * _e81) + _e88);
                            let _e90 = dx;
                            let _e92 = dy;
                            if ((_e90 != 0i) && (_e92 != 0i)) {
                                let _e95 = k;
                                let _e98 = unnamed_2.costs[_e95];
                                local_1 = (_e98 >> bitcast<u32>(16i));
                            } else {
                                let _e101 = k;
                                let _e104 = unnamed_2.costs[_e101];
                                local_1 = (_e104 & 65535u);
                            }
                            let _e106 = local_1;
                            step = _e106;
                            let _e107 = k;
                            let _e111 = unnamed_1.a[_e107][0u];
                            let _e113 = step;
                            candidate = (bitcast<i32>(_e111) - bitcast<i32>(_e113));
                            let _e116 = candidate;
                            let _e117 = threshold;
                            let _e118 = (_e116 >= _e117);
                            phi_207_ = _e118;
                            if _e118 {
                                let _e119 = candidate;
                                let _e122 = value[0u];
                                let _e123 = (bitcast<u32>(_e119) > _e122);
                                phi_206_ = _e123;
                                if !(_e123) {
                                    let _e125 = candidate;
                                    let _e128 = value[0u];
                                    let _e129 = (bitcast<u32>(_e125) == _e128);
                                    phi_205_ = _e129;
                                    if _e129 {
                                        let _e130 = k;
                                        let _e134 = unnamed_1.a[_e130][1u];
                                        let _e136 = value[1u];
                                        phi_205_ = (_e134 < _e136);
                                    }
                                    let _e139 = phi_205_;
                                    phi_206_ = _e139;
                                }
                                let _e141 = phi_206_;
                                phi_207_ = _e141;
                            }
                            let _e143 = phi_207_;
                            if _e143 {
                                let _e144 = candidate;
                                let _e146 = k;
                                let _e150 = unnamed_1.a[_e146][1u];
                                value = vec2<u32>(bitcast<u32>(_e144), _e150);
                            }
                        }
                        continue;
                    } else {
                        break;
                    }
                    continuing {
                        let _e152 = dx;
                        dx = (_e152 + 1i);
                    }
                }
                continue;
            } else {
                break;
            }
            continuing {
                let _e154 = dy;
                dy = (_e154 + 1i);
            }
        }
    }
    let _e156 = i;
    let _e157 = value;
    unnamed_3.b[_e156] = _e157;
    let _e160 = value;
    let _e161 = old;
    if any((_e160 != _e161)) {
        let _e166 = atomicOr((&unnamed_4.changed[0i]), 1u);
    }
    return;
}

@compute @workgroup_size(128, 1, 1)
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}
