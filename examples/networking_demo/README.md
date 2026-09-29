# Networking Demo

This example demonstrates the engine's networking system using ZeroMQ.

The demo supports three request/response communication methods:

- **String** — Send and receive `std::string` messages.
- **Template** — Send and receive fixed-size, trivially copyable C++ types.
- **Bytes** — Send variable-size data using `ByteView` and `Bytes`.

It also includes a basic Publisher/Subscriber example.

## Running

Start the server first, using the communication mode you want to test:

```bash
./server string
./server template
./server bytes
```

Then start the client with the matching mode:

```bash
./client string
./client template
./client bytes
```

For the Publisher/Subscriber example:

```bash
./server subpub
./client subpub
```

## Examples

### Template Communication

The client sends an `engine::Transform` directly:

```cpp
auto [response, valid] =
    reqHand.send<engine::Transform, engine::Transform>(transform);
```

### Variable-Size Communication

Variable-size data can be sent without copying the request into a new buffer:

```cpp
engine::networking::ByteView request{
    reinterpret_cast<const std::byte*>(msg.data()),
    msg.size()
};

engine::networking::Bytes response;

bool valid = reqHand.send(request, response);
```

`ByteView` provides a non-owning view of the outgoing data, while `Bytes` stores the variable-size response.
