# multithread_demo

A platformer scene updated across four threads through `engine::SimulationThread`.

## Threads

| Thread | Work |
|---|---|
| main | SDL events, keyboard capture, rendering, pause and speed requests |
| sim | player input, physics, collision (owns the Scene) |
| world | moving platform (`addSubsystemThread`) |
| fakeNetThread | drains per-tick player state and logs its rate; stands in for networking |

## Controls

| Key | Action |
|---|---|
| A / D | move left / right |
| Space | jump |
| P | toggle pause |
| 1 / 2 / 3 | speed 0.5 / 1.0 / 2.0 |

The bar in the top-left shows the state: its width scales with speed, and it turns red while paused.

## Expected log

The sim pushes one message per tick, so the fake net rate follows the game speed:

```
fake net: 60 msgs/s, last tick ...    speed 1.0
fake net: 30 msgs/s, last tick ...    speed 0.5
fake net: 120 msgs/s, last tick ...   speed 2.0
fake net: 0 msgs/s, last tick ...     paused
```

## Why render never touches the sim's Scene

After every frame that changes something, the sim publishes a copy of the Scene in a `RenderFrame`. Main takes the newest one and draws it; if none is waiting, it redraws the previous one. The live Scene therefore has one owner and no lock, and render never stalls the sim. The price is one Scene copy per published frame.
