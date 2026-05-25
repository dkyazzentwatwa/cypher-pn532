#!/usr/bin/env python3
"""Host CLI for Cypher PN532 field workstation firmware."""

from __future__ import annotations

import argparse
import binascii
import json
import sys
import time
from pathlib import Path
from urllib.parse import quote, unquote_plus


DEFAULT_BAUD = 115200


def encode_value(value: str) -> str:
    return quote(value, safe="-_.")


def build_command(op: str, **kwargs: str | None) -> str:
    parts = [op.upper()]
    for key, value in kwargs.items():
        if value is None:
            continue
        parts.append(f"{key}={encode_value(str(value))}")
    return " ".join(parts)


def json_lines_from_text(text: str) -> list[dict]:
    rows: list[dict] = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return rows


class SerialTransport:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc
        self.serial = serial.Serial(port, baudrate=baud, timeout=0.2)
        self.timeout = timeout
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.serial.close()

    def request(self, command: str) -> list[dict]:
        self.serial.write((command + "\n").encode("utf-8"))
        self.serial.flush()
        rows: list[dict] = []
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            if not text.startswith("{"):
                continue
            try:
                row = json.loads(text)
            except json.JSONDecodeError:
                continue
            rows.append(row)
            if row.get("end") or (row.get("ok") is not None and row.get("op") != "GET_FILE"):
                break
        return rows


def print_rows(rows: list[dict]) -> int:
    if not rows:
        print(json.dumps({"ok": False, "error": "timeout_or_no_json"}))
        return 1
    for row in rows:
        print(json.dumps(row, indent=2, sort_keys=True))
    return 0 if rows[-1].get("ok", False) else 1


def download_file(transport: SerialTransport, name: str, output: Path) -> int:
    rows = transport.request(build_command("GET_FILE", name=name))
    chunks: list[tuple[int, bytes]] = []
    ok = False
    for row in rows:
        if row.get("hex"):
            chunks.append((int(row.get("offset", 0)), binascii.unhexlify(row["hex"])))
        if row.get("end"):
            ok = True
    if not ok:
        return print_rows(rows)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as f:
        for _, data in sorted(chunks):
            f.write(data)
    print(json.dumps({"ok": True, "op": "download", "filename": name, "output": str(output)}))
    return 0


def self_test() -> int:
    command = build_command("WRITE_NDEF", type="url", content="https://example.com/a b")
    assert command == "WRITE_NDEF type=url content=https%3A%2F%2Fexample.com%2Fa%20b"
    assert build_command("HELP") == "HELP"
    assert unquote_plus("hello+world%21") == "hello world!"
    rows = json_lines_from_text('boot\n{"ok":true,"op":"HELP","commands":["STATUS"]}\n')
    assert rows == [{"ok": True, "op": "HELP", "commands": ["STATUS"]}]
    chunks = json_lines_from_text(
        '{"ok":true,"op":"GET_FILE","start":true}\n'
        '{"ok":true,"op":"GET_FILE","offset":0,"hex":"4142"}\n'
        '{"ok":true,"op":"GET_FILE","end":true}\n'
    )
    assert chunks[1]["hex"] == "4142"
    print(json.dumps({"ok": True, "op": "self-test"}))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Control Cypher PN532 over USB serial.")
    parser.add_argument("--port", help="Serial port, for example /dev/cu.usbmodem3101")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--self-test", action="store_true")
    sub = parser.add_subparsers(dest="cmd")

    for name in ("help", "status", "scan", "dump", "key-audit", "verify", "emulate-ndef", "files"):
        sub.add_parser(name)

    p = sub.add_parser("write-ndef")
    p.add_argument("--type", choices=("url", "text", "vcard"), default="text")
    p.add_argument("--content", default="")
    p.add_argument("--name", default="")
    p.add_argument("--tel", default="")
    p.add_argument("--email", default="")

    p = sub.add_parser("write-from-sd")
    p.add_argument("name")

    p = sub.add_parser("clone")
    p.add_argument("--name", default="")

    p = sub.add_parser("download")
    p.add_argument("name")
    p.add_argument("-o", "--output", type=Path)

    p = sub.add_parser("upload-preset")
    p.add_argument("name")
    p.add_argument("content")

    p = sub.add_parser("delete")
    p.add_argument("name")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.cmd:
        parser.error("choose a command or --self-test")
    if not args.port:
        parser.error("--port is required unless using --self-test")

    command_map = {
        "help": build_command("HELP"),
        "status": build_command("STATUS"),
        "scan": build_command("SCAN"),
        "dump": build_command("DUMP"),
        "key-audit": build_command("KEY_AUDIT"),
        "verify": build_command("VERIFY"),
        "emulate-ndef": build_command("EMULATE_NDEF"),
        "files": build_command("FILES"),
    }

    transport = SerialTransport(args.port, args.baud, args.timeout)
    try:
        if args.cmd == "write-ndef":
            command = build_command("WRITE_NDEF", type=args.type, content=args.content,
                                    name=args.name, tel=args.tel, email=args.email)
        elif args.cmd == "write-from-sd":
            command = build_command("WRITE_FROM_SD", name=args.name)
        elif args.cmd == "clone":
            command = build_command("CLONE", name=args.name)
        elif args.cmd == "upload-preset":
            command = build_command("PUT_PRESET", name=args.name, content=args.content)
        elif args.cmd == "delete":
            command = build_command("DELETE", name=args.name)
        elif args.cmd == "download":
            output = args.output or Path(args.name)
            return download_file(transport, args.name, output)
        else:
            command = command_map[args.cmd]
        return print_rows(transport.request(command))
    finally:
        transport.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
