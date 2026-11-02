// @vitest-environment jsdom
import { useState } from 'react';
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { ColorPicker } from '../src/skins/ColorPicker.tsx';

afterEach(cleanup);
function Picker() {
  const [color, setColor] = useState('#ff0000');
  return <ColorPicker label="Paint color" value={color} onChange={setColor} />;
}
it('keeps partial hex edits local and accepts valid colors without an OS picker', () => {
  const { container } = render(<Picker />);
  expect(container.querySelector('input[type="color"]')).toBeNull();
  const hex = screen.getByLabelText('Paint color');
  fireEvent.change(hex, { target: { value: '#12' } });
  expect(hex.getAttribute('aria-invalid')).toBe('true');
  expect(screen.getByText('#FF0000')).toBeTruthy();
  fireEvent.blur(hex);
  expect((hex as HTMLInputElement).value).toBe('#ff0000');
  fireEvent.change(hex, { target: { value: '00FF00' } });
  expect(screen.getByText('#00FF00')).toBeTruthy();
  expect(screen.getByLabelText('Paint color hue').getAttribute('value')).toBe('120');
});
it('preserves chosen hue at black and adjusts both axes with the keyboard', () => {
  render(<Picker />);
  const plane = screen.getByRole('slider', { name: 'Paint color saturation and brightness' });
  for (let i = 0; i < 10; i++) fireEvent.keyDown(plane, { key: 'ArrowDown', shiftKey: true });
  expect(screen.getByText('#000000')).toBeTruthy();
  fireEvent.change(screen.getByLabelText('Paint color hue'), { target: { value: '120' } });
  fireEvent.keyDown(plane, { key: 'ArrowUp', shiftKey: true });
  expect(screen.getByText('#001A00')).toBeTruthy();
  fireEvent.keyDown(plane, { key: 'ArrowLeft', shiftKey: true });
  expect(plane.getAttribute('aria-valuenow')).toBe('90');
});
it('reflects an external eyedropper or undo value, including while editing hex', () => {
  const onChange = vi.fn();
  const { rerender } = render(
    <ColorPicker label="Paint color" value="#ff0000" onChange={onChange} />,
  );
  fireEvent.change(screen.getByLabelText('Paint color'), { target: { value: '#12' } });
  rerender(<ColorPicker label="Paint color" value="#0000ff" onChange={onChange} />);
  expect((screen.getByLabelText('Paint color') as HTMLInputElement).value).toBe('#0000ff');
  expect(screen.getByLabelText('Paint color hue').getAttribute('value')).toBe('240');
  expect(onChange).not.toHaveBeenCalled();
});
