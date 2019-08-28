#!/usr/bin/env python
from socket import *

s = socket(AF_INET, SOCK_STREAM)
s.connect(('127.0.0.1', 9999))

# send data

# 20 byte data 'hello'
s.send('\x14\x00\x00\x00you are my sunshine.')

# 4 byte data 'high'
s.send('\x04\x00\x00\x00high')
# 5 byte data 'hello'
s.send('\x05\x00\x00\x00hello')

s.close()