#!/usr/bin/env python3
"""Isolated TLS terminator for browser/native WSS match tests, not deployment."""
import asyncio
from pathlib import Path
import ssl
import subprocess
import sys

async def main():
    directory, backend = Path(sys.argv[1]), int(sys.argv[2])
    cert, key = directory / 'cert.pem', directory / 'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
        '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
        '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    async def accept(reader, writer):
        upstream = None
        tasks = []
        async def copy(source, destination):
            while chunk := await source.read(16384):
                destination.write(chunk)
                await destination.drain()
        try:
            remote, upstream = await asyncio.open_connection('127.0.0.1', backend)
            tasks = [asyncio.create_task(copy(reader, upstream)), asyncio.create_task(copy(remote, writer))]
            await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        except (OSError, asyncio.IncompleteReadError):
            pass
        finally:
            for task in tasks: task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            writer.close()
            if upstream: upstream.close()
    server = await asyncio.start_server(accept, '127.0.0.1', 0, ssl=context)
    print(f'TLS forwarder listening on {server.sockets[0].getsockname()[1]}', flush=True)
    async with server: await server.serve_forever()

asyncio.run(main())
