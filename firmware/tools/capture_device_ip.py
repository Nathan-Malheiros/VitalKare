#!/usr/bin/env python3
"""
capture_device_ip.py — escuta a serial da placa e tenta capturar o IP obtido no Wi-Fi.

Uso:
    python capture_device_ip.py --port COM8
    python capture_device_ip.py --port COM8 --timeout 60
"""

from __future__ import annotations

import argparse
import re
import sys
import time

import serial

VK_IP_RE = re.compile(
    r"VK_IP:\s*mode=(?P<mode>\S+)\s+ip=(?P<ip>[0-9.]+)\s+gw=(?P<gateway>[0-9.]+)\s+netmask=(?P<netmask>[0-9.]+)",
    re.IGNORECASE,
)
LEGACY_IP_RE = re.compile(
    r"IP obtido(?:\s*\((?P<mode>[A-Z]+)\))?:\s*(?P<ip>[0-9.]+)",
    re.IGNORECASE,
)


def print_network_summary(network: dict[str, str]) -> None:
    print("\n[OK] Rede capturada da placa:")
    print(f"      modo: {network.get('mode', 'DHCP')}")
    print(f"      ip: {network['ip']}")
    if network.get("gateway"):
        print(f"      gateway: {network['gateway']}")
    if network.get("netmask"):
        print(f"      netmask: {network['netmask']}")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture VitalKare device IP from serial logs")
    parser.add_argument("--port", required=True, help="Porta serial da placa")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--timeout", type=int, default=45, help="Tempo máximo de escuta em segundos")
    args = parser.parse_args()

    print(f"[INFO] Escutando {args.port} por ate {args.timeout}s...")
    print("[INFO] Se a placa ainda nao reiniciou, aperte RESET agora.\n")

    deadline = time.time() + args.timeout
    found_network: dict[str, str] | None = None

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
            ser.dtr = False
            ser.rts = False

            while time.time() < deadline:
                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="replace").rstrip()
                if not line:
                    continue

                print(line)

                match = VK_IP_RE.search(line)
                if match:
                    found_network = {
                        "mode": match.group("mode").upper(),
                        "ip": match.group("ip"),
                        "gateway": match.group("gateway"),
                        "netmask": match.group("netmask"),
                    }
                    print_network_summary(found_network)
                    return 0

                match = LEGACY_IP_RE.search(line)
                if match:
                    found_network = {
                        "mode": (match.group("mode") or "DHCP").upper(),
                        "ip": match.group("ip"),
                    }
                    print_network_summary(found_network)
                    return 0
    except serial.SerialException as exc:
        print(f"[ERRO] Falha ao abrir a serial {args.port}: {exc}")
        return 1

    if found_network:
        print_network_summary(found_network)
        return 0

    print("\n[AVISO] Nenhum IP foi capturado no tempo limite.")
    print("        Verifique SSID/senha, reinicie a placa e tente novamente.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
