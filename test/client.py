import socket

addr=("127.0.0.1",1325)

so = socket.socket(socket.AF_INET,socket.SOCK_STREAM)

so.connect(addr)

while True:
    msg=raw_input("input: ")
    if not msg:
        break
    so.send(msg.encode())
    data=so.recv(1024)
    if not data:
        break
    print(data.decode())
