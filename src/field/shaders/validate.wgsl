struct Failure {
    failure: array<u32>,
}

struct Params {
    width: u32,
    height: u32,
    cap: u32,
    pitch: u32,
}

struct Seeds {
    seeds: array<u32>,
}

struct Field {
    values: array<u32>,
}

struct Costs {
    costs: array<u32>,
}

struct OriginalCosts {
    originals: array<u32>,
}

struct Failure_1 {
    failure: array<atomic<u32>>,
}

@group(0) @binding(3)
var<storage, read_write> unnamed: Failure_1;
var<private> gl_GlobalInvocationID_1: vec3<u32>;
@group(0) @binding(4)
var<storage> unnamed_1: Params;
@group(0) @binding(0)
var<storage> unnamed_2: Seeds;
@group(0) @binding(1)
var<storage> unnamed_3: Field;
@group(0) @binding(2)
var<storage> unnamed_4: Costs;
@group(0) @binding(5)
var<storage> unnamed_5: OriginalCosts;

fn reject_u0028_u1_u003b(i: ptr<function, u32>) {
    let _e22 = atomicOr((&unnamed.failure[0i]), 1u);
    let _e25 = (*i);
    let _e26 = atomicMin((&unnamed.failure[1i]), _e25);
    return;
}

fn main_1() {
    var i_1: u32;
    var seed: u32;
    var value: u32;
    var own: u32;
    var param: u32;
    var param_1: u32;
    var param_2: u32;
    var x: u32;
    var y: u32;
    var best: u32;
    var west: u32;
    var local: u32;
    var east: u32;
    var local_1: u32;
    var north: u32;
    var local_2: u32;
    var south: u32;
    var local_3: u32;
    var threshold: i32;
    var dy: i32;
    var dx: i32;
    var row: u32;
    var local_4: u32;
    var column: u32;
    var local_5: u32;
    var k: u32;
    var packed: u32;
    var edge: u32;
    var local_6: u32;
    var param_3: u32;
    var candidate: i32;
    var param_4: u32;
    var phi_102_: bool;
    var phi_131_: bool;
    var phi_280_: bool;

    let _e52 = gl_GlobalInvocationID_1[0u];
    i_1 = _e52;
    let _e53 = i_1;
    let _e55 = unnamed_1.width;
    let _e57 = unnamed_1.height;
    if (_e53 >= (_e55 * _e57)) {
        return;
    }
    let _e60 = i_1;
    if (_e60 == 0u) {
        let _e64 = atomicExchange((&unnamed.failure[2i]), 1448104518u);
    }
    let _e65 = i_1;
    let _e68 = unnamed_2.seeds[_e65];
    seed = _e68;
    let _e69 = i_1;
    let _e72 = unnamed_3.values[_e69];
    value = _e72;
    let _e73 = i_1;
    let _e76 = unnamed_4.costs[_e73];
    own = _e76;
    let _e77 = seed;
    let _e79 = value;
    let _e82 = value;
    let _e83 = seed;
    let _e85 = (((_e77 > 65535u) || (_e79 > 65535u)) || (_e82 < _e83));
    phi_102_ = _e85;
    if !(_e85) {
        let _e87 = own;
        let _e88 = i_1;
        let _e91 = unnamed_5.originals[_e88];
        phi_102_ = (_e87 != _e91);
    }
    let _e94 = phi_102_;
    if _e94 {
        let _e95 = i_1;
        param = _e95;
        reject_u0028_u1_u003b((&param));
        return;
    }
    let _e96 = seed;
    if (_e96 == 0u) {
        let _e98 = value;
        if (_e98 != 0u) {
            let _e100 = i_1;
            param_1 = _e100;
            reject_u0028_u1_u003b((&param_1));
        }
        return;
    }
    let _e101 = own;
    let _e103 = ((_e101 & 65535u) == 0u);
    phi_131_ = _e103;
    if !(_e103) {
        let _e105 = own;
        phi_131_ = ((_e105 >> bitcast<u32>(16i)) == 0u);
    }
    let _e110 = phi_131_;
    if _e110 {
        let _e111 = i_1;
        param_2 = _e111;
        reject_u0028_u1_u003b((&param_2));
        return;
    }
    let _e112 = i_1;
    let _e114 = unnamed_1.width;
    x = (_e112 % _e114);
    let _e116 = i_1;
    let _e118 = unnamed_1.width;
    y = (_e116 / _e118);
    let _e120 = seed;
    best = _e120;
    let _e121 = x;
    if (_e121 == 0u) {
        let _e124 = unnamed_1.width;
        local = (_e124 - 1u);
    } else {
        let _e126 = x;
        local = (_e126 - 1u);
    }
    let _e128 = local;
    west = _e128;
    let _e129 = x;
    let _e132 = unnamed_1.width;
    if ((_e129 + 1u) == _e132) {
        local_1 = 0u;
    } else {
        let _e134 = x;
        local_1 = (_e134 + 1u);
    }
    let _e136 = local_1;
    east = _e136;
    let _e137 = y;
    if (_e137 == 0u) {
        let _e140 = unnamed_1.height;
        local_2 = (_e140 - 1u);
    } else {
        let _e142 = y;
        local_2 = (_e142 - 1u);
    }
    let _e144 = local_2;
    north = _e144;
    let _e145 = y;
    let _e148 = unnamed_1.height;
    if ((_e145 + 1u) == _e148) {
        local_3 = 0u;
    } else {
        let _e150 = y;
        local_3 = (_e150 + 1u);
    }
    let _e152 = local_3;
    south = _e152;
    let _e154 = unnamed_1.cap;
    threshold = max(2i, (65535i - bitcast<i32>(_e154)));
    dy = -1i;
    loop {
        let _e158 = dy;
        if (_e158 <= 1i) {
            dx = -1i;
            loop {
                let _e160 = dx;
                if (_e160 <= 1i) {
                    let _e162 = dx;
                    let _e164 = dy;
                    if ((_e162 != 0i) || (_e164 != 0i)) {
                        let _e167 = dy;
                        if (_e167 < 0i) {
                            let _e169 = north;
                            local_4 = _e169;
                        } else {
                            let _e170 = dy;
                            let _e172 = south;
                            let _e173 = y;
                            local_4 = select(_e173, _e172, (_e170 > 0i));
                        }
                        let _e175 = local_4;
                        row = _e175;
                        let _e176 = dx;
                        if (_e176 < 0i) {
                            let _e178 = west;
                            local_5 = _e178;
                        } else {
                            let _e179 = dx;
                            let _e181 = east;
                            let _e182 = x;
                            local_5 = select(_e182, _e181, (_e179 > 0i));
                        }
                        let _e184 = local_5;
                        column = _e184;
                        let _e185 = row;
                        let _e187 = unnamed_1.width;
                        let _e189 = column;
                        k = ((_e185 * _e187) + _e189);
                        let _e191 = k;
                        let _e194 = unnamed_2.seeds[_e191];
                        let _e195 = (_e194 == 0u);
                        phi_280_ = _e195;
                        if !(_e195) {
                            let _e197 = k;
                            let _e200 = unnamed_3.values[_e197];
                            phi_280_ = (_e200 <= 1u);
                        }
                        let _e203 = phi_280_;
                        if _e203 {
                            continue;
                        }
                        let _e204 = k;
                        let _e207 = unnamed_5.originals[_e204];
                        packed = _e207;
                        let _e208 = dx;
                        let _e210 = dy;
                        if ((_e208 != 0i) && (_e210 != 0i)) {
                            let _e213 = packed;
                            local_6 = (_e213 >> bitcast<u32>(16i));
                        } else {
                            let _e216 = packed;
                            local_6 = (_e216 & 65535u);
                        }
                        let _e218 = local_6;
                        edge = _e218;
                        let _e219 = edge;
                        if (_e219 == 0u) {
                            let _e221 = i_1;
                            param_3 = _e221;
                            reject_u0028_u1_u003b((&param_3));
                            return;
                        }
                        let _e222 = k;
                        let _e225 = unnamed_3.values[_e222];
                        let _e227 = edge;
                        candidate = (bitcast<i32>(_e225) - bitcast<i32>(_e227));
                        let _e230 = candidate;
                        let _e231 = threshold;
                        if (_e230 >= _e231) {
                            let _e233 = best;
                            let _e234 = candidate;
                            best = max(_e233, bitcast<u32>(_e234));
                        }
                    }
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e237 = dx;
                    dx = (_e237 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e239 = dy;
            dy = (_e239 + 1i);
        }
    }
    let _e241 = value;
    let _e242 = best;
    if (_e241 != _e242) {
        let _e244 = i_1;
        param_4 = _e244;
        reject_u0028_u1_u003b((&param_4));
    }
    return;
}

@compute @workgroup_size(128, 1, 1)
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}
