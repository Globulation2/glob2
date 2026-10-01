// Profile 1: observable results, including non-finite values and signed zero.
export function step(ctx, state) {
  const rows = [];
  const record = (name, value) => rows.push([name, typeof value === 'number' && !Number.isFinite(value)
    ? (Number.isNaN(value) ? 'NaN' : value < 0 ? '-Infinity' : 'Infinity') : value]);
  const unary = ['abs','acos','acosh','asin','asinh','atan','atanh','cbrt','ceil',
    'clz32','cos','cosh','exp','expm1','floor','f16round','fround','log','log10',
    'log1p','log2','round','sign','sin','sinh','sqrt','tan','tanh','trunc'];
  const edges = [-0,0,Number.MIN_VALUE,-Number.MIN_VALUE,Number.MAX_VALUE,-Number.MAX_VALUE,
    2**-1022,2**-24,0.49999999999999994,0.5,0.5000000000000001,-0.5,
    1,1.0000000000000002,65504,65520,16777217,2147483648,1e300,-745,Infinity,-Infinity,NaN];
  let seed = 0x4a535031;
  const next = () => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed >>> 0; };
  for (let i=0;i<32;i++) edges.push((1+next()/4294967296)*2**((next()%2098)-1074)*(next()&1?-1:1));
  const batch=state.batch??0;
  state.batchCount=unary.length+Math.ceil(edges.length/8)+1;
  if (batch<unary.length) {
    const method=unary[batch];
    edges.forEach((x,i) => record(`${method}/${i}`,Math[method](x)));
  }
  edges.forEach((x,i) => {
    if (batch<unary.length || batch>=state.batchCount-1 || Math.floor(i/8)!==batch-unary.length) return;
    const y = edges[(i*7+3)%edges.length];
    record(`atan2/${i}`,Math.atan2(x,y)); record(`hypot/${i}`,Math.hypot(x,y,Number.MIN_VALUE));
    record(`pow/${i}`,Math.pow(x,y)); record(`power-operator/${i}`,x**y);
    record(`imul/${i}`,Math.imul(x,y)); record(`max/${i}`,Math.max(x,y)); record(`min/${i}`,Math.min(x,y));
    record(`add/${i}`,x+y); record(`subtract/${i}`,x-y); record(`multiply/${i}`,x*y);
    record(`divide/${i}`,x/y); record(`remainder/${i}`,x%y); record(`negate/${i}`,-x);
    record(`int32/${i}`,x|0); record(`uint32/${i}`,x>>>0);
    record(`string/${i}`,String(x)); record(`radix16/${i}`,x.toString(16));
    record(`fixed/${i}`,x.toFixed(17)); record(`precision/${i}`,x.toPrecision(17));
    record(`exponential/${i}`,x.toExponential(17)); record(`json/${i}`,JSON.stringify(x));
  });
  if (batch===state.batchCount-1) {
  record('hypot/architecture-regression',Math.hypot(1.2154874465220262,1.8249387819142753));
  record('hypot/decision',Math.hypot(1.2154874465220262,1.8249387819142753)===2.192672180328695);
  record('hypot/no-arguments',Math.hypot()); record('hypot/inf-nan',Math.hypot(Infinity,NaN));
  record('sumPrecise/cancellation',Math.sumPrecise([1e100,1,-1e100]));
  record('sumPrecise/negative-zero',Math.sumPrecise([-0,-0]));
  record('random',Math.random()); record('ctx.random',ctx.random());
  for (const text of ['-0','0.1000000000000000055511151231257827021181583404541015625',
    '4.9406564584124654e-324','2.2250738585072012e-308','1.7976931348623157e308',
    '1e309','1e-999','0x10000000000001','0b101','0o777','  12.5tail','Infinity','NaN','']) {
    record(`Number/${text}`,Number(text)); record(`unary-plus/${text}`,+text);
    record(`parseFloat/${text}`,parseFloat(text)); record(`parseInt/${text}`,parseInt(text));
    record(`parseInt16/${text}`,parseInt(text,16));
  }
  for (const text of ['-0','0.1','4.9406564584124654e-324','1.7976931348623157e308','1e309','1e-999'])
    record(`JSON.parse/${text}`,JSON.parse(text));
  }
  state.mathMethods=Object.getOwnPropertyNames(Math).filter(k=>typeof Math[k]==='function').sort();
  state.results=rows;
  return null;
}
