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
    assert all("world=1" in output for output in outputs), outputs
    assert all("moving=1" in output and "sharedClock=1" in output
               for output in outputs), outputs
    assert "seen=2,3," in outputs[0], outputs
    assert "seen=1,3," in outputs[1], outputs
    assert "seen=1,2," in outputs[2], outputs
    assert "left=2," in outputs[0], outputs
finally:
    for process in locals().get("peers", []):
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
    if server.poll() is None:
        server.terminate()
    server.wait(timeout=3)
