import socket
import sys
import time

# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Connect the socket to the port where the server is listening
server_address = ('127.0.0.1', 7002)
# print 'connecting to %s port %s' % server_address
sock.connect(server_address)

try:
    # Send data
    message = 'config'
    sock.sendall(message)
    time.sleep(1)

    with open('config_template.txt', 'r') as data_file:
        config_content = data_file.read()

    sock.sendall(config_content)
    
    time.sleep(0.1)
    sock.sendall('done')

finally:
    # print 'closing socket'
    sock.close()
