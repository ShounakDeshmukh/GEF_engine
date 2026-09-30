# Basic peer to peer demo

The server registers clients and shares their reachable addresses. Each client
publishes its own square position directly to the other clients. Player updates
do not pass through the server. This example uses the engine's `Server` and
`Client` API in direct peer mode, with a game-specific eight-byte position payload.

## Build and run

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target peer_to_peer_server peer_to_peer_client
```

Start one server and two clients in separate terminals:

```bash
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_server
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_client
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_client
```

Move your square with WASD or arrow keys. Each client shows the other client's
square. Escape closes a client; its square disappears from the remaining window.
The server reports the number of connected peers. Both clients can use the same
default settings on one machine because peer ports are allocated automatically.

The server listens on port 5555 by default. Use `--port 5556` on the server and
`--server tcp://127.0.0.1:5556` on each client to change it. On a LAN, pass the
server's reachable address to `--server` and the client's reachable host address
to `--advertise`. The advertised address and allocated peer port must be
reachable by the other clients. This demo assumes trusted peers and does not
provide NAT traversal.

`demoPayload.hpp` encodes the example's two floats; the engine protocol carries
opaque game bytes. The client sends one position per 60 Hz game tick, ignores its
own ID and older ticks, and removes a remote square when that peer leaves. The
first update can be missed while a new subscriber connects; later ticks provide
fresh state.
