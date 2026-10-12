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

    let _e38 = (*x);
    if (_e38 < 0i) {
        let _e40 = (*size);
        let _e42 = (*x);
        let _e46 = (*size);
        local = ((_e40 - 1u) - (bitcast<u32>((-(_e42) - 1i)) % _e46));
    } else {
        let _e49 = (*x);
        let _e51 = (*size);
        local = (bitcast<u32>(_e49) % _e51);
    }
    let _e53 = local;
    return _e53;
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
    var phi_397_: bool;
    var phi_459_: bool;
    var phi_471_: bool;

    let _e84 = gl_LocalInvocationID_1[0u];
    lid = _e84;
    let _e86 = gl_WorkGroupID_1[0u];
    gx = _e86;
    let _e88 = gl_WorkGroupID_1[1u];
    gy = _e88;
    let _e89 = gy;
    let _e91 = unnamed.pitch;
    let _e93 = gx;
    tile = ((_e89 * _e91) + _e93);
    let _e96 = unnamed.height;
    rows = ((_e96 + 15u) / 16u);
    let _e99 = tile;
    let _e102 = unnamed_1.activeTiles[_e99];
    isActive = (_e102 != 0u);
    let _e104 = lid;
    j = _e104;
    loop {
        let _e105 = j;
        if (_e105 < 324u) {
            let _e107 = gx;
            let _e109 = j;
            param = (bitcast<i32>(((_e107 * 16u) + (_e109 % 18u))) - 1i);
            let _e115 = unnamed.width;
            param_1 = _e115;
            let _e116 = wrap_u0028_i1_u003b_u1_u003b((&param), (&param_1));
            x_1 = _e116;
            let _e117 = gy;
            let _e119 = j;
            param_2 = (bitcast<i32>(((_e117 * 16u) + (_e119 / 18u))) - 1i);
            let _e125 = unnamed.height;
            param_3 = _e125;
            let _e126 = wrap_u0028_i1_u003b_u1_u003b((&param_2), (&param_3));
            y = _e126;
            let _e127 = j;
            let _e128 = y;
            let _e130 = unnamed.width;
            let _e132 = x_1;
            let _e136 = unnamed_2.a[((_e128 * _e130) + _e132)];
            values[_e127] = _e136;
            continue;
        } else {
            break;
        }
        continuing {
            let _e138 = j;
            j = (_e138 + 128u);
        }
    }
    workgroupBarrier();
    let _e141 = unnamed.cap;
    threshold = max(2i, (65535i - bitcast<i32>(_e141)));
    round = 0u;
    loop {
        let _e145 = round;
        if (_e145 < 8u) {
            color = 0u;
            loop {
                let _e147 = color;
                if (_e147 < 4u) {
                    let _e149 = lid;
                    j_1 = _e149;
                    loop {
                        let _e150 = j_1;
                        if (_e150 < 324u) {
                            let _e152 = j_1;
                            let _e153 = j_1;
                            let _e155 = values[_e153];
                            nextValues[_e152] = _e155;
                            continue;
                        } else {
                            break;
                        }
                        continuing {
                            let _e157 = j_1;
                            j_1 = (_e157 + 128u);
                        }
                    }
                    workgroupBarrier();
                    let _e159 = isActive;
                    let _e160 = lid;
                    if (_e159 && (_e160 < 64u)) {
                        let _e163 = lid;
                        let _e167 = color;
                        x_2 = ((1u + ((_e163 % 8u) * 2u)) + (_e167 & 1u));
                        let _e170 = lid;
                        let _e174 = color;
                        y_1 = ((1u + ((_e170 / 8u) * 2u)) + (_e174 >> bitcast<u32>(1i)));
                        let _e178 = y_1;
                        let _e180 = x_2;
                        j_2 = ((_e178 * 18u) + _e180);
                        let _e182 = j_2;
                        let _e184 = values[_e182];
                        value = _e184;
                        let _e185 = value;
                        if (_e185 != 0u) {
                            dy = -1i;
                            loop {
                                let _e187 = dy;
                                if (_e187 <= 1i) {
                                    dx = -1i;
                                    loop {
                                        let _e189 = dx;
                                        if (_e189 <= 1i) {
                                            let _e191 = dx;
                                            let _e193 = dy;
                                            if ((_e191 != 0i) || (_e193 != 0i)) {
                                                let _e196 = j_2;
                                                let _e198 = dy;
                                                let _e201 = dx;
                                                k = bitcast<u32>(((bitcast<i32>(_e196) + (_e198 * 18i)) + _e201));
                                                let _e204 = gx;
                                                let _e206 = x_2;
                                                let _e210 = dx;
                                                param_4 = ((bitcast<i32>(((_e204 * 16u) + _e206)) - 1i) + _e210);
                                                let _e213 = unnamed.width;
                                                param_5 = _e213;
                                                let _e214 = wrap_u0028_i1_u003b_u1_u003b((&param_4), (&param_5));
                                                sx = _e214;
                                                let _e215 = gy;
                                                let _e217 = y_1;
                                                let _e221 = dy;
                                                param_6 = ((bitcast<i32>(((_e215 * 16u) + _e217)) - 1i) + _e221);
                                                let _e224 = unnamed.height;
                                                param_7 = _e224;
                                                let _e225 = wrap_u0028_i1_u003b_u1_u003b((&param_6), (&param_7));
                                                sy = _e225;
                                                let _e226 = sy;
                                                let _e228 = unnamed.width;
                                                let _e230 = sx;
                                                let _e234 = unnamed_3.costs[((_e226 * _e228) + _e230)];
                                                packed = _e234;
                                                let _e235 = dx;
                                                let _e237 = dy;
                                                if ((_e235 != 0i) && (_e237 != 0i)) {
                                                    let _e240 = packed;
                                                    local_1 = (_e240 >> bitcast<u32>(16i));
                                                } else {
                                                    let _e243 = packed;
                                                    local_1 = (_e243 & 65535u);
                                                }
                                                let _e245 = local_1;
                                                step = _e245;
                                                let _e246 = k;
                                                let _e248 = values[_e246];
                                                let _e250 = step;
                                                candidate = (bitcast<i32>(_e248) - bitcast<i32>(_e250));
                                                let _e253 = candidate;
                                                let _e254 = threshold;
                                                if (_e253 >= _e254) {
                                                    let _e256 = value;
                                                    let _e257 = candidate;
                                                    value = max(_e256, bitcast<u32>(_e257));
                                                }
                                            }
                                            continue;
                                        } else {
                                            break;
                                        }
                                        continuing {
                                            let _e260 = dx;
                                            dx = (_e260 + 1i);
                                        }
                                    }
                                    continue;
                                } else {
                                    break;
                                }
                                continuing {
                                    let _e262 = dy;
                                    dy = (_e262 + 1i);
                                }
                            }
                        }
                        let _e264 = j_2;
                        let _e265 = value;
                        nextValues[_e264] = _e265;
                    }
                    workgroupBarrier();
                    let _e267 = lid;
                    j_3 = _e267;
                    loop {
                        let _e268 = j_3;
                        if (_e268 < 324u) {
                            let _e270 = j_3;
                            let _e271 = j_3;
                            let _e273 = nextValues[_e271];
                            values[_e270] = _e273;
                            continue;
                        } else {
                            break;
                        }
                        continuing {
                            let _e275 = j_3;
                            j_3 = (_e275 + 128u);
                        }
                    }
                    workgroupBarrier();
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e277 = color;
                    color = (_e277 + bitcast<u32>(1i));
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e280 = round;
            round = (_e280 + bitcast<u32>(1i));
        }
    }
    let _e283 = lid;
    if (_e283 == 0u) {
        atomicStore((&localChanged), 0u);
    }
    workgroupBarrier();
    let _e285 = lid;
    j_4 = _e285;
    loop {
        let _e286 = j_4;
        if (_e286 < 256u) {
            let _e288 = gx;
            let _e290 = j_4;
            x_3 = ((_e288 * 16u) + (_e290 % 16u));
            let _e293 = gy;
            let _e295 = j_4;
            y_2 = ((_e293 * 16u) + (_e295 / 16u));
            let _e298 = x_3;
            let _e300 = unnamed.width;
            let _e301 = (_e298 < _e300);
            phi_397_ = _e301;
            if _e301 {
                let _e302 = y_2;
                let _e304 = unnamed.height;
                phi_397_ = (_e302 < _e304);
            }
            let _e307 = phi_397_;
            if _e307 {
                let _e308 = y_2;
                let _e310 = unnamed.width;
                let _e312 = x_3;
                i = ((_e308 * _e310) + _e312);
                let _e314 = j_4;
                let _e318 = j_4;
                let _e323 = values[(((((_e314 / 16u) + 1u) * 18u) + (_e318 % 16u)) + 1u)];
                v = _e323;
                let _e324 = i;
                let _e325 = v;
                unnamed_4.b[_e324] = _e325;
                let _e328 = i;
                let _e331 = unnamed_2.a[_e328];
                let _e332 = v;
                if (_e331 != _e332) {
                    let _e334 = atomicOr((&localChanged), 1u);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e335 = j_4;
            j_4 = (_e335 + 128u);
        }
    }
    workgroupBarrier();
    let _e337 = atomicLoad((&localChanged));
    if (_e337 != 0u) {
        let _e339 = lid;
        if (_e339 == 0u) {
            let _e343 = atomicOr((&unnamed_5.changed[0i]), 1u);
        }
        let _e345 = unnamed.width;
        let _e347 = ((_e345 % 16u) != 0u);
        phi_459_ = _e347;
        if _e347 {
            let _e349 = unnamed.width;
            phi_459_ = (_e349 > 16u);
        }
        let _e352 = phi_459_;
        rx = select(1i, 2i, _e352);
        let _e355 = unnamed.height;
        let _e357 = ((_e355 % 16u) != 0u);
        phi_471_ = _e357;
        if _e357 {
            let _e359 = unnamed.height;
            phi_471_ = (_e359 > 16u);
        }
        let _e362 = phi_471_;
        ry = select(1i, 2i, _e362);
        let _e364 = lid;
        let _e365 = rx;
        let _e368 = ry;
        if (_e364 < bitcast<u32>((((2i * _e365) + 1i) * ((2i * _e368) + 1i)))) {
            let _e374 = lid;
            let _e375 = bitcast<i32>(_e374);
            let _e376 = rx;
            let _e378 = ((2i * _e376) + 1i);
            let _e386 = rx;
            dx_1 = ((_e375 - (i32(floor((f32(_e375) / f32(_e378)))) * _e378)) - _e386);
            let _e388 = lid;
            let _e390 = rx;
            let _e394 = ry;
            dy_1 = ((bitcast<i32>(_e388) / ((2i * _e390) + 1i)) - _e394);
            let _e396 = gy;
            let _e398 = dy_1;
            param_8 = (bitcast<i32>(_e396) + _e398);
            let _e400 = rows;
            param_9 = _e400;
            let _e401 = wrap_u0028_i1_u003b_u1_u003b((&param_8), (&param_9));
            let _e403 = unnamed.pitch;
            let _e405 = gx;
            let _e407 = dx_1;
            param_10 = (bitcast<i32>(_e405) + _e407);
            let _e410 = unnamed.pitch;
            param_11 = _e410;
            let _e411 = wrap_u0028_i1_u003b_u1_u003b((&param_10), (&param_11));
            let _e415 = atomicOr((&unnamed_6.nextActive[((_e401 * _e403) + _e411)]), 1u);
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
