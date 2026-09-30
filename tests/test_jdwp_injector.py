"""JDWP wire fixtures for the host Python 3 injector (no phone required)."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "injector", Path(__file__).resolve().parents[1] / "python/jdwp-shellifier.py")
injector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(injector)


def string(value):
    data = value.encode("utf-8")
    return struct.pack(">I", len(data)) + data


def reply(packet_id, data=b"", error=0):
    return struct.pack(">IIBH", 11 + len(data), packet_id, 0x80, error) + data


def event(data):
    return struct.pack(">IIBBB", 11 + len(data), 99, 0, 64, 100) + data


class FragmentedSocket:
    def __init__(self, incoming):
        self.incoming = incoming
        self.sent = []

    def connect(self, address):
        self.address = address

    def recv(self, size):
        # Every header/body/handshake arrives in fragments, including IDs.
        size = min(size, 3)
        data, self.incoming = self.incoming[:size], self.incoming[size:]
        return data

    def sendall(self, data):
        if not isinstance(data, bytes):
            raise TypeError("JDWP wire payload must be bytes")
        self.sent.append(data)

    def close(self):
        pass


class InjectorTests(unittest.TestCase):
    def test_start_class_and_method_discovery(self):
        sizes = struct.pack(">IIIII", 8, 8, 8, 8, 8)
        version = string("Android Runtime") + struct.pack(">II", 1, 8)
        version += string("2.1.0") + string("Dalvik")
        classes = struct.pack(">IBQ", 1, 1, 0x12)
        classes += string("Ljava/lang/Runtime;") + struct.pack(">I", 1)
        methods = struct.pack(">IQ", 1, 0x33)
        methods += string("load") + string("(Ljava/lang/String;)V") + struct.pack(">I", 1)
        vm_start = b"\x02" + struct.pack(">IBIQ", 1, 90, 0, 7)
        sock = FragmentedSocket(b"JDWP-Handshake" + event(vm_start)
                                + reply(1, sizes) + reply(3, version)
                                + reply(5, classes) + reply(7, methods))
        client = injector.JDWPClient("127.0.0.1", 8700)
        with patch.object(injector.socket, "socket", return_value=sock):
            client.start()
        self.assertEqual(sock.sent[0], b"JDWP-Handshake")
        self.assertEqual(sock.sent[1], bytes.fromhex("0000000b00000001000107"))
        self.assertEqual(client.version, "Dalvik - 2.1.0")
        self.assertEqual(client.get_class_by_name("Ljava/lang/Runtime;")["refTypeId"], 0x12)
        client.get_methods(0x12)
        self.assertEqual(client.get_method_by_name("load")["methodId"], 0x33)
        self.assertEqual(client.wait_for_event(), vm_start)
        self.assertIsNone(client.parse_event_breakpoint(vm_start, 1))
        self.assertFalse(sock.incoming)

    def test_utf8_wire_byte_length(self):
        client = injector.JDWPClient("localhost")
        payload = client.buildstring("场景")
        self.assertEqual(payload, b"\x00\x00\x00\x06\xe5\x9c\xba\xe6\x99\xaf")
        self.assertEqual(client.readstring(payload), "场景")

    def test_async_load_reply_and_breakpoint_do_not_replace_resume_reply(self):
        breakpoint = b"\x02" + struct.pack(">IBIQ", 1, 2, 42, 7)
        client = injector.JDWPClient("localhost")
        client.objectIDSize = 8
        client.socket = FragmentedSocket(reply(1, b"V") + event(breakpoint) + reply(3))
        client.create_packet(injector.INVOKEMETHOD_SIG)
        client.create_packet(injector.RESUMEVM_SIG)
        self.assertEqual(client.read_reply(), b"")
        self.assertEqual(client.parse_event_breakpoint(client.wait_for_event(), 42), (42, 7, -1))

    def test_error_reply_and_closed_socket_fail(self):
        client = injector.JDWPClient("localhost")
        client.create_packet(injector.IDSIZES_SIG)
        client.socket = FragmentedSocket(reply(1, error=20))
        with self.assertRaisesRegex(RuntimeError, "error 20"):
            client.read_reply()
        client.socket = FragmentedSocket(b"\x00\x00")
        with self.assertRaises(EOFError):
            client.read_reply()


if __name__ == "__main__":
    unittest.main()
