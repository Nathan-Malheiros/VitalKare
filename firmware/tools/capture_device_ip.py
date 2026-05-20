#!/usr/bin/env python3
"""
capture_device_ip.py — escuta a serial da placa e tenta capturar o IP obtido no Wi-Fi.

Uso:
    python capture_device_ip.py --port COM8
    python capture_device_ip.py --port COM8 --timeout 60
"""

from __future__ import annotations

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture VitalKare device IP from serial logs")
    parser.add_argument("--port", required=True, help="Porta serial da placa")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--timeout", type=int, default=45, help="Tempo máximo de escuta em segundos")
    args = parser.parse_args()

    print(f"[INFO] Escutando {args.port} por ate {args.timeout}s...")
    print("[INFO] Se a placa ainda nao reiniciou, aperte RESET agora.\n")

    deadline = time.time() + args.timeout
    found_ip = None

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

                marker = "IP obtido:"
                if marker in line:
                    found_ip = line.split(marker, 1)[1].strip()
                    print(f"\n[OK] IP capturado da placa: {found_ip}\n")
                    return 0
    except serial.SerialException as exc:
        print(f"[ERRO] Falha ao abrir a serial {args.port}: {exc}")
        return 1

    if found_ip:
        print(f"\n[OK] IP capturado da placa: {found_ip}\n")
        return 0

    print("\n[AVISO] Nenhum IP foi capturado no tempo limite.")
    print("        Verifique SSID/senha, reinicie a placa e tente novamente.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
