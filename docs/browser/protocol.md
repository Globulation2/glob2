# YOG admission and compatibility

YOG owns admission for both native TCP and browser/native WebSocket clients. The
gateway forwards framed bytes to its configured backend; it does not maintain a
second authentication state machine or grant room membership.

## Admission order

| Server state | Accepted incoming messages |
| --- | --- |
| Waiting for greeting | Client information, ping reply |
| Waiting for credentials | Login attempt, registration request, ping reply |
| Authenticated | Existing lobby/room/file messages, ping reply |
| Incompatible | Ping reply; other messages close the connection |

A greeting is accepted only once. Login and registration cannot precede it or
replace an authenticated identity. Room and file operations cannot run before
successful authentication. Invalid transitions close the connection. A rejected
password leaves the client able to retry; rejection is not authentication.

Protocol version 29 identifies the updated browser/desktop simulation and
admission contract. Both older and newer protocol numbers are refused before
server information and before a legitimate client sends credentials. Server information now carries the server protocol as well. Updated clients
decode the shorter legacy greeting as version zero solely to report the
mismatch; they do not submit credentials to it. The legacy stable refusal
opcode and reason are retained so mismatches can be reported.
The UI asks users to install the same release as the server. A minimum-version
check is insufficient for client-simulated lockstep: newer is not equivalent to
compatible.

The client also validates the order of server information and acceptance
messages. It publishes its next connection state before notifying listeners;
listeners can submit credentials synchronously without their new state being
overwritten afterward. Credentials submitted through the client API before
server information or after authentication are ignored.

## Greeting wire contract

Both greetings retain their stable opcode and the existing unsigned 16-bit
big-endian frame length (which includes the opcode). Client information is
opcode 9 followed by the unsigned 16-bit protocol number. Server information is
opcode 10 followed by login policy (8 bits), game policy (8 bits), player ID
(16 bits), and protocol number (16 bits), all multibyte fields big-endian.
The server-information body is seven bytes including its opcode. Its legacy
five-byte form is recognized only to produce the compatibility error. A partial
version field, trailing bytes or malformed frame is rejected by shared framing.

Keep this initial version exchange small and stable. Capability and data
negotiation should use explicit typed messages after it, so recognizing an
incompatible release does not require parsing that release's entire handshake.

## Evidence and next protocol work

The maintained browser multiplayer suite sends invalid greeting/login/room
sequences through the real WebSocket gateway and native lobby. It tests both
version directions, duplicate greetings/logins, valid password retries and
normal lobby entry. A transport fault changes the actual browser greeting to an
incompatible version and verifies that no login or registration bytes follow.
Normal browser/browser and browser/native checksum scenarios remain regression
gates. Framing tests independently cover truncated messages and bounded queues.

Exact protocol admission is the first compatibility boundary, not the complete
release handshake. Explicit capability negotiation, simulation compatibility
identifiers and required game-data hashes remain to be added, including
validation of the data actually loaded at runtime. Matching a protocol number
alone does not establish deterministic equivalence or authenticate a client.
Room-specific authorization, guest/session credentials and reconnect epochs also
remain separate requirements. These checks do not make the lobby ready for an
unqualified public deployment.
