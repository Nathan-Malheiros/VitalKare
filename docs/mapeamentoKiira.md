# KIIRA V2 — Mapeamento Completo da Placa

> Plataforma educacional baseada em ESP32-S3 voltada para Robótica, IoT, Machine Learning, Visão Computacional e IA Embarcada. 

---

# Visão Geral

A **KIIRA V2** é uma placa de desenvolvimento integrada baseada no microcontrolador **ESP32-S3**, contendo sensores, atuadores, câmera, armazenamento e periféricos embarcados para acelerar o desenvolvimento de projetos de:

* Robótica
* IoT
* Inteligência Artificial
* Machine Learning
* Visão Computacional
* Automação
* Sistemas Embarcados

A proposta da placa é evitar a necessidade de módulos externos, centralizando tudo em uma única plataforma. 

---

# Microcontrolador Principal

## ESP32-S3

### Características

* Dual-Core Xtensa LX7
* Wi-Fi 802.11 b/g/n
* Bluetooth 5.0 LE
* Suporte a IA embarcada
* USB nativo
* PSRAM integrada
* Operação em 3.3V

### Interface de Programação

* USB-C
* CDC/JTAG embarcado



---

# Arquitetura Geral da Placa

## Subsistemas Integrados

| Subsistema    | Função                 |
| ------------- | ---------------------- |
| Câmera OV5640 | Visão computacional    |
| Microfone PDM | Captura de áudio       |
| OLED SSD1306  | Interface gráfica      |
| HDC1080       | Temperatura e umidade  |
| MPU6050       | Movimento e orientação |
| Cartão SD     | Armazenamento          |
| LDR           | Sensor de luminosidade |
| Trimpot       | Entrada analógica      |
| Botões        | Entrada digital        |
| Relé          | Controle de cargas     |
| MOSFET        | Controle de potência   |
| LED/Buzzer    | Feedback visual/sonoro |

---

# Mapeamento Completo de GPIOs

# Câmera OV5640

## Função

Módulo de câmera para:

* Reconhecimento de objetos
* IA visual
* Processamento de imagem
* Streaming
* OpenCV embarcado
* ESP-WHO

## Pinagem

| Sinal      | GPIO    |
| ---------- | ------- |
| XCLK       | GPIO 15 |
| SIOD (SDA) | GPIO 4  |
| SIOC (SCL) | GPIO 5  |
| Y9         | GPIO 16 |
| Y8         | GPIO 17 |
| Y7         | GPIO 18 |
| Y6         | GPIO 12 |
| Y5         | GPIO 10 |
| Y4         | GPIO 8  |
| Y3         | GPIO 9  |
| Y2         | GPIO 11 |
| VSYNC      | GPIO 6  |
| HREF       | GPIO 7  |
| PCLK       | GPIO 13 |

## Observações

* Interface paralela de alta velocidade
* Usa muitos GPIOs do ESP32-S3
* Necessita PSRAM para captura de frames maiores



---

# Microfone Digital PDM

## Função

Captura de áudio para:

* Reconhecimento de voz
* Classificação sonora
* IA de áudio
* Assistentes virtuais

## Pinagem

| Sinal    | GPIO    |
| -------- | ------- |
| PDM CLK  | GPIO 2  |
| PDM DATA | GPIO 37 |

## Configuração

| Parâmetro   | Valor  |
| ----------- | ------ |
| Tipo        | PDM    |
| Sample Rate | 16 kHz |

## Observações

* Ideal para Edge Impulse
* Compatível com ESP-DSP
* Baixo consumo



---

# Display OLED SSD1306

## Função

Interface visual para:

* Menus
* Debug
* Status
* Dashboards
* Sensores

## Pinagem

| Sinal | GPIO    |
| ----- | ------- |
| SDA   | GPIO 14 |
| SCL   | GPIO 21 |

## Comunicação

| Item       | Valor   |
| ---------- | ------- |
| Interface  | I2C     |
| Barramento | I2C1    |
| Clock      | 400 kHz |

## Observações

* Compartilha barramento I2C
* Muito usado com biblioteca Adafruit SSD1306



---

# Sensor HDC1080

## Função

Sensor ambiental para:

* Temperatura
* Umidade relativa
* Monitoramento climático

## Pinagem

| Sinal | GPIO    |
| ----- | ------- |
| SDA   | GPIO 14 |
| SCL   | GPIO 21 |

## Configurações

| Item       | Valor   |
| ---------- | ------- |
| Interface  | I2C     |
| Barramento | I2C0    |
| Clock      | 100 kHz |
| Endereço   | 0x40    |

## Observações

* Compartilha barramento com MPU6050
* Sensor de alta precisão



---

# Sensor MPU6050

## Função

IMU de 6 eixos:

* Acelerômetro
* Giroscópio
* Detecção de movimento
* Estabilização
* Navegação

## Pinagem

| Sinal | GPIO    |
| ----- | ------- |
| SDA   | GPIO 14 |
| SCL   | GPIO 21 |

## Configurações

| Item       | Valor   |
| ---------- | ------- |
| Interface  | I2C     |
| Barramento | I2C0    |
| Clock      | 400 kHz |

## Observações

* Excelente para robótica
* Compatível com filtros complementares e Kalman



---

# Leitor de Cartão SD

## Função

Armazenamento de:

* Logs
* Imagens
* Áudio
* Datasets
* Configurações

## Pinagem

| Sinal | GPIO    |
| ----- | ------- |
| CLK   | GPIO 39 |
| CMD   | GPIO 38 |
| D0    | GPIO 40 |

## Configuração

| Item      | Valor  |
| --------- | ------ |
| Interface | SDMMC  |
| Modo      | 1-bit  |
| Clock     | 25 MHz |

## Observações

* Mais rápido que SPI convencional
* Ideal para gravação de câmera



---

# Sensor LDR

## Função

Leitura de luminosidade ambiente.

## Pinagem

| Item | GPIO   |
| ---- | ------ |
| ADC  | GPIO 1 |

## Configuração

| Item      | Valor    |
| --------- | -------- |
| ADC       | ADC1_CH0 |
| Resolução | 12 bits  |
| Faixa     | 0–4095   |

## Observações

* Leitura analógica
* Pode ser usado para automação inteligente



---

# Trimpot (Potenciômetro)

## Função

Entrada analógica ajustável para:

* Testes
* Controle manual
* Calibração

## Pinagem

| Item | GPIO   |
| ---- | ------ |
| ADC  | GPIO 1 |

## Configuração

| Item      | Valor    |
| --------- | -------- |
| ADC       | ADC1_CH0 |
| Resolução | 12 bits  |

## Observações

* Compartilha ADC com o LDR
* Necessário multiplexar via software dependendo da placa



---

# Botões de Entrada

## Função

Interação do usuário.

## Pinagem

| Botão                | GPIO    |
| -------------------- | ------- |
| Botão 1              | GPIO 45 |
| Botão 2              | GPIO 46 |
| Entrada Fotoacoplada | GPIO 35 |

## Observações

* Pull-up interno
* Leitura digital



---

# LED / Buzzer PWM

## Função

Saída configurável para:

* LED indicador
* Alertas sonoros
* PWM

## Pinagem

| Item | GPIO   |
| ---- | ------ |
| PWM  | GPIO 3 |

## Configuração PWM

| Item       | Valor   |
| ---------- | ------- |
| Frequência | 5 kHz   |
| Resolução  | 10 bits |
| Duty Cycle | 0–1023  |

## Observações

* Seleção via chave física
* Pode ser controlado via LEDC do ESP32



---

# Relé

## Função

Controle de cargas externas.

## Pinagem

| Item     | GPIO    |
| -------- | ------- |
| Controle | GPIO 48 |
| Controle | GPIO 47 |

## Características

* Acionamento em nível LOW
* Controle de cargas AC/DC

## Observações

* Compartilha linha com MOSFET
* Seleção via chave



---

# MOSFET

## Função

Controle eletrônico de potência.

## Pinagem

| Item     | GPIO    |
| -------- | ------- |
| Controle | GPIO 48 |
| Controle | GPIO 47 |

## Características

* Acionamento LOW
* Ideal para:

  * LEDs
  * Motores
  * Fitas LED
  * Bombas
  * Solenoides

## Observações

* Seleção via chave física
* Compartilhado com relé



---

# Barramentos da KIIRA V2

# I2C

## GPIOs

| SDA     | SCL     |
| ------- | ------- |
| GPIO 14 | GPIO 21 |

## Dispositivos no barramento

* OLED SSD1306
* HDC1080
* MPU6050

---

# SDMMC

| Sinal | GPIO |
| ----- | ---- |
| CLK   | 39   |
| CMD   | 38   |
| D0    | 40   |

---

# ADC

| GPIO   | Uso           |
| ------ | ------------- |
| GPIO 1 | LDR + Trimpot |

---

# PWM

| GPIO   | Uso        |
| ------ | ---------- |
| GPIO 3 | LED/Buzzer |

---

# GPIOs Utilizados

## GPIOs já ocupados pela placa

```text
1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
12, 13, 14, 15, 16, 17, 18,
21, 35, 37, 38, 39, 40,
45, 46, 47, 48
```

---

# GPIOs Provavelmente Livres

```text
0, 19, 20, 33, 34, 36, 41, 42
```

> Pode variar dependendo da revisão da placa.

---

# Capacidades da KIIRA V2

# IA Embarcada

A placa suporta:

* TinyML
* Edge Impulse
* TensorFlow Lite Micro
* ESP-WHO
* Reconhecimento facial
* Reconhecimento de voz

---

# Visão Computacional

Com a OV5640:

* Captura de imagem
* Streaming MJPEG
* OCR
* Detecção de objetos
* QR Code
* IA visual

---

# Áudio

Com microfone PDM:

* FFT
* Classificação sonora
* Wake words
* Assistente virtual

---

# IoT

Conectividade nativa:

* Wi-Fi
* MQTT
* HTTP
* WebSocket
* Bluetooth LE

---

# Aplicações Indicadas

## Robótica

* Carrinhos autônomos
* Controle PID
* Navegação

## Automação

* Casas inteligentes
* Monitoramento
* Controle remoto

## IA

* Visão computacional
* Classificação sonora
* TinyML

## Sistemas Embarcados

* Dashboards
* Telemetria
* Edge computing

---

# Resumo Técnico Final

| Categoria           | Especificação |
| ------------------- | ------------- |
| MCU                 | ESP32-S3      |
| Clock               | Dual-Core     |
| Wi-Fi               | Sim           |
| Bluetooth           | BLE 5.0       |
| Câmera              | OV5640        |
| Display             | OLED SSD1306  |
| IMU                 | MPU6050       |
| Sensor Temp/Umidade | HDC1080       |
| Áudio               | Microfone PDM |
| Armazenamento       | MicroSD       |
| ADC                 | 12 bits       |
| PWM                 | Sim           |
| USB                 | USB-C         |
| Tensão              | 3.3V          |

---

# Observações Importantes

## Possível conflito ADC

O documento informa que:

* LDR → GPIO 1
* Trimpot → GPIO 1

Então provavelmente:

* Existe multiplexação
  OU
* Os dois compartilham leitura analógica

Isso merece validação elétrica real da placa.

---

## Relé e MOSFET

Relé e MOSFET compartilham:

* GPIO 47
* GPIO 48

A seleção ocorre via chave física da placa.

---

# Fonte

Documentação oficial da KIIRA — Embarcatech V2. 
