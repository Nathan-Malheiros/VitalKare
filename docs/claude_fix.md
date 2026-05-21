# VitalKare — Auditoria Técnica e Registro de Correções

> Gerado por auditoria completa do projeto em 2026-05-20.
> Cobre: firmware (ESP32-S3/KIIRA V2), servidor MVP (`server/`), backend de produção (`backend/`), dashboard (React 18).

---

## 1. Entendimento do Projeto

### Stack Ativa (o que de fato roda)

O stack **ativo** é o MVP:

```
ESP32-S3 KIIRA V2
    │  VK Binary Protocol (TCP :7890)
    ▼
server/server.py       ← asyncio TCP + uvicorn HTTP, porta 8000
server/api.py          ← FastAPI REST + WebSocket /ws/dashboard
server/tcp_handler.py  ← por conexão ESP32, parseia VK frames
server/db.py           ← SQLite (aiosqlite)
    │
    ▼  WebSocket
dashboard/             ← React 18 + Zustand + Tailwind
```

O diretório `backend/` (FastAPI + PostgreSQL + MQTT + Redis + MinIO) é a **arquitetura de produção futura** — não está rodando. O `start.ps1` e o `system-startup-assistant.ps1` iniciam apenas `server/server.py` + `npm run dev`.

### Protocolo de Comunicação Firmware ↔ Servidor

**VK Binary Protocol** — frames TCP com:
- Magic: `0x56 0x4B` ("VK")
- Header: magic(2) + version(1) + pkt_type(1) + payload_len(4 LE) + seq(2)
- CRC-16/CCITT (poly=0x1021, init=0xFFFF) ao final
- Tipos: HANDSHAKE_HELLO/ACK, HEARTBEAT_PING/PONG, EVENT_REQUEST/EMERGENCY, AUDIO_SESSION_START/CHUNK/END, TELEMETRY_ENV/OXIMETER/SYSTEM

### Fluxo de Autenticação Dashboard

1. Login → `POST /api/auth/login` (MVP server)
2. Token JWT HS256 (12h) armazenado em Zustand persist + `localStorage.access_token`
3. WebSocket `/ws/dashboard` — envia `{"type":"auth","token":"..."}` na abertura
4. Axios interceptor lê `localStorage.access_token` e injeta `Authorization: Bearer`

---

## 2. Mapeamento de Arquitetura

### Firmware (ESP32-S3)

| Componente | Arquivo | Estado |
|---|---|---|
| Main / orquestrador | `firmware/main/main.c` | Sólido |
| Protocolo VK wire | `network/vk_protocol.c` | Sólido |
| TCP TX/RX (tasks separadas) | `network/tcp_client.c` | Sólido |
| Reliability / retransmissão | `network/vk_reliability.c` | Implementado |
| Watchdog HW | `watchdog/watchdog.c` | Correto (delegado a task_monitor) |
| Áudio I2S | `audio/` | **Desativado** (`#if 0`) — conflito GPIO37/PSRAM |
| Câmera OV5640 | `camera/` | Condicional `VK_BOARD_HAS_CAMERA` |
| Sensores HDC1080/MPU6050/LDR | `sensors/` | Condicional `VK_BOARD_HAS_ENV_SENSORS` |
| Display OLED | `display/` | Condicional `VK_BOARD_HAS_DISPLAY` |

### Servidor MVP (`server/`)

| Arquivo | Função |
|---|---|
| `server.py` | Entry point, asyncio TCP + uvicorn HTTP |
| `api.py` | FastAPI: auth, devices, events, WS, admin, provisioned slots |
| `tcp_handler.py` | Parseia frames VK, atualiza DB, broadcast WS |
| `db.py` | SQLite via aiosqlite, schema auto-criado |
| `ws_manager.py` | Singleton WSManager, broadcast/send |
| `vk_proto.py` | CRC-16, try_parse_frame, parsers de pacotes |

### Dashboard (`dashboard/src/`)

| Caminho | Estado |
|---|---|
| `pages/Dashboard.tsx` | Ativo — usa `devicesStore` + `useWebSocket` |
| `pages/Admin.tsx` | Ativo — gestão de dispositivos e commissioning |
| `store/devicesStore.ts` | Ativo |
| `store/authStore.ts` | Ativo — **BUG-01** |
| `services/api.ts` | Ativo — **BUG-02** |
| `hooks/useWebSocket.ts` | Ativo |
| `components/devices/DeviceCard.tsx` | Ativo |
| `store/bedsStore.ts` | **Morto** — arquitetura de produção |
| `store/alertsStore.ts` | **Morto** |
| `services/websocketService.ts` | **Morto** |
| `components/beds/BedCard.tsx` | **Morto** |
| `components/beds/BedGrid.tsx` | **Morto** |
| `components/beds/PatientModal.tsx` | **Morto** |
| `components/alerts/EmergencyBanner.tsx` | **Morto** |

---

## 3. Problemas Detectados

### BUG-01 [CRÍTICO] — Logout não limpa localStorage

**Arquivo:** `dashboard/src/store/authStore.ts:30-32`

```typescript
logout: () => {
  set({ accessToken: null, operatorName: null, operatorRole: null })
  // FALTA: localStorage.removeItem('access_token')
},
```

O interceptor em `api.ts` lê `localStorage.getItem('access_token')`. Após o logout, o Zustand é limpo, mas o token persiste no localStorage. Cada requisição Axios depois do logout continua enviando `Authorization: Bearer <token_expirado>` até o próximo reload da página.

**Impacto:** Qualquer chamada API após logout inclui o token antigo — comportamento incorreto e potencial risco de segurança em shared machines.

---

### BUG-02 [MÉDIO] — Interceptor de refresh chama endpoint inexistente

**Arquivo:** `dashboard/src/services/api.ts:53`

```typescript
const { data } = await axios.post<{ access_token: string }>('/api/v1/auth/refresh', {
  refresh_token: refreshToken,
})
```

O servidor MVP (`server/api.py`) não tem rota `/api/v1/auth/refresh`. Na prática não causa crash porque `refresh_token` nunca é armazenado no localStorage (login só guarda `access_token`), então `refreshToken` é sempre `null` e `_redirectToLogin()` é chamado imediatamente. Porém o código é enganoso e desnecessariamente complexo.

**Impacto:** Código morto enganoso; em teoria poderia causar loop de redirect em edge cases.

---

### BUG-03 [MÉDIO] — Status EMERGENCY sobrescrito por heartbeat no servidor MVP

**Arquivo:** `server/tcp_handler.py:212-215`

```python
if t == vk.Pkt.HEARTBEAT_PING:
    pong = vk.build_pong(next_seq())
    await send(pong)
    await db.update_device_status(device_id, "ONLINE")  # BUG: sobrescreve EMERGENCY
```

Quando um dispositivo em estado `EMERGENCY` envia um heartbeat (o firmware continua enviando heartbeats mesmo em emergência), o status é revertido para `ONLINE` antes do operador confirmar o atendimento.

**Impacto:** Banner de emergência some do dashboard sem o operador confirmar — emergências podem ser silenciadas automaticamente.

---

### BUG-04 [BAIXO] — Imports inline em api.py

**Arquivo:** `server/api.py:287-303`

```python
async def ws_dashboard(websocket: WebSocket) -> None:
    ...
    import asyncio   # ← inline import
    ...
    import json      # ← inline import
```

`asyncio` e `json` são módulos stdlib que devem estar importados no topo do arquivo, não dentro de funções. Causam leve overhead na primeira chamada e tornam o código difícil de auditar.

**Impacto:** Nenhum funcional, mas bad practice.

---

### BUG-05 [BAIXO] — Variável `handleWsStatus` morta em Dashboard.tsx

**Arquivo:** `dashboard/src/pages/Dashboard.tsx:174`

```typescript
const handleWsStatus = useCallback((connected: boolean) => setWsOk(connected), [])
useWebSocket(handleMessage, 2000)
```

`handleWsStatus` é definida mas nunca passada para `useWebSocket` (que não aceita esse parâmetro). Gera warning de lint/TypeScript.

**Impacto:** Nenhum funcional. Warning de variável não utilizada.

---

### BUG-06 [MÉDIO] — Backend produção: heartbeat sobrescreve EMERGENCY

**Arquivo:** `backend/app/core/heartbeat_monitor.py:66,78-80`

```python
result = await session.execute(
    select(Bed).where(Bed.status != BedStatus.OFFLINE)  # inclui EMERGENCY
)
...
if lh < critical_threshold:
    if bed.status != BedStatus.OFFLINE:
        bed.status = BedStatus.OFFLINE  # sobrescreve EMERGENCY
```

Mesmo problema do BUG-03 mas no backend de produção. Qualquer leito em EMERGENCY que pare de enviar heartbeat por >90s terá o status mudado para OFFLINE pelo monitor.

**Impacto:** (No backend de produção) — emergências podem ser silenciadas automaticamente se o dispositivo desconectar.

---

### BUG-07 [BAIXO] — Código morto no dashboard

Arquivos não importados por nenhuma página/componente ativo:

- `dashboard/src/store/bedsStore.ts`
- `dashboard/src/store/alertsStore.ts`
- `dashboard/src/services/websocketService.ts`
- `dashboard/src/components/beds/BedCard.tsx`
- `dashboard/src/components/beds/BedGrid.tsx`
- `dashboard/src/components/beds/PatientModal.tsx`
- `dashboard/src/components/alerts/EmergencyBanner.tsx`

São remanescentes da arquitetura de produção (`backend/`) e não são carregados em nenhum bundle ativo.

**Impacto:** Confusão de código. Sem impacto funcional.

---

## 4. Análise de Causa Raiz

| Bug | Causa raiz |
|---|---|
| BUG-01 | Dois locais de armazenamento de token (Zustand persist + localStorage) dessincronizados no logout |
| BUG-02 | Lógica de refresh copiada de arquitetura de produção sem adaptar ao MVP |
| BUG-03 | Ausência de guarda de estado ao atualizar status no heartbeat |
| BUG-04 | Imports colocados dentro da função para evitar circular import (provavelmente), depois não foram movidos |
| BUG-05 | Parâmetro removido do hook useWebSocket mas callback não foi removido do caller |
| BUG-06 | Query filtra apenas OFFLINE mas não protege EMERGENCY |
| BUG-07 | Código da arquitetura de produção não removido ao simplificar para MVP |

---

## 5. Correções Aplicadas

### FIX-01 — authStore.ts: logout limpa localStorage

**Arquivo:** `dashboard/src/store/authStore.ts`
**Data:** 2026-05-20

Adicionado `localStorage.removeItem('access_token')` ao método `logout()`.

---

### FIX-02 — api.ts: simplificar handler de 401

**Arquivo:** `dashboard/src/services/api.ts`
**Data:** 2026-05-20

Removida lógica de refresh token morta (tentava `/api/v1/auth/refresh` inexistente no MVP). O handler agora vai direto para `_redirectToLogin()` em qualquer 401, que é o comportamento real de qualquer forma.

---

### FIX-03 — tcp_handler.py: não sobrescrever EMERGENCY no heartbeat

**Arquivo:** `server/tcp_handler.py`
**Data:** 2026-05-20

Adicionada função `update_device_heartbeat()` no `db.py` que usa `CASE WHEN` para preservar status EMERGENCY durante update de `last_seen`. Substituída chamada `update_device_status("ONLINE")` no handler de heartbeat.

---

### FIX-04 — api.py: mover imports para o topo

**Arquivo:** `server/api.py`
**Data:** 2026-05-20

Movidos `import asyncio` e `import json` do corpo de função para os imports do módulo.

---

### FIX-05 — Dashboard.tsx: remover variável morta

**Arquivo:** `dashboard/src/pages/Dashboard.tsx`
**Data:** 2026-05-20

Removida linha com `handleWsStatus` nunca utilizada.

---

### FIX-06 — heartbeat_monitor.py: preservar status EMERGENCY

**Arquivo:** `backend/app/core/heartbeat_monitor.py`
**Data:** 2026-05-20

Adicionado filtro `Bed.status != BedStatus.EMERGENCY` na query que busca leitos a verificar.

---

## 6. Arquivos Modificados

| Arquivo | Fix |
|---|---|
| `dashboard/src/store/authStore.ts` | FIX-01 |
| `dashboard/src/services/api.ts` | FIX-02 |
| `server/db.py` | FIX-03 (nova função) |
| `server/tcp_handler.py` | FIX-03 |
| `server/api.py` | FIX-04 |
| `dashboard/src/pages/Dashboard.tsx` | FIX-05 |
| `backend/app/core/heartbeat_monitor.py` | FIX-06 |

---

## 7. Decisões Importantes

1. **Não excluir código morto do dashboard** — os arquivos `bedsStore.ts`, `BedCard.tsx`, etc. representam a arquitetura de produção pretendida. Serão necessários quando o backend PostgreSQL for ativado. Mantidos como estão.

2. **Não simplificar o servidor MVP em backend de produção** — os dois coexistem; o MVP é o stack operacional atual e o `backend/` está em desenvolvimento incremental.

3. **Nova função `update_device_heartbeat()` em db.py em vez de alterar `update_device_status()`** — `update_device_status` é chamada em vários contextos (incluindo reset forçado); seria perigoso adicionar guarda de EMERGENCY globalmente. Melhor ter uma função específica para heartbeat.

4. **Audio desativado intencionalmente (`#if 0`)** — conflito GPIO37/PSRAM no perfil KIIRA V2. Não é um bug — é decisão de hardware documentada.

---

## 8. Problemas Remanescentes (não corrigidos nesta sessão)

| ID | Descrição | Razão de não corrigir |
|---|---|---|
| R01 | `db.py` usa SHA-256 simples para senha (não bcrypt) | MVP design: intencional para simplicidade, sem bcrypt dep |
| R02 | JWT secret padrão `vitalkare-mvp-secret-2024` hardcoded | Aceito em ambiente de bancada; produção usa `VK_JWT_SECRET` env var |
| R03 | `environment_reading.py` composite PK duplo | Consistente com migration `0001_initial_schema.py`; não causa crash |
| R04 | `backend/app/api/v1/routes/beds.py` POST sem Pydantic validation | Backend produção não está rodando; baixo risco imediato |
| R05 | Dead code files no dashboard | Mantidos intencionalmente (ver Decisão 1) |

---

## 9. Observações de Runtime

- O servidor MVP (`server/server.py`) executa TCP e HTTP no **mesmo event loop asyncio**. Cada conexão TCP recebe uma coroutine — não há bloqueio entre elas. O uvicorn usa `loop="none"` para compartilhar o loop.
- O `RECV_BUF = 4096` no tcp_handler pode fragmentar frames VK grandes (ex: chunks de áudio). O `buf.extend()` acumula dados e o loop de parsing consome frames completos — isso está correto.
- Timeout de 90s no TCP handler (`TCP_TIMEOUT_S`) é adequado: o firmware envia heartbeat a cada ~30s.
- A função `try_parse_frame` em `vk_proto.py` retorna `(None, 1)` para bytes inválidos (resync) e `(None, 0)` para frame incompleto — o handler distingue corretamente.

---

## 10. Recomendações Futuras

1. **Ativar bcrypt no servidor MVP** — substituir SHA-256 por bcrypt (já está no requirements.txt).
2. **Criar endpoint `GET /api/devices/{id}/events`** no servidor MVP para histórico por dispositivo no Admin.
3. **Persistir `reconnect_count` por dispositivo** — útil para diagnóstico de instabilidade de rede.
4. **Definir `VK_JWT_SECRET` via variável de ambiente** em qualquer deploy não-desenvolvimento.
5. **Implementar `audio_ctrl_start/stop`** no firmware quando GPIO37 for roteado corretamente na KIIRA V2 final.

---

## 11. Status Final

| Camada | Estado |
|---|---|
| Servidor MVP (`server/`) | ✅ Estável — correções aplicadas |
| Dashboard (`dashboard/`) | ✅ Estável — correções aplicadas |
| Firmware ESP32-S3 | ✅ Estável — sem bugs críticos encontrados |
| Backend produção (`backend/`) | ⚠️ Em desenvolvimento — correção de heartbeat aplicada |

**Para iniciar o sistema:**
```powershell
# Na raiz do projeto
.\start.ps1
# ou
.\system-startup-assistant.bat
```

Dashboard em: `http://localhost:5173`  
API docs: `http://localhost:8000/docs`  
Login: `admin / admin`
