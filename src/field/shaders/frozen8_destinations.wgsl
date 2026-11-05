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
var<workgroup> values: array<vec2<u32>, 324>;
@group(0) @binding(0)
var<storage> unnamed_2: Input;
var<workgroup> nextValues: array<vec2<u32>, 324>;
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
    var value: vec2<u32>;
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
    var v: vec2<u32>;
    var rx: i32;
    var ry: i32;
    var dx_1: i32;
    var dy_1: i32;
    var param_8: i32;
    var param_9: u32;
    var param_10: i32;
    var param_11: u32;
    var phi_355_: bool;
    var phi_356_: bool;
    var phi_357_: bool;
    var phi_430_: bool;
    var phi_494_: bool;
    var phi_506_: bool;

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
                        let _e186 = value[0u];
                        if (_e186 != 0u) {
                            dy = -1i;
                            loop {
                                let _e188 = dy;
                                if (_e188 <= 1i) {
                                    dx = -1i;
                                    loop {
                                        let _e190 = dx;
                                        if (_e190 <= 1i) {
                                            let _e192 = dx;
                                            let _e194 = dy;
                                            if ((_e192 != 0i) || (_e194 != 0i)) {
                                                let _e197 = j_2;
                                                let _e199 = dy;
                                                let _e202 = dx;
                                                k = bitcast<u32>(((bitcast<i32>(_e197) + (_e199 * 18i)) + _e202));
                                                let _e205 = gx;
                                                let _e207 = x_2;
                                                let _e211 = dx;
                                                param_4 = ((bitcast<i32>(((_e205 * 16u) + _e207)) - 1i) + _e211);
                                                let _e214 = unnamed.width;
                                                param_5 = _e214;
                                                let _e215 = wrap_u0028_i1_u003b_u1_u003b((&param_4), (&param_5));
                                                sx = _e215;
                                                let _e216 = gy;
                                                let _e218 = y_1;
                                                let _e222 = dy;
                                                param_6 = ((bitcast<i32>(((_e216 * 16u) + _e218)) - 1i) + _e222);
                                                let _e225 = unnamed.height;
                                                param_7 = _e225;
                                                let _e226 = wrap_u0028_i1_u003b_u1_u003b((&param_6), (&param_7));
                                                sy = _e226;
                                                let _e227 = sy;
                                                let _e229 = unnamed.width;
                                                let _e231 = sx;
                                                let _e235 = unnamed_3.costs[((_e227 * _e229) + _e231)];
                                                packed = _e235;
                                                let _e236 = dx;
                                                let _e238 = dy;
                                                if ((_e236 != 0i) && (_e238 != 0i)) {
                                                    let _e241 = packed;
                                                    local_1 = (_e241 >> bitcast<u32>(16i));
                                                } else {
                                                    let _e244 = packed;
                                                    local_1 = (_e244 & 65535u);
                                                }
                                                let _e246 = local_1;
                                                step = _e246;
                                                let _e247 = k;
                                                let _e250 = values[_e247][0u];
                                                let _e252 = step;
                                                candidate = (bitcast<i32>(_e250) - bitcast<i32>(_e252));
                                                let _e255 = candidate;
                                                let _e256 = threshold;
                                                let _e257 = (_e255 >= _e256);
                                                phi_357_ = _e257;
                                                if _e257 {
                                                    let _e258 = candidate;
                                                    let _e261 = value[0u];
                                                    let _e262 = (bitcast<u32>(_e258) > _e261);
                                                    phi_356_ = _e262;
                                                    if !(_e262) {
                                                        let _e264 = candidate;
                                                        let _e267 = value[0u];
                                                        let _e268 = (bitcast<u32>(_e264) == _e267);
                                                        phi_355_ = _e268;
                                                        if _e268 {
                                                            let _e269 = k;
                                                            let _e272 = values[_e269][1u];
                                                            let _e274 = value[1u];
                                                            phi_355_ = (_e272 < _e274);
                                                        }
                                                        let _e277 = phi_355_;
                                                        phi_356_ = _e277;
                                                    }
                                                    let _e279 = phi_356_;
                                                    phi_357_ = _e279;
                                                }
                                                let _e281 = phi_357_;
                                                if _e281 {
                                                    let _e282 = candidate;
                                                    let _e284 = k;
                                                    let _e287 = values[_e284][1u];
                                                    value = vec2<u32>(bitcast<u32>(_e282), _e287);
                                                }
                                            }
                                            continue;
                                        } else {
                                            break;
                                        }
                                        continuing {
                                            let _e289 = dx;
                                            dx = (_e289 + 1i);
                                        }
                                    }
                                    continue;
                                } else {
                                    break;
                                }
                                continuing {
                                    let _e291 = dy;
                                    dy = (_e291 + 1i);
                                }
                            }
                        }
                        let _e293 = j_2;
                        let _e294 = value;
                        nextValues[_e293] = _e294;
                    }
                    workgroupBarrier();
                    let _e296 = lid;
                    j_3 = _e296;
                    loop {
                        let _e297 = j_3;
                        if (_e297 < 324u) {
                            let _e299 = j_3;
                            let _e300 = j_3;
                            let _e302 = nextValues[_e300];
                            values[_e299] = _e302;
                            continue;
                        } else {
                            break;
                        }
                        continuing {
                            let _e304 = j_3;
                            j_3 = (_e304 + 128u);
                        }
                    }
                    workgroupBarrier();
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e306 = color;
                    color = (_e306 + bitcast<u32>(1i));
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e309 = round;
            round = (_e309 + bitcast<u32>(1i));
        }
    }
    let _e312 = lid;
    if (_e312 == 0u) {
        atomicStore((&localChanged), 0u);
    }
    workgroupBarrier();
    let _e314 = lid;
    j_4 = _e314;
    loop {
        let _e315 = j_4;
        if (_e315 < 256u) {
            let _e317 = gx;
            let _e319 = j_4;
            x_3 = ((_e317 * 16u) + (_e319 % 16u));
            let _e322 = gy;
            let _e324 = j_4;
            y_2 = ((_e322 * 16u) + (_e324 / 16u));
            let _e327 = x_3;
            let _e329 = unnamed.width;
            let _e330 = (_e327 < _e329);
            phi_430_ = _e330;
            if _e330 {
                let _e331 = y_2;
                let _e333 = unnamed.height;
                phi_430_ = (_e331 < _e333);
            }
            let _e336 = phi_430_;
            if _e336 {
                let _e337 = y_2;
                let _e339 = unnamed.width;
                let _e341 = x_3;
                i = ((_e337 * _e339) + _e341);
                let _e343 = j_4;
                let _e347 = j_4;
                let _e352 = values[(((((_e343 / 16u) + 1u) * 18u) + (_e347 % 16u)) + 1u)];
                v = _e352;
                let _e353 = i;
                let _e354 = v;
                unnamed_4.b[_e353] = _e354;
                let _e357 = i;
                let _e360 = unnamed_2.a[_e357];
                let _e361 = v;
                if any((_e360 != _e361)) {
                    let _e364 = atomicOr((&localChanged), 1u);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e365 = j_4;
            j_4 = (_e365 + 128u);
        }
    }
    workgroupBarrier();
    let _e367 = atomicLoad((&localChanged));
    if (_e367 != 0u) {
        let _e369 = lid;
        if (_e369 == 0u) {
            let _e373 = atomicOr((&unnamed_5.changed[0i]), 1u);
        }
        let _e375 = unnamed.width;
        let _e377 = ((_e375 % 16u) != 0u);
        phi_494_ = _e377;
        if _e377 {
            let _e379 = unnamed.width;
            phi_494_ = (_e379 > 16u);
        }
        let _e382 = phi_494_;
        rx = select(1i, 2i, _e382);
        let _e385 = unnamed.height;
        let _e387 = ((_e385 % 16u) != 0u);
        phi_506_ = _e387;
        if _e387 {
            let _e389 = unnamed.height;
            phi_506_ = (_e389 > 16u);
        }
        let _e392 = phi_506_;
        ry = select(1i, 2i, _e392);
        let _e394 = lid;
        let _e395 = rx;
        let _e398 = ry;
        if (_e394 < bitcast<u32>((((2i * _e395) + 1i) * ((2i * _e398) + 1i)))) {
            let _e404 = lid;
            let _e405 = bitcast<i32>(_e404);
            let _e406 = rx;
            let _e408 = ((2i * _e406) + 1i);
            let _e416 = rx;
            dx_1 = ((_e405 - (i32(floor((f32(_e405) / f32(_e408)))) * _e408)) - _e416);
            let _e418 = lid;
            let _e420 = rx;
            let _e424 = ry;
            dy_1 = ((bitcast<i32>(_e418) / ((2i * _e420) + 1i)) - _e424);
            let _e426 = gy;
            let _e428 = dy_1;
            param_8 = (bitcast<i32>(_e426) + _e428);
            let _e430 = rows;
            param_9 = _e430;
            let _e431 = wrap_u0028_i1_u003b_u1_u003b((&param_8), (&param_9));
            let _e433 = unnamed.pitch;
            let _e435 = gx;
            let _e437 = dx_1;
            param_10 = (bitcast<i32>(_e435) + _e437);
            let _e440 = unnamed.pitch;
            param_11 = _e440;
            let _e441 = wrap_u0028_i1_u003b_u1_u003b((&param_10), (&param_11));
            let _e445 = atomicOr((&unnamed_6.nextActive[((_e431 * _e433) + _e441)]), 1u);
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
