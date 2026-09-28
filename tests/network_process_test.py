import re
import socket
import subprocess
import sys
import time

probe = sys.argv[1]
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    registration = sock.getsockname()[1]
control = registration + 1
if control + 103 > 65535:
    registration, control = 35000, 35001
ports = [registration + 100 + i for i in range(3)]

server = subprocess.Popen([probe, "server", str(registration), str(control)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    time.sleep(0.3)
    peers = []
    for index, (rate, duration) in enumerate([(5, 6500), (20, 2500)]):
        peers.append(subprocess.Popen([probe, "peer", str(registration), str(control),
                                       str(ports[index]), str(rate), str(duration)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    time.sleep(1)
    peers.append(subprocess.Popen([probe, "peer", str(registration), str(control),
                                   str(ports[2]), "40", "4500"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    outputs = []
    for process in peers:
        output, error = process.communicate(timeout=10)
        assert process.returncode == 0, error
        print(output.strip())
        outputs.append(output)
    results = []
    for output in outputs:
        match = re.fullmatch(
            r"self=(\d+) world=(\d+) moving=(\d+) sharedClock=(\d+) "
            r"seen=([\d,]*) left=([\d,]*)", output.strip())
        assert match, outputs
        self_id, world, moving, shared_clock, seen, left = match.groups()
        results.append({
            "self": int(self_id),
            "world": int(world),
            "moving": int(moving),
            "shared_clock": int(shared_clock),
            "seen": {int(value) for value in seen.split(",") if value},
            "left": {int(value) for value in left.split(",") if value},
        })

    ids = {result["self"] for result in results}
    assert ids == {1, 2, 3}, outputs
    assert all(result["world"] == result["moving"] == result["shared_clock"] == 1
               for result in results), outputs
    assert all(result["seen"] == ids - {result["self"]} for result in results), outputs
    # The second launched peer exits early, regardless of which ID it received.
    assert results[1]["self"] in results[0]["left"], outputs
finally:
    for process in locals().get("peers", []):
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
    if server.poll() is None:
        server.terminate()
    server.wait(timeout=3)
