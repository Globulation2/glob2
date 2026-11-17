// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only pre-js for the browser build of torus-render-benchmark
// (cmake/web.cmake, web-render-parity). Its legacy GL capture reads the
// framebuffer as GL_RGB, which desktop GL accepts but WebGL2 rejects for an
// RGBA framebuffer (INVALID_OPERATION, leaving the capture black). Read RGBA
// and pack it to RGB with the context's pack alignment instead.
(() => {
  if (typeof WebGL2RenderingContext === 'undefined') return;
  const proto = WebGL2RenderingContext.prototype, readPixels = proto.readPixels;
  const RGB = 0x1907, RGBA = 0x1908, UNSIGNED_BYTE = 0x1401;
  proto.readPixels = function(x, y, width, height, format, type, destination, offset) {
    if (format !== RGB || type !== UNSIGNED_BYTE || !ArrayBuffer.isView(destination)
        || this.getParameter(this.PIXEL_PACK_BUFFER_BINDING))
      return readPixels.apply(this, arguments);
    const rgba = new Uint8Array(width * height * 4);
    readPixels.call(this, x, y, width, height, RGBA, UNSIGNED_BYTE, rgba);
    const alignment = this.getParameter(this.PACK_ALIGNMENT);
    const stride = Math.ceil(width * 3 / alignment) * alignment;
    const out = new Uint8Array(destination.buffer, destination.byteOffset + (offset || 0) * destination.BYTES_PER_ELEMENT);
    for (let row = 0; row < height; ++row)
      for (let column = 0; column < width; ++column) {
        const from = (row * width + column) * 4, to = row * stride + column * 3;
        out[to] = rgba[from]; out[to + 1] = rgba[from + 1]; out[to + 2] = rgba[from + 2];
      }
  };
})();
