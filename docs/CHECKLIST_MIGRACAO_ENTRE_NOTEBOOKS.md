# VitalKare — Checklist de checagem entre notebooks

> Guia prático para reduzir problemas ao alternar o projeto entre dois notebooks diferentes.

---

## Objetivo

Este checklist ajuda a identificar o que pode quebrar ao trabalhar em dois ambientes distintos, principalmente em:

- paths locais
- variáveis de ambiente
- IPs de rede
- dependências instaladas
- firmware provisionado
- diferenças de versão entre ferramentas

---

## 1. Confirmar se o projeto depende de caminho absoluto

### O que checar

1. Se os scripts usam o caminho da pasta atual do projeto de forma relativa.
2. Se existe algum `C:\...`, `D:\...` ou referência ao nome do usuário dentro do código ou dos scripts.
3. Se algum arquivo `.env`, `.bat`, `.ps1` ou configuração salva aponta para uma pasta específica do notebook antigo.

### Neste projeto

- `start.ps1` usa a raiz do repositório com base no próprio arquivo.
- `system-startup-assistant.ps1` também resolve a raiz do projeto de forma relativa.
- `system-startup-assistant.bat` e `firmware-assistant.bat` usam `%~dp0`, então acompanham a pasta onde estão.

### Risco real

O projeto em si **não parece preso ao path da pasta do repositório**, mas há dependências externas que podem usar caminhos fixos no sistema.

---

## 2. Confirmar ferramentas instaladas e no PATH

### O que checar

No notebook novo, validar se os comandos abaixo funcionam no terminal:

```powershell
python --version
node --version
npm --version
git --version
docker --version
```

### O que pode dar errado

- Python não instalado ou fora do `PATH`
- Node/NPM ausentes
- Docker Desktop não instalado
- Git ausente

### Impacto

- `start.ps1` depende de Python e Node
- dashboard depende de `npm install` e `npm run dev`
- backend depende de Python e `venv`
- fluxo com containers depende de Docker

---

## 3. Confirmar ESP-IDF e dependências de firmware

### O que checar

1. Se o ESP-IDF está instalado no notebook novo.
2. Se ele está em um caminho compatível com os scripts atuais.
3. Se o ambiente do IDF carrega corretamente.

### Ponto crítico deste projeto

Os scripts de firmware procuram o ESP-IDF em `C:\esp\...`.

### O que pode dar errado

- ESP-IDF instalado em outra pasta
- pasta `C:\esp` inexistente
- `export.ps1` não encontrado
- `IDF_PATH` ausente ou inconsistente
- `esptool` não instalado no Python usado

### Como checar

```powershell
Get-ChildItem C:\esp
Test-Path C:\esp
```

Se quiser validar o fluxo do IDF manualmente:

```powershell
. C:\esp\<versao>\esp-idf\export.ps1
```

### Observação

Se o notebook novo tiver o IDF em outro local, os scripts atuais podem precisar de ajuste ou o ambiente precisará seguir o mesmo padrão do notebook antigo.

---

## 4. Confirmar variáveis de ambiente e arquivos `.env`

### O que checar

1. Se os arquivos `.env` necessários existem no notebook novo.
2. Se foram copiados a partir dos arquivos `.example`.
3. Se os valores fazem sentido para o ambiente atual.

### Arquivos importantes

- `dashboard\.env.local`
- `backend\.env` se o backend dessa parte do projeto estiver sendo usado
- `docker\.env`

### O que pode dar errado

- arquivo não existe
- arquivo existe com valores do notebook antigo
- senha, host, porta ou URL não batem com o novo ambiente
- dashboard tentando falar com IP antigo

### Exemplos de campos sensíveis

- `VITE_BACKEND_HOST`
- `VITE_WS_URL`
- `HOST_LAN_IP`
- `DATABASE_URL`
- `REDIS_URL`
- `MQTT_HOST`
- `SECRET_KEY`

---

## 5. Confirmar IP da LAN do notebook atual

### O que checar

1. Qual IP local o notebook atual recebeu na rede.
2. Se esse IP mudou em relação ao outro notebook.
3. Se dashboard, backend, Docker e firmware estão usando esse IP correto.

### Neste projeto

- `dashboard\.env.local` guarda `VITE_BACKEND_HOST` e `VITE_WS_URL`
- `docker\.env` usa `HOST_LAN_IP`
- o provisionamento da placa grava `server_host` na NVS
- `system-startup-assistant.ps1` detecta e propaga o IP automaticamente

### O que pode dar errado

- o notebook novo recebe outro IP
- a placa continua tentando conectar no IP do notebook antigo
- o dashboard aponta para backend antigo ou inexistente
- serviços funcionam localmente, mas a placa não alcança o host correto

### Boa prática

Sempre revalidar o IP ao trocar de notebook ou de rede Wi-Fi.

---

## 6. Confirmar portas e conflitos locais

### O que checar

As portas abaixo precisam estar livres ou sendo usadas pelo processo correto:

- `7890` — TCP do servidor
- `8000` — HTTP/API
- `5173` — dashboard Vite

### O que pode dar errado

- outro processo ocupando a porta
- serviço antigo do outro ambiente ainda aberto
- dashboard subindo, mas apontando para backend errado

### Sinal típico

O script inicia parcialmente, mas algum serviço não responde ou sobe em erro.

---

## 7. Confirmar provisionamento da placa

### O que checar

1. Se a placa foi provisionada para o IP atual do servidor.
2. Se SSID e senha da rede continuam válidos.
3. Se `device_id`, `ward_id` e demais dados continuam coerentes.

### O que pode dar errado

- Wi-Fi diferente entre os dois ambientes
- senha do Wi-Fi mudou
- IP do servidor mudou
- placa segue com configuração gravada do notebook antigo

### Sintoma comum

O backend sobe normalmente, mas o dispositivo não aparece nem conecta.

---

## 8. Confirmar versões das ferramentas

### O que checar

Comparar entre os dois notebooks:

- versão do Python
- versão do Node.js
- versão do NPM
- versão do Docker
- versão do ESP-IDF

### O que pode dar errado

- dependência instala em um notebook e falha no outro
- diferença de comportamento no build
- scripts funcionam em uma versão e falham em outra

### Boa prática

Manter versões o mais próximas possível entre as duas máquinas.

---

## 9. Confirmar dependências locais recriadas no notebook novo

### O que checar

1. Se a `venv` do servidor foi recriada localmente.
2. Se `npm install` foi rodado no dashboard.
3. Se dependências do firmware estão acessíveis no novo ambiente.

### O que pode dar errado

- copiar pasta com `venv` de outro notebook e quebrar por path interno
- copiar `node_modules` entre máquinas e gerar inconsistência
- usar cache antigo de build

### Recomendação

No notebook novo, prefira recriar artefatos locais em vez de reaproveitar:

- `venv`
- `node_modules`
- builds temporários

---

## 10. Confirmar arquivos que não deveriam ser compartilhados cegamente

### Itens para revisar antes de copiar

- `.env`
- `.env.local`
- `docker\.env`
- `server\venv`
- `dashboard\node_modules`
- builds do firmware
- caches e arquivos temporários

### Motivo

Esses arquivos costumam carregar:

- paths internos da máquina
- credenciais locais
- IPs específicos
- dependências compiladas

---

## 11. Checagem rápida ao trocar de notebook

Use esta ordem:

1. Confirmar `python`, `node`, `npm`, `git` e `docker`
2. Confirmar instalação e localização do ESP-IDF
3. Confirmar IP atual da LAN
4. Revisar `dashboard\.env.local`
5. Revisar `docker\.env`
6. Recriar `venv` e instalar dependências do backend/server
7. Rodar `npm install` no dashboard
8. Subir backend e dashboard
9. Testar portas `7890`, `8000` e `5173`
10. Reprovisionar a placa se o IP/SSID mudou

---

## 12. Sinais de problema e causa provável

| Sintoma | Causa provável |
|---|---|
| Script abre, mas backend não sobe | Python ausente, `venv` quebrada ou dependências faltando |
| Dashboard sobe, mas não carrega dados | `VITE_BACKEND_HOST` ou `VITE_WS_URL` apontando para host errado |
| Placa não conecta | IP do servidor antigo gravado na NVS, Wi-Fi errado ou TCP inacessível |
| Assistente de firmware falha logo no início | ESP-IDF fora de `C:\esp` ou `export.ps1` não encontrado |
| Docker sobe parcialmente | `.env` incorreto ou portas já ocupadas |
| Funciona em um notebook e no outro não | diferença de versão, PATH, IP, `.env` ou dependências locais |

---

## 13. Conclusão prática

O principal risco ao alternar entre notebooks **não é o path da pasta do repositório**, porque os scripts principais usam caminhos relativos. Os problemas mais prováveis estão em:

- ferramentas instaladas no sistema
- ESP-IDF em caminho diferente
- variáveis de ambiente locais
- IP da LAN diferente
- firmware provisionado com host antigo
- dependências locais recriadas de forma incompleta

Se essas checagens forem feitas sempre que trocar de máquina ou de rede, a chance de quebra cai bastante.
