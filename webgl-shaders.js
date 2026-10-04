// SPDX-License-Identifier: GPL-3.0-or-later
// Legacy GL emulation prepends this WebGL1 extension when it sees derivatives.
// GLSL ES 3.00 has derivatives built in and requires #version to come first.
// Run after the SDK defines GL, in both the UI and application pthread realms.
const glob2OriginalShaderSource = GL.getSource;
GL.getSource = (...args) => {
  const source = glob2OriginalShaderSource(...args);
  const prefix = '#extension GL_OES_standard_derivatives : enable\n';
  return source.startsWith(prefix + '#version 300 es') ? source.slice(prefix.length) : source;
};
