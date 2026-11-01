struct Params {
    width: u32,
    height: u32,
    cap: u32,
    pitch: u32,
}

struct Active {
    activeTiles: array<u32>,
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

struct NextActive {
    nextActive: array<u32>,
}

struct Changed_1 {
    changed: array<atomic<u32>>,
}

struct NextActive_1 {
    nextActive: array<atomic<u32>>,
}

var<private> gl_LocalInvocationID_1: vec3<u32>;
var<private> gl_WorkGroupID_1: vec3<u32>;
@group(0) @binding(4)
var<storage> unnamed: Params;
@group(0) @binding(5)
var<storage> unnamed_1: Active;
var<workgroup> values: array<u32, 324>;
@group(0) @binding(0)
var<storage> unnamed_2: Input;
var<workgroup> nextValues: array<u32, 324>;
@group(0) @binding(2)
var<storage> unnamed_3: Costs;
var<workgroup> localChanged: atomic<u32>;
@group(0) @binding(1)
var<storage, read_write> unnamed_4: Output;
@group(0) @binding(3)
var<storage, read_write> unnamed_5: Changed_1;
@group(0) @binding(6)
var<storage, read_write> unnamed_6: NextActive_1;

fn wrap_u0028_i1_u003b_u1_u003b(x: ptr<function, i32>, size: ptr<function, u32>) -> u32 {
    var local: u32;

    let _e39 = (*x);
    if (_e39 < 0i) {
        let _e41 = (*size);
        let _e43 = (*x);
        let _e47 = (*size);
        local = ((_e41 - 1u) - (bitcast<u32>((-(_e43) - 1i)) % _e47));
    } else {
        let _e50 = (*x);
        let _e52 = (*size);
        local = (bitcast<u32>(_e50) % _e52);
    }
    let _e54 = local;
    return _e54;
}

fn main_1() {
    var lid: u32;
    var gx: u32;
    var gy: u32;
    var tile: u32;
    var rows: u32;
    var isActive: bool;
    var j: u32;
    var x_1: u32;
    var param: i32;
    var param_1: u32;
    var y: u32;
    var param_2: i32;
    var param_3: u32;
    var threshold: i32;
    var round: u32;
    var color: u32;
    var j_1: u32;
    var x_2: u32;
    var y_1: u32;
    var j_2: u32;
    var value: u32;
    var dy: i32;
    var dx: i32;
    var k: u32;
    var sx: u32;
    var param_4: i32;
    var param_5: u32;
    var sy: u32;
    var param_6: i32;
    var param_7: u32;
    var slot: i32;
    var packed: u32;
    var step: u32;
    var local_1: u32;
    var candidate: i32;
    var j_3: u32;
    var j_4: u32;
    var x_3: u32;
    var y_2: u32;
    var i: u32;
    var v: u32;
    var rx: i32;
    var ry: i32;
    var dx_1: i32;
    var dy_1: i32;
    var param_8: i32;
    var param_9: u32;
    var param_10: i32;
    var param_11: u32;
    var phi_431_: bool;
    var phi_493_: bool;
    var phi_505_: bool;

    let _e86 = gl_LocalInvocationID_1[0u];
    lid = _e86;
    let _e88 = gl_WorkGroupID_1[0u];
    gx = _e88;
    let _e90 = gl_WorkGroupID_1[1u];
    gy = _e90;
    let _e91 = gy;
    let _e93 = unnamed.pitch;
    let _e95 = gx;
    tile = ((_e91 * _e93) + _e95);
    let _e98 = unnamed.height;
    rows = ((_e98 + 15u) / 16u);
    let _e101 = tile;
    let _e104 = unnamed_1.activeTiles[_e101];
    isActive = (_e104 != 0u);
    let _e106 = lid;
    j = _e106;
    loop {
        let _e107 = j;
        if (_e107 < 324u) {
            let _e109 = gx;
            let _e111 = j;
            param = (bitcast<i32>(((_e109 * 16u) + (_e111 % 18u))) - 1i);
            let _e117 = unnamed.width;
            param_1 = _e117;
            let _e118 = wrap_u0028_i1_u003b_u1_u003b((&param), (&param_1));
            x_1 = _e118;
            let _e119 = gy;
            let _e121 = j;
            param_2 = (bitcast<i32>(((_e119 * 16u) + (_e121 / 18u))) - 1i);
            let _e127 = unnamed.height;
            param_3 = _e127;
            let _e128 = wrap_u0028_i1_u003b_u1_u003b((&param_2), (&param_3));
            y = _e128;
            let _e129 = j;
            let _e130 = y;
            let _e132 = unnamed.width;
            let _e134 = x_1;
            let _e138 = unnamed_2.a[((_e130 * _e132) + _e134)];
            values[_e129] = _e138;
            continue;
        } else {
            break;
        }
        continuing {
            let _e140 = j;
            j = (_e140 + 128u);
        }
    }
    workgroupBarrier();
    let _e143 = unnamed.cap;
    threshold = max(2i, (65535i - bitcast<i32>(_e143)));
    round = 0u;
    loop {
        let _e147 = round;
        if (_e147 < 8u) {
            color = 0u;
            loop {
                let _e149 = color;
                if (_e149 < 4u) {
                    let _e151 = lid;
                    j_1 = _e151;
                    loop {
                        let _e152 = j_1;
                        if (_e152 < 324u) {
                            let _e154 = j_1;
                            let _e155 = j_1;
                            let _e157 = values[_e155];
                            nextValues[_e154] = _e157;
                            continue;
                        } else {
                            break;
                        }
                        continuing {
                            let _e159 = j_1;
                            j_1 = (_e159 + 128u);
                        }
                    }
                    workgroupBarrier();
                    let _e161 = isActive;
                    let _e162 = lid;
                    if (_e161 && (_e162 < 64u)) {
                        let _e165 = lid;
                        let _e169 = color;
                        x_2 = ((1u + ((_e165 % 8u) * 2u)) + (_e169 & 1u));
                        let _e172 = lid;
                        let _e176 = color;
                        y_1 = ((1u + ((_e172 / 8u) * 2u)) + (_e176 >> bitcast<u32>(1i)));
                        let _e180 = y_1;
                        let _e182 = x_2;
                        j_2 = ((_e180 * 18u) + _e182);
                        let _e184 = j_2;
                        let _e186 = values[_e184];
                        value = _e186;
                        let _e187 = value;
                        if (_e187 != 0u) {
                            dy = -1i;
                            loop {
                                let _e189 = dy;
                                if (_e189 <= 1i) {
                                    dx = -1i;
                                    loop {
                                        let _e191 = dx;
                                        if (_e191 <= 1i) {
                                            let _e193 = dx;
                                            let _e195 = dy;
                                            if ((_e193 != 0i) || (_e195 != 0i)) {
                                                let _e198 = j_2;
                                                let _e200 = dy;
                                                let _e203 = dx;
                                                k = bitcast<u32>(((bitcast<i32>(_e198) + (_e200 * 18i)) + _e203));
                                                let _e206 = gx;
                                                let _e208 = x_2;
                                                let _e212 = dx;
                                                param_4 = ((bitcast<i32>(((_e206 * 16u) + _e208)) - 1i) + _e212);
                                                let _e215 = unnamed.width;
                                                param_5 = _e215;
                                                let _e216 = wrap_u0028_i1_u003b_u1_u003b((&param_4), (&param_5));
                                                sx = _e216;
                                                let _e217 = gy;
                                                let _e219 = y_1;
                                                let _e223 = dy;
                                                param_6 = ((bitcast<i32>(((_e217 * 16u) + _e219)) - 1i) + _e223);
                                                let _e226 = unnamed.height;
                                                param_7 = _e226;
                                                let _e227 = wrap_u0028_i1_u003b_u1_u003b((&param_6), (&param_7));
                                                sy = _e227;
                                                let _e228 = dy;
                                                let _e232 = dx;
                                                slot = ((((1i - _e228) * 3i) + 1i) - _e232);
                                                let _e234 = slot;
                                                let _e237 = slot;
                                                slot = (_e237 - select(0i, 1i, (_e234 > 4i)));
                                                let _e240 = unnamed.width;
                                                let _e242 = unnamed.height;
                                                let _e244 = sy;
                                                let _e246 = unnamed.width;
                                                let _e249 = sx;
                                                let _e253 = unnamed_3.costs[(((_e240 * _e242) + (_e244 * _e246)) + _e249)];
                                                let _e254 = slot;
                                                if ((_e253 & (1u << bitcast<u32>(_e254))) == 0u) {
                                                    continue;
                                                }
                                                let _e259 = sy;
                                                let _e261 = unnamed.width;
                                                let _e263 = sx;
                                                let _e267 = unnamed_3.costs[((_e259 * _e261) + _e263)];
                                                packed = _e267;
                                                let _e268 = dx;
                                                let _e270 = dy;
                                                if ((_e268 != 0i) && (_e270 != 0i)) {
                                                    let _e273 = packed;
                                                    local_1 = (_e273 >> bitcast<u32>(16i));
                                                } else {
                                                    let _e276 = packed;
                                                    local_1 = (_e276 & 65535u);
                                                }
                                                let _e278 = local_1;
                                                step = _e278;
                                                let _e279 = k;
                                                let _e281 = values[_e279];
                                                let _e283 = step;
                                                candidate = (bitcast<i32>(_e281) - bitcast<i32>(_e283));
                                                let _e286 = candidate;
                                                let _e287 = threshold;
                                                if (_e286 >= _e287) {
                                                    let _e289 = value;
                                                    let _e290 = candidate;
                                                    value = max(_e289, bitcast<u32>(_e290));
                                                }
                                            }
                                            continue;
                                        } else {
                                            break;
                                        }
                                        continuing {
                                            let _e293 = dx;
                                            dx = (_e293 + 1i);
                                        }
                                    }
                                    continue;
                                } else {
                                    break;
                                }
                                continuing {
                                    let _e295 = dy;
                                    dy = (_e295 + 1i);
                                }
                            }
                        }
                        let _e297 = j_2;
                        let _e298 = value;
                        nextValues[_e297] = _e298;
                    }
                    workgroupBarrier();
                    let _e300 = lid;
                    j_3 = _e300;
                    loop {
                        let _e301 = j_3;
                        if (_e301 < 324u) {
                            let _e303 = j_3;
                            let _e304 = j_3;
                            let _e306 = nextValues[_e304];
                            values[_e303] = _e306;
                            continue;
                        } else {
                            break;
                        }
                        continuing {
                            let _e308 = j_3;
                            j_3 = (_e308 + 128u);
                        }
                    }
                    workgroupBarrier();
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e310 = color;
                    color = (_e310 + bitcast<u32>(1i));
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e313 = round;
            round = (_e313 + bitcast<u32>(1i));
        }
    }
    let _e316 = lid;
    if (_e316 == 0u) {
        atomicStore((&localChanged), 0u);
    }
    workgroupBarrier();
    let _e318 = lid;
    j_4 = _e318;
    loop {
        let _e319 = j_4;
        if (_e319 < 256u) {
            let _e321 = gx;
            let _e323 = j_4;
            x_3 = ((_e321 * 16u) + (_e323 % 16u));
            let _e326 = gy;
            let _e328 = j_4;
            y_2 = ((_e326 * 16u) + (_e328 / 16u));
            let _e331 = x_3;
            let _e333 = unnamed.width;
            let _e334 = (_e331 < _e333);
            phi_431_ = _e334;
            if _e334 {
                let _e335 = y_2;
                let _e337 = unnamed.height;
                phi_431_ = (_e335 < _e337);
            }
            let _e340 = phi_431_;
            if _e340 {
                let _e341 = y_2;
                let _e343 = unnamed.width;
                let _e345 = x_3;
                i = ((_e341 * _e343) + _e345);
                let _e347 = j_4;
                let _e351 = j_4;
                let _e356 = values[(((((_e347 / 16u) + 1u) * 18u) + (_e351 % 16u)) + 1u)];
                v = _e356;
                let _e357 = i;
                let _e358 = v;
                unnamed_4.b[_e357] = _e358;
                let _e361 = i;
                let _e364 = unnamed_2.a[_e361];
                let _e365 = v;
                if (_e364 != _e365) {
                    let _e367 = atomicOr((&localChanged), 1u);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e368 = j_4;
            j_4 = (_e368 + 128u);
        }
    }
    workgroupBarrier();
    let _e370 = atomicLoad((&localChanged));
    if (_e370 != 0u) {
        let _e372 = lid;
        if (_e372 == 0u) {
            let _e376 = atomicOr((&unnamed_5.changed[0i]), 1u);
        }
        let _e378 = unnamed.width;
        let _e380 = ((_e378 % 16u) != 0u);
        phi_493_ = _e380;
        if _e380 {
            let _e382 = unnamed.width;
            phi_493_ = (_e382 > 16u);
        }
        let _e385 = phi_493_;
        rx = select(1i, 2i, _e385);
        let _e388 = unnamed.height;
        let _e390 = ((_e388 % 16u) != 0u);
        phi_505_ = _e390;
        if _e390 {
            let _e392 = unnamed.height;
            phi_505_ = (_e392 > 16u);
        }
        let _e395 = phi_505_;
        ry = select(1i, 2i, _e395);
        let _e397 = lid;
        let _e398 = rx;
        let _e401 = ry;
        if (_e397 < bitcast<u32>((((2i * _e398) + 1i) * ((2i * _e401) + 1i)))) {
            let _e407 = lid;
            let _e408 = bitcast<i32>(_e407);
            let _e409 = rx;
            let _e411 = ((2i * _e409) + 1i);
            let _e419 = rx;
            dx_1 = ((_e408 - (i32(floor((f32(_e408) / f32(_e411)))) * _e411)) - _e419);
            let _e421 = lid;
            let _e423 = rx;
            let _e427 = ry;
            dy_1 = ((bitcast<i32>(_e421) / ((2i * _e423) + 1i)) - _e427);
            let _e429 = gy;
            let _e431 = dy_1;
            param_8 = (bitcast<i32>(_e429) + _e431);
            let _e433 = rows;
            param_9 = _e433;
            let _e434 = wrap_u0028_i1_u003b_u1_u003b((&param_8), (&param_9));
            let _e436 = unnamed.pitch;
            let _e438 = gx;
            let _e440 = dx_1;
            param_10 = (bitcast<i32>(_e438) + _e440);
            let _e443 = unnamed.pitch;
            param_11 = _e443;
            let _e444 = wrap_u0028_i1_u003b_u1_u003b((&param_10), (&param_11));
            let _e448 = atomicOr((&unnamed_6.nextActive[((_e434 * _e436) + _e444)]), 1u);
        }
    }
    return;
}

@compute @workgroup_size(128, 1, 1)
fn main(@builtin(local_invocation_id) gl_LocalInvocationID: vec3<u32>, @builtin(workgroup_id) gl_WorkGroupID: vec3<u32>) {
    gl_LocalInvocationID_1 = gl_LocalInvocationID;
    gl_WorkGroupID_1 = gl_WorkGroupID;
    main_1();
}
