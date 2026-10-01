// Keep numeric edge results in globals, so their snapshot bytes are verified.
// Returning only Number.isNaN() would hide architecture-dependent NaN payloads.
let calls = 0;
let numbers = [];
function step(ctx) {
  calls++;
  const zero = ctx.tick - ctx.tick;
  numbers = [NaN, -NaN, zero / zero, Math.sqrt(-1), Math.log(-1),
    Infinity, -Infinity, -0, 0, Number.MIN_VALUE, -Number.MIN_VALUE, Number.MAX_VALUE];
  return {calls, nan: numbers.slice(0, 5).every(Number.isNaN),
    infinities: numbers[5] === Infinity && numbers[6] === -Infinity,
    zeros: Object.is(numbers[7], -0) && Object.is(numbers[8], 0)};
}
