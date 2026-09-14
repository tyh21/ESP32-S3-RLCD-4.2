import serial, time, sys
port = "COM6"
try:
    s = serial.Serial(port, 115200, timeout=0.5)
    s.setDTR(True)
    s.setRTS(False)
    time.sleep(0.5)
    s.reset_input_buffer()
    deadline = time.time() + 12
    while time.time() < deadline:
        data = s.read(2048)
        if data:
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
    s.close()
except Exception as e:
    sys.stderr.write(f"Error: {e}\n")
