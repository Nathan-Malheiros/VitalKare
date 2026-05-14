#!/usr/bin/env python3
"""
provision.py — Grava credenciais WiFi/MQTT na partição NVS do ESP32-S3

Uso:
    python provision.py --port COM16
    python provision.py --port COM16 --baud 115200

Dependências:
    pip install esptool
    idf.py deve estar no PATH (ou ESPIDF definido), para usar nvs_partition_gen.py
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import csv

# ─────────────────────────────────────────────
# Utilitários de prompt
# ─────────────────────────────────────────────

def ask(prompt: str, default: str = "") -> str:
    if default:
        val = input(f"  {prompt} [{default}]: ").strip()
        return val if val else default
    else:
        while True:
            val = input(f"  {prompt}: ").strip()
            if val:
                return val
            print("    ⚠  Campo obrigatório.")


def ask_int(prompt: str, default: int) -> int:
    while True:
        val = input(f"  {prompt} [{default}]: ").strip()
        if not val:
            return default
        try:
            return int(val)
        except ValueError:
            print("    ⚠  Digite um número inteiro.")


# ─────────────────────────────────────────────
# Coleta de parâmetros
# ─────────────────────────────────────────────

def collect_params() -> dict:
    print("\n╔══════════════════════════════════════╗")
    print("║  VitalKare — Provisionamento NVS     ║")
    print("╚══════════════════════════════════════╝\n")

    print("[ Wi-Fi ]")
    ssid = ask("SSID da rede Wi-Fi")
    wifi_pass = ask("Senha da rede Wi-Fi")

    print("\n[ MQTT Broker ]")
    mqtt_host = ask("IP do broker MQTT (notebook na LAN)", "192.168.1.100")
    mqtt_port = ask_int("Porta MQTT", 1883)
    mqtt_user = ask("Usuário MQTT", "vitalkare")
    mqtt_pass = ask("Senha MQTT", "vitalkare_mqtt")

    print("\n[ Dispositivo ]")
    device_id = ask("ID do dispositivo (ex: leito01)", "leito01")
    ward_id   = ask("ID da ala (ex: ala-a)", "ala-a")
    patient_id = ask("ID do paciente (ex: PAC-000)", "PAC-000")

    return {
        "ssid":       ssid,
        "pass":       wifi_pass,
        "mqtt_host":  mqtt_host,
        "mqtt_port":  str(mqtt_port),
        "mqtt_user":  mqtt_user,
        "mqtt_pass":  mqtt_pass,
        "device_id":  device_id,
        "ward_id":    ward_id,
        "patient_id": patient_id,
    }


# ─────────────────────────────────────────────
# Geração da partição NVS via nvs_partition_gen
# ─────────────────────────────────────────────

NVS_CSV_HEADER = ["key", "type", "encoding", "namespace", "value"]

NVS_NAMESPACE = "vitalkare"


def write_nvs_csv(params: dict, csv_path: str) -> None:
    rows = [
        # namespace
        [NVS_NAMESPACE, "namespace", "",   "",             ""],
        # wifi
        ["ssid",        "data",      "string", NVS_NAMESPACE, params["ssid"]],
        ["pass",        "data",      "string", NVS_NAMESPACE, params["pass"]],
        # mqtt
        ["mqtt_host",   "data",      "string", NVS_NAMESPACE, params["mqtt_host"]],
        ["mqtt_port",   "data",      "u16",    NVS_NAMESPACE, params["mqtt_port"]],
        ["mqtt_user",   "data",      "string", NVS_NAMESPACE, params["mqtt_user"]],
        ["mqtt_pass",   "data",      "string", NVS_NAMESPACE, params["mqtt_pass"]],
        # device
        ["device_id",   "data",      "string", NVS_NAMESPACE, params["device_id"]],
        ["ward_id",     "data",      "string", NVS_NAMESPACE, params["ward_id"]],
        ["patient_id",  "data",      "string", NVS_NAMESPACE, params["patient_id"]],
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(NVS_CSV_HEADER)
        writer.writerows(rows)
    print(f"  CSV gerado: {csv_path}")


def find_nvs_gen() -> str:
    """Localiza nvs_partition_gen.py no IDF ou no PATH."""
    # 1. Variável de ambiente IDF_PATH
    idf_path = os.environ.get("IDF_PATH", "")
    if idf_path:
        candidate = os.path.join(idf_path, "components", "nvs_flash",
                                 "nvs_partition_generator", "nvs_partition_gen.py")
        if os.path.isfile(candidate):
            return candidate

    # 2. Procura relativo ao script (caso esteja dentro do projeto)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for parent in [script_dir, os.path.dirname(script_dir)]:
        candidate = os.path.join(parent, "nvs_partition_gen.py")
        if os.path.isfile(candidate):
            return candidate

    raise FileNotFoundError(
        "nvs_partition_gen.py não encontrado.\n"
        "Configure IDF_PATH ou certifique-se que o ESP-IDF está instalado.\n"
        "Download: https://docs.espressif.com/projects/esp-idf/en/latest/"
    )


def generate_nvs_bin(csv_path: str, bin_path: str, size_kb: int = 24) -> None:
    gen = find_nvs_gen()
    cmd = [
        sys.executable, gen,
        "generate", csv_path, bin_path,
        str(size_kb * 1024),
    ]
    print(f"  Executando: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERRO:\n{result.stderr}")
        sys.exit(1)
    print(f"  Binário NVS gerado: {bin_path}")


# ─────────────────────────────────────────────
# Flash via esptool
# ─────────────────────────────────────────────

# Offset padrão da partição NVS no esquema de partição VitalKare
# Ajuste se usar partitions.csv diferente
NVS_FLASH_OFFSET = "0x9000"


def flash_nvs(bin_path: str, port: str, baud: int) -> None:
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "--port", port,
        "--baud", str(baud),
        "write_flash",
        NVS_FLASH_OFFSET, bin_path,
    ]
    print(f"\n  Gravando NVS em {port} @ {baud} bps...")
    print(f"  Comando: {' '.join(cmd)}\n")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("\n  ❌ Flash falhou. Verifique a porta e se a placa está em modo bootloader.")
        sys.exit(1)
    print("\n  ✅ NVS gravada com sucesso!")


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="VitalKare — Provisioning NVS")
    parser.add_argument("--port", default="COM16", help="Porta serial (default: COM16)")
    parser.add_argument("--baud", type=int, default=460800, help="Baud rate (default: 460800)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Gera CSV e BIN sem fazer o flash")
    args = parser.parse_args()

    params = collect_params()

    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = os.path.join(tmpdir, "nvs_data.csv")
        bin_path = os.path.join(tmpdir, "nvs.bin")

        print("\n[ Gerando partição NVS ]")
        write_nvs_csv(params, csv_path)
        generate_nvs_bin(csv_path, bin_path)

        if args.dry_run:
            import shutil
            out = os.path.join(os.path.dirname(__file__), "nvs_preview.bin")
            shutil.copy(bin_path, out)
            print(f"\n  Dry-run: binário salvo em {out}")
        else:
            flash_nvs(bin_path, args.port, args.baud)

    print("\n  Reinicie a placa para aplicar as configurações.\n")


if __name__ == "__main__":
    main()
