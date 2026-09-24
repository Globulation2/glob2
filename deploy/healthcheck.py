#!/usr/bin/python3
"""Container liveness checks; match admission remains a YOG protocol decision."""
import socket
import sys
import urllib.request

if sys.argv[1] == 'gateway':
    with urllib.request.urlopen('http://127.0.0.1:8080/healthz', timeout=2) as response:
        assert response.status == 200
else:
    for port in ([7489, 7490] if sys.argv[1] == 'lobby' else [7491]):
        with socket.create_connection(('127.0.0.1', port), timeout=2):
            pass
