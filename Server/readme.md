# ScorchedXB Relay Server

**Presented by Team Resurgent / Darkone83**

The ScorchedXB relay server handles matchmaking and game traffic between Xbox clients. It is a lightweight Python TCP server requiring no database or external dependencies.

---

## Requirements

- Python 3.8 or newer
- Any Linux VPS or home server with a public IP
- One open TCP port (default 10053)

---

## Setup

```bash
# Clone or copy relay_server.py to your server
# No pip installs required -- standard library only

python3 relay_server.py
```

The server starts immediately and logs to both stdout and `relay_server.log` in the same directory.

---

## Configuration

All settings are at the top of `relay_server.py`:

| Setting | Default | Description |
|---|---|---|
| `HOST` | `0.0.0.0` | Listen address. `0.0.0.0` binds all interfaces. |
| `PORT` | `10053` | TCP port. Must match what players enter in Net Settings. |
| `MAX_ROOMS` | `16` | Maximum concurrent rooms. |
| `MAX_PLAYERS` | `4` | Maximum human players per room (2–4). |
| `MIN_PLAYERS` | `2` | Minimum humans required before host can start. |
| `LOBBY_TIMEOUT_S` | `600` | Seconds before an empty room is auto-closed. |
| `RECV_TIMEOUT_S` | `30` | Socket idle timeout before a client is considered dropped. |
| `LOG_FILE` | `relay_server.log` | Log file path. |
| `LOG_TO_FILE` | `True` | Write log to file. |
| `LOG_TO_STDOUT` | `True` | Print log to console. |

---

## Firewall

Open the configured port for inbound TCP:

```bash
# ufw
ufw allow 10053/tcp

# iptables
iptables -A INPUT -p tcp --dport 10053 -j ACCEPT
```

---

## Running as a Service

Create `/etc/systemd/system/scorchedxb-relay.service`:

```ini
[Unit]
Description=ScorchedXB Relay Server
After=network.target

[Service]
ExecStart=/usr/bin/python3 /opt/scorchedxb/relay_server.py
WorkingDirectory=/opt/scorchedxb
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Then enable and start:

```bash
systemctl daemon-reload
systemctl enable scorchedxb-relay
systemctl start scorchedxb-relay
systemctl status scorchedxb-relay
```

---

## Log Output

The server is verbose by design. Every connection, packet, and room event is logged with a timestamp and level tag.

```
[2026-05-26 18:00:00] [INFO ] ScorchedXB Relay Server starting
[2026-05-26 18:00:00] [INFO ] Listen address : 0.0.0.0:10053
[2026-05-26 18:00:00] [INFO ] Listening on 0.0.0.0:10053
[2026-05-26 18:00:01] [INFO ] CONNECT: Darkone83 connected from 1.2.3.4:50001
[2026-05-26 18:00:01] [PKT  ]   RX [Darkone83        ] HELLO (10b)
[2026-05-26 18:00:01] [PKT  ]   TX [Darkone83        ] ROOMLIST (3b)
[2026-05-26 18:00:05] [INFO ] JOIN/CREATE: Darkone83 created room #1 (2 humans + 1 AI)
[2026-05-26 18:00:05] [ROOM ] Room #  1 | host=Darkone83        | humans=1/2 | AI=1 | status=WAITING
[2026-05-26 18:00:20] [INFO ] JOIN: Player2 joined room #1 (slot 1, 2/2)
[2026-05-26 18:00:22] [INFO ] READY: Darkone83 (slot 0) = READY in room #1
[2026-05-26 18:00:24] [INFO ] READY: Player2 (slot 1) = READY in room #1
[2026-05-26 18:00:24] [INFO ] GAMESTART: room #1 | 2 humans + 1 AI | terrain_seed=0xABCD1234 wind_seed=0x5678EF01
```

Voice packets (`SXBP_VOICE`) are not logged individually to avoid flooding the log at 50 packets/sec per talker.

---

## Protocol

All packets are length-prefixed TCP:

```
byte 0   = total packet length (including this byte)
byte 1   = packet type
byte 2+  = payload
```

| ID | Name | Direction | Description |
|---|---|---|---|
| `0xF0` | `HELLO` | Client → Server | Player name on connect |
| `0xF1` | `ROOMLIST` | Server → Client | List of open rooms |
| `0xF2` | `JOIN` | Client → Server | Create or join a room |
| `0xF3` | `ROOMCONFIG` | Client → Server | Host sets room options |
| `0xF4` | `ROOMINFO` | Server → Client | Current room state |
| `0xF5` | `READY` | Client → Server | Player ready toggle |
| `0xF6` | `GAMESTART` | Server → Client | Game starting with shared seeds |
| `0xF7` | `TURN` | Client → Server | Turn submission |
| `0xF8` | `TURN_RELAY` | Server → Client | Turn relayed to other players |
| `0xF9` | `DISCONNECT` | Server → Client | A player dropped |
| `0xFA` | `PING` | Both | Keepalive |
| `0xFB` | `PONG` | Both | Keepalive response |
| `0xFC` | `VOICE` | Both | ADPCM voice frame |

---

## Room Rules

- Minimum **2 human players** required to start
- Maximum **4 total players** per room (humans + AI)
- AI slots are filled locally on each Xbox — AI turns are never sent over the network
- Host sends `READY` to trigger game start once minimum players are present
- If the host disconnects, remaining clients are notified and returned to the lobby

---

## Troubleshooting

**Clients can connect but rooms don't appear**
- Check firewall rules — the port must be open for inbound TCP
- Verify `HOST = "0.0.0.0"` to bind all interfaces

**Players disconnect during game**
- Increase `RECV_TIMEOUT_S` if on a slow connection
- Check that your VPS isn't killing idle TCP connections (some providers do this)

**Server crashes on start**
- Verify Python 3.8+ with `python3 --version`
- Check the port isn't already in use: `ss -tlnp | grep 10053`

---

## Credits

Relay server by Darkone83 · Presented by Team Resurgent