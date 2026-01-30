#!/usr/bin/env python3
"""
Reactor CTL - MicroReactor Command Line Control Tool

A Python tool for interacting with MicroReactor devices via serial port.
Provides signal injection, monitoring, and debugging capabilities.

Usage:
    rctl.py list                    # List all entities
    rctl.py inject <target> <sig>   # Inject a signal
    rctl.py listen [--filter=SIG_*] # Monitor signals
    rctl.py param get <id>          # Get parameter value
    rctl.py param set <id> <value>  # Set parameter value
    rctl.py trace start             # Start tracing
    rctl.py trace dump              # Dump trace buffer
"""

import argparse
import serial
import struct
import json
import time
import sys
from dataclasses import dataclass
from typing import Optional, List, Callable

# Codec constants (must match ur_codec.h)
SYNC_BYTE = 0x55
HEADER_SIZE = 7
CRC_SIZE = 2

# System signals
SIG_NONE = 0x0000
SIG_SYS_INIT = 0x0001
SIG_SYS_ENTRY = 0x0002
SIG_SYS_EXIT = 0x0003
SIG_SYS_TICK = 0x0004
SIG_SYS_TIMEOUT = 0x0005
SIG_USER_BASE = 0x0100

# Shell commands
CMD_LIST_ENTITIES = b"list\n"
CMD_STATUS = b"status\n"
CMD_INJECT = b"inject %d %d %d\n"
CMD_PARAM_GET = b"param get %d\n"
CMD_PARAM_SET = b"param set %d %s\n"
CMD_TRACE_START = b"trace start\n"
CMD_TRACE_STOP = b"trace stop\n"
CMD_TRACE_DUMP = b"trace dump\n"


@dataclass
class Signal:
    """Signal structure"""
    id: int
    src_id: int
    payload: bytes
    timestamp: int = 0

    def to_json(self) -> str:
        return json.dumps({
            "id": self.id,
            "src": self.src_id,
            "ts": self.timestamp,
            "payload": list(self.payload)
        })

    @classmethod
    def from_json(cls, data: str) -> 'Signal':
        obj = json.loads(data)
        return cls(
            id=obj.get("id", 0),
            src_id=obj.get("src", 0),
            payload=bytes(obj.get("payload", [0, 0, 0, 0])),
            timestamp=obj.get("ts", 0)
        )


def crc16_ccitt(data: bytes) -> int:
    """Calculate CRC16 CCITT"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
        crc &= 0xFFFF
    return crc


def encode_signal(sig: Signal) -> bytes:
    """Encode signal to binary frame"""
    payload = sig.payload[:4].ljust(4, b'\x00')

    frame = bytearray()
    frame.append(SYNC_BYTE)
    frame.extend(struct.pack('<H', len(payload)))  # Length
    frame.extend(struct.pack('<H', sig.id))        # Signal ID
    frame.extend(struct.pack('<H', sig.src_id))    # Source ID
    frame.extend(payload)                           # Payload

    crc = crc16_ccitt(bytes(frame[1:]))
    frame.extend(struct.pack('<H', crc))

    return bytes(frame)


def decode_signal(data: bytes) -> Optional[Signal]:
    """Decode binary frame to signal"""
    if len(data) < HEADER_SIZE + CRC_SIZE:
        return None

    # Find sync byte
    start = data.find(bytes([SYNC_BYTE]))
    if start < 0:
        return None

    data = data[start:]
    if len(data) < HEADER_SIZE + CRC_SIZE:
        return None

    payload_len = struct.unpack('<H', data[1:3])[0]
    total_len = HEADER_SIZE + payload_len + CRC_SIZE

    if len(data) < total_len:
        return None

    # Verify CRC
    frame_data = data[1:total_len - CRC_SIZE]
    expected_crc = struct.unpack('<H', data[total_len - CRC_SIZE:total_len])[0]
    actual_crc = crc16_ccitt(frame_data)

    if expected_crc != actual_crc:
        print(f"CRC mismatch: expected {expected_crc:04X}, got {actual_crc:04X}")
        return None

    sig_id = struct.unpack('<H', data[3:5])[0]
    src_id = struct.unpack('<H', data[5:7])[0]
    payload = data[HEADER_SIZE:HEADER_SIZE + payload_len]

    return Signal(id=sig_id, src_id=src_id, payload=bytes(payload))


class ReactorCTL:
    """MicroReactor control interface"""

    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial: Optional[serial.Serial] = None
        self.signal_callback: Optional[Callable[[Signal], None]] = None
        self.decoder_buffer = bytearray()

    def connect(self) -> bool:
        """Connect to device"""
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(0.1)  # Wait for connection
            return True
        except serial.SerialException as e:
            print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from device"""
        if self.serial:
            self.serial.close()
            self.serial = None

    def send_command(self, cmd: bytes) -> str:
        """Send shell command and return response"""
        if not self.serial:
            return ""

        self.serial.write(cmd)
        time.sleep(0.1)

        response = b""
        while self.serial.in_waiting:
            response += self.serial.read(self.serial.in_waiting)
            time.sleep(0.05)

        return response.decode('utf-8', errors='ignore')

    def send_signal(self, target_id: int, sig: Signal):
        """Send signal to device"""
        if not self.serial:
            return

        frame = encode_signal(sig)
        self.serial.write(frame)

    def inject_signal(self, target_id: int, sig_id: int, payload: int = 0):
        """Inject signal via shell command"""
        cmd = CMD_INJECT % (target_id, sig_id, payload)
        return self.send_command(cmd)

    def list_entities(self) -> str:
        """List all registered entities"""
        return self.send_command(CMD_LIST_ENTITIES)

    def get_status(self) -> str:
        """Get system status"""
        return self.send_command(CMD_STATUS)

    def get_param(self, param_id: int) -> str:
        """Get parameter value"""
        cmd = CMD_PARAM_GET % param_id
        return self.send_command(cmd)

    def set_param(self, param_id: int, value: str) -> str:
        """Set parameter value"""
        cmd = CMD_PARAM_SET % (param_id, value.encode())
        return self.send_command(cmd)

    def start_trace(self) -> str:
        """Start performance tracing"""
        return self.send_command(CMD_TRACE_START)

    def stop_trace(self) -> str:
        """Stop performance tracing"""
        return self.send_command(CMD_TRACE_STOP)

    def dump_trace(self) -> str:
        """Dump trace buffer"""
        return self.send_command(CMD_TRACE_DUMP)

    def listen(self, filter_pattern: str = None, callback: Callable[[Signal], None] = None):
        """Listen for signals (blocking)"""
        if not self.serial:
            return

        self.signal_callback = callback

        print(f"Listening on {self.port}... (Ctrl+C to stop)")

        try:
            while True:
                if self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting)
                    self.decoder_buffer.extend(data)

                    # Try to decode signals
                    while len(self.decoder_buffer) >= HEADER_SIZE + CRC_SIZE:
                        sig = decode_signal(bytes(self.decoder_buffer))
                        if sig:
                            # Check filter
                            if filter_pattern:
                                sig_name = f"0x{sig.id:04X}"
                                if not sig_name.startswith(filter_pattern.replace('*', '')):
                                    self.decoder_buffer = self.decoder_buffer[1:]
                                    continue

                            print(f"[{time.strftime('%H:%M:%S')}] {sig.to_json()}")

                            if self.signal_callback:
                                self.signal_callback(sig)

                            # Remove processed data
                            frame_len = HEADER_SIZE + 4 + CRC_SIZE
                            self.decoder_buffer = self.decoder_buffer[frame_len:]
                        else:
                            # No valid frame, skip one byte
                            self.decoder_buffer = self.decoder_buffer[1:]

                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\nStopped listening.")


def main():
    parser = argparse.ArgumentParser(description='MicroReactor Control Tool')
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0',
                       help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200,
                       help='Baud rate (default: 115200)')

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # List command
    subparsers.add_parser('list', help='List all entities')

    # Status command
    subparsers.add_parser('status', help='Get system status')

    # Inject command
    inject_parser = subparsers.add_parser('inject', help='Inject a signal')
    inject_parser.add_argument('target', type=int, help='Target entity ID')
    inject_parser.add_argument('signal', type=lambda x: int(x, 0),
                              help='Signal ID (hex or decimal)')
    inject_parser.add_argument('--payload', type=int, default=0,
                              help='Payload value (default: 0)')

    # Listen command
    listen_parser = subparsers.add_parser('listen', help='Monitor signals')
    listen_parser.add_argument('--filter', type=str, default=None,
                              help='Filter pattern (e.g., SIG_NET_*)')

    # Param command
    param_parser = subparsers.add_parser('param', help='Parameter operations')
    param_subparsers = param_parser.add_subparsers(dest='param_cmd')

    param_get = param_subparsers.add_parser('get', help='Get parameter')
    param_get.add_argument('id', type=int, help='Parameter ID')

    param_set = param_subparsers.add_parser('set', help='Set parameter')
    param_set.add_argument('id', type=int, help='Parameter ID')
    param_set.add_argument('value', type=str, help='Parameter value')

    # Trace command
    trace_parser = subparsers.add_parser('trace', help='Tracing operations')
    trace_subparsers = trace_parser.add_subparsers(dest='trace_cmd')
    trace_subparsers.add_parser('start', help='Start tracing')
    trace_subparsers.add_parser('stop', help='Stop tracing')
    trace_subparsers.add_parser('dump', help='Dump trace buffer')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    # Connect to device
    ctl = ReactorCTL(args.port, args.baudrate)
    if not ctl.connect():
        sys.exit(1)

    try:
        if args.command == 'list':
            print(ctl.list_entities())

        elif args.command == 'status':
            print(ctl.get_status())

        elif args.command == 'inject':
            result = ctl.inject_signal(args.target, args.signal, args.payload)
            print(result if result else "Signal injected")

        elif args.command == 'listen':
            ctl.listen(filter_pattern=args.filter)

        elif args.command == 'param':
            if args.param_cmd == 'get':
                print(ctl.get_param(args.id))
            elif args.param_cmd == 'set':
                print(ctl.set_param(args.id, args.value))

        elif args.command == 'trace':
            if args.trace_cmd == 'start':
                print(ctl.start_trace())
            elif args.trace_cmd == 'stop':
                print(ctl.stop_trace())
            elif args.trace_cmd == 'dump':
                print(ctl.dump_trace())

    finally:
        ctl.disconnect()


if __name__ == '__main__':
    main()
