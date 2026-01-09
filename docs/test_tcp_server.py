import socket
import struct
import sys

# To decode, we would need the generated python protobuf classes.
# For now, we just verify the message framing and length.

def start_server(port=5252):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(('127.0.0.1', port))
    server_socket.listen(1)
    print(f"Listening on 127.0.0.1:{port}...", flush=True)

    try:
        conn, addr = server_socket.accept()
        print(f"Connected by {addr}", flush=True)
        while True:
            header = conn.recv(4)
            if not header:
                break
            
            length = struct.unpack('>I', header)[0]
            print(f"Next message length: {length}", flush=True)
            
            # Read the exact length
            data = b""
            while len(data) < length:
                packet = conn.recv(length - len(data))
                if not packet:
                    break
                data += packet
            
            print(f"Received {len(data)} bytes of data.", flush=True)
            # If we had the generated pb code, we could do:
            # event = midi_event_pb2.MidiEvent()
            # event.ParseFromString(data)
            # print(event)

    except KeyboardInterrupt:
        print("\nStopping server.")
    finally:
        server_socket.close()

if __name__ == "__main__":
    start_server()
