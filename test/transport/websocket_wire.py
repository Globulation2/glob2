"""Minimal RFC 6455 wire helpers for verified TLS integration peers."""
import os
import struct

def receive(sock, size):
    result = b''
    while len(result) < size:
        part = sock.recv(size - len(result))
        if not part:
            raise EOFError('Connection closed')
        result += part
    return result


def frame_header(length, opcode=2, final=True):
    header = bytes([(128 if final else 0) | opcode])
    if length < 126:
        header += bytes([128 | length])
    elif length < 65536:
        header += b'\xfe' + struct.pack('!H', length)
    else:
        header += b'\xff' + struct.pack('!Q', length)
    return header


def send_frame_header(sock, length, opcode=2, final=True):
    """Declare a masked frame without sending its body, for size-limit tests."""
    sock.sendall(frame_header(length, opcode, final) + os.urandom(4))


def send_frame(sock, payload, opcode=2, final=True):
    mask = os.urandom(4)
    sock.sendall(frame_header(len(payload), opcode, final) + mask +
                 bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))


def read_frame(sock):
    flags, length = receive(sock, 2)
    if length & 128:
        raise AssertionError('Server must not mask frames')
    if length == 126:
        length = struct.unpack('!H', receive(sock, 2))[0]
    elif length == 127:
        length = struct.unpack('!Q', receive(sock, 8))[0]
    return flags & 15, receive(sock, length)
