#!/usr/bin/env python3
"""
provision.py — Grava credenciais WiFi/servidor na partição NVS do ESP32-S3

Uso:
    python provision.py --port COM16
    python provision.py --port COM16 --baud 115200

Dependências:
    pip install esptool
    idf.py deve estar no PATH (ou ESPIDF definido), para usar nvs_partition_gen.py
"""

import argparse
import csv
import ipaddress
import os
import subprocess
import sys
import tempfile

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

def ask_or_value(
    label: str,
    value: str | None,
    default: str = "",
    *,
    non_interactive: bool = False,
    required: bool = False,
) -> str:
    if value is not None:
        return value
    if non_interactive:
        if default:
            return default
        if required:
            raise SystemExit(f"Campo obrigatório ausente em modo não interativo: {label}")
        return ""
    return ask(label, default)


def ask_int_or_value(
    label: str,
    value: int | None,
    default: int,
    *,
    non_interactive: bool = False,
) -> int:
    if value is not None:
        return value
    if non_interactive:
        return default
    return ask_int(label, default)


def validate_ipv4(value: str, label: str) -> str:
    try:
        return str(ipaddress.IPv4Address(value))
    except ipaddress.AddressValueError as exc:
        raise SystemExit(f"{label} invalido: {value}") from exc


def resolve_static_ip_config(args: argparse.Namespace) -> dict:
    if args.use_static_ip and args.clear_static_ip:
        raise SystemExit("Use apenas uma opcao: --use-static-ip ou --clear-static-ip")

    if args.use_static_ip:
        if not args.static_ip or not args.gateway or not args.netmask:
            raise SystemExit(
                "IP fixo exige --static-ip, --gateway e --netmask"
            )
        return {
            "enabled": True,
            "ip": validate_ipv4(args.static_ip, "IP fixo"),
            "gateway": validate_ipv4(args.gateway, "Gateway"),
            "netmask": validate_ipv4(args.netmask, "Mascara"),
        }

    return {
        "enabled": False,
        "ip": "",
        "gateway": "",
        "netmask": "",
    }


def collect_params(args: argparse.Namespace) -> dict:
    print("\n╔══════════════════════════════════════╗")
    print("║  VitalKare — Provisionamento NVS     ║")
    print("╚══════════════════════════════════════╝\n")

    print("[ Wi-Fi ]")
    ssid = ask_or_value("SSID da rede Wi-Fi", args.ssid, non_interactive=args.non_interactive, required=True)
    wifi_pass = ask_or_value("Senha da rede Wi-Fi", args.wifi_pass, non_interactive=args.non_interactive, required=True)

    print("\n[ Servidor VitalKare ]")
    server_host = ask_or_value("IP do servidor VitalKare (TCP/HTTP)", args.server_host, "192.168.1.100", non_interactive=args.non_interactive)
    tcp_port = ask_int_or_value("Porta TCP do servidor", args.tcp_port, 7890, non_interactive=args.non_interactive)

    print("\n[ Compatibilidade MQTT legada ]")
    mqtt_port = ask_int_or_value("Porta MQTT", args.mqtt_port, 1883, non_interactive=args.non_interactive)
    mqtt_user = ask_or_value("Usuário MQTT", args.mqtt_user, "vitalkare", non_interactive=args.non_interactive)
    mqtt_pass = ask_or_value("Senha MQTT", args.mqtt_pass, "vitalkare_mqtt", non_interactive=args.non_interactive)

    print("\n[ Dispositivo ]")
    device_id = ask_or_value("ID do dispositivo (ex: leito01)", args.device_id, "leito01", non_interactive=args.non_interactive)
    ward_id = ask_or_value("ID da ala (ex: ala-a)", args.ward_id, "ala-a", non_interactive=args.non_interactive)
    patient_id = ask_or_value("ID do paciente (ex: PAC-000)", args.patient_id, "PAC-000", non_interactive=args.non_interactive)

    return {
        "ssid":       ssid,
        "pass":       wifi_pass,
        "server_host": server_host,
        "tcp_port":   str(tcp_port),
        "mqtt_host":  server_host,
        "mqtt_port":  str(mqtt_port),
        "mqtt_user":  mqtt_user,
        "mqtt_pass":  mqtt_pass,
        "device_id":  device_id,
        "ward_id":    ward_id,
        "patient_id": patient_id,
        "static_ip":  resolve_static_ip_config(args),
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
        ["wifi_static_en", "data",   "u8",     NVS_NAMESPACE, "1" if params["static_ip"]["enabled"] else "0"],
        # servidor
        ["server_host", "data",      "string", NVS_NAMESPACE, params["server_host"]],
        ["tcp_port",    "data",      "u16",    NVS_NAMESPACE, params["tcp_port"]],
        # legado MQTT
        ["mqtt_host",   "data",      "string", NVS_NAMESPACE, params["mqtt_host"]],
        ["mqtt_port",   "data",      "u16",    NVS_NAMESPACE, params["mqtt_port"]],
        ["mqtt_user",   "data",      "string", NVS_NAMESPACE, params["mqtt_user"]],
        ["mqtt_pass",   "data",      "string", NVS_NAMESPACE, params["mqtt_pass"]],
        # device
        ["device_id",   "data",      "string", NVS_NAMESPACE, params["device_id"]],
        ["ward_id",     "data",      "string", NVS_NAMESPACE, params["ward_id"]],
        ["patient_id",  "data",      "string", NVS_NAMESPACE, params["patient_id"]],
    ]
    if params["static_ip"]["enabled"]:
        rows.extend([
            ["wifi_static_ip", "data", "string", NVS_NAMESPACE, params["static_ip"]["ip"]],
            ["wifi_static_gw", "data", "string", NVS_NAMESPACE, params["static_ip"]["gateway"]],
            ["wifi_static_nm", "data", "string", NVS_NAMESPACE, params["static_ip"]["netmask"]],
        ])
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
    parser.add_argument("--ssid", help="SSID da rede Wi-Fi")
    parser.add_argument("--wifi-pass", dest="wifi_pass", help="Senha da rede Wi-Fi")
    parser.add_argument("--server-host", help="IP/host do servidor VitalKare")
    parser.add_argument("--tcp-port", type=int, help="Porta TCP do servidor")
    parser.add_argument("--mqtt-port", type=int, help="Porta MQTT legada")
    parser.add_argument("--mqtt-user", help="Usuário MQTT legado")
    parser.add_argument("--mqtt-pass", help="Senha MQTT legada")
    parser.add_argument("--device-id", help="ID do dispositivo")
    parser.add_argument("--ward-id", help="ID da ala")
    parser.add_argument("--patient-id", help="ID do paciente")
    parser.add_argument("--use-static-ip", action="store_true",
                        help="Grava a configuracao para usar IP fixo na interface Wi-Fi")
    parser.add_argument("--clear-static-ip", action="store_true",
                        help="Limpa a configuracao de IP fixo e volta a usar DHCP")
    parser.add_argument("--static-ip", help="Endereco IPv4 fixo da placa")
    parser.add_argument("--gateway", help="Gateway IPv4 para o IP fixo")
    parser.add_argument("--netmask", help="Mascara IPv4 para o IP fixo")
    parser.add_argument("--non-interactive", action="store_true",
                        help="Usa defaults para campos opcionais sem abrir prompts")
    parser.add_argument("--dry-run", action="store_true",
                        help="Gera CSV e BIN sem fazer o flash")
    args = parser.parse_args()

    params = collect_params(args)

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
