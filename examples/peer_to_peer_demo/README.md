# Peer-to-Peer Demo

This demo shows the direct player-position transport in `PeerClient`. The
coordinator assigns IDs and sends the peer directory and a shared moving drone.
Each client sends its square's position directly to the other clients.

## Build and run

From the repository root:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target peer_to_peer_server peer_to_peer_client
```

Start the coordinator in one terminal:

```bash
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_server
```

Start two or more clients, each with a different advertised port:

```bash
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_client tcp://127.0.0.1:7001
./build/linux-debug/examples/peer_to_peer_demo/peer_to_peer_client tcp://127.0.0.1:7002
```

Move a square with WASD or the arrow keys. The square with a white outline is
local; the other colored squares are peers. The yellow square is the coordinator's
shared drone. Press Escape or close the window to stop a client. Press Ctrl+C to
stop the coordinator. Start another client later, or close one, to see the peer
directory update.

The default registration endpoint is `tcp://127.0.0.1:5555`; control ports start
at `6001`. To use other ports, run the server with
`peer_to_peer_server <registration-port> <first-control-port>`, then pass the
registration endpoint as the client's second argument. For clients on different
machines, advertise a host address reachable from the other machines and allow
the registration, control, and peer ports through the firewall.
