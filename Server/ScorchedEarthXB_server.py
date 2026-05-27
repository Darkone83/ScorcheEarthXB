#!/usr/bin/env python3
"""
relay_server.py -- ScorchedEarthXB TCP relay server.
Presented by Team Resurgent / Darkone83

Verbose diagnostic logging to stdout and relay_server.log.
All configurable constants are at the top of this file.

Packet format (length-prefixed, same as client):
    buf[0] = total length including this byte
    buf[1] = packet type (SXBP_*)
    buf[2..] = payload
"""

import socket
import threading
import struct
import time
import os
import sys

# =============================================================================
# CONFIGURATION -- edit these before deploying
# =============================================================================

HOST            = "0.0.0.0"    # Listen address (0.0.0.0 = all interfaces)
PORT            = 20056         # Must match XBNET_DEFAULT_PORT in xb_net.h
MAX_ROOMS       = 16            # Max concurrent rooms
MAX_PLAYERS     = 4             # Max humans per room (2-4)
MIN_PLAYERS     = 2             # Min humans required to start
LOBBY_TIMEOUT_S = 600           # Seconds before empty room is auto-closed
RECV_TIMEOUT_S  = 30            # Socket recv timeout (keepalive detection)
LOG_FILE        = "relay_server.log"
LOG_TO_FILE     = True          # Also write log to file
LOG_TO_STDOUT   = True          # Print log to console

# =============================================================================
# PACKET TYPE IDs -- must match xb_net.h SXBP_* values exactly
# =============================================================================

SXBP_HELLO       = 0xF0
SXBP_ROOMLIST    = 0xF1
SXBP_JOIN        = 0xF2
SXBP_ROOMCONFIG  = 0xF3
SXBP_ROOMINFO    = 0xF4
SXBP_READY       = 0xF5
SXBP_GAMESTART   = 0xF6
SXBP_TURN        = 0xF7
SXBP_TURN_RELAY  = 0xF8
SXBP_DISCONNECT  = 0xF9
SXBP_PING        = 0xFA
SXBP_PONG        = 0xFB
SXBP_VOICE       = 0xFC

PACKET_NAMES = {
    SXBP_HELLO:      "HELLO",
    SXBP_ROOMLIST:   "ROOMLIST",
    SXBP_JOIN:       "JOIN",
    SXBP_ROOMCONFIG: "ROOMCONFIG",
    SXBP_ROOMINFO:   "ROOMINFO",
    SXBP_READY:      "READY",
    SXBP_GAMESTART:  "GAMESTART",
    SXBP_TURN:       "TURN",
    SXBP_TURN_RELAY: "TURN_RELAY",
    SXBP_DISCONNECT: "DISCONNECT",
    SXBP_PING:       "PING",
    SXBP_PONG:       "PONG",
    SXBP_VOICE:      "VOICE",
}

# =============================================================================
# LOGGING
# =============================================================================

_log_lock = threading.Lock()
_log_file  = None

def log_open():
    global _log_file
    if LOG_TO_FILE:
        _log_file = open(LOG_FILE, "a", buffering=1)

def log_close():
    global _log_file
    if _log_file:
        _log_file.close()
        _log_file = None

def log(msg, level="INFO"):
    ts  = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{ts}] [{level:5s}] {msg}"
    with _log_lock:
        if LOG_TO_STDOUT:
            print(line, flush=True)
        if LOG_TO_FILE and _log_file:
            _log_file.write(line + "\n")

def log_pkt(direction, client_name, pkt_type, length):
    name = PACKET_NAMES.get(pkt_type, f"0x{pkt_type:02X}")
    log(f"  {direction:2s} [{client_name:16s}] {name} ({length}b)", "PKT")

def log_room(room):
    log(f"  Room #{room.room_id:3d} | "
        f"host={room.host_name:16s} | "
        f"humans={room.n_players}/{room.max_humans} | "
        f"AI={room.n_ai} | "
        f"status={'PLAYING' if room.playing else 'WAITING'}", "ROOM")

# =============================================================================
# ROOM
# =============================================================================

class Room:
    _next_id = 1
    _id_lock = threading.Lock()

    def __init__(self, host_client):
        with Room._id_lock:
            self.room_id  = Room._next_id
            Room._next_id += 1

        self.host        = host_client
        self.host_name   = host_client.name
        self.clients     = [host_client]   # slot 0 = host
        self.max_humans  = 2
        self.n_ai        = 0
        self.ai_diff     = [0, 0, 0]
        self.n_rounds    = 1
        self.start_cash  = 1
        self.wind_str    = 2
        self.terrain     = 0
        self.ready       = {}   # nobody ready at start -- host must press Start
        self.playing     = False
        self.created_at  = time.time()
        self.lock        = threading.Lock()

    @property
    def n_players(self):
        return len(self.clients)

    def get_slot(self, client):
        try:
            return self.clients.index(client)
        except ValueError:
            return -1

    def all_ready(self):
        return all(self.ready.get(c, False) for c in self.clients)

    def enough_to_start(self):
        return self.n_players >= MIN_PLAYERS

# =============================================================================
# CLIENT
# =============================================================================

class Client:
    def __init__(self, sock, addr):
        self.sock      = sock
        self.addr      = addr
        self.name      = f"{addr[0]}:{addr[1]}"   # replaced by HELLO name
        self.room      = None
        self.tank_type = 0
        self.color_idx = 0
        self.connected = True
        self.lock      = threading.Lock()

    def send(self, pkt: bytes):
        """Send a length-prefixed packet. Thread-safe."""
        with self.lock:
            try:
                self.sock.sendall(pkt)
                log_pkt("TX", self.name,
                        pkt[1] if len(pkt) >= 2 else 0xFF, len(pkt))
                return True
            except Exception as e:
                log(f"Send error to {self.name}: {e}", "WARN")
                return False

    def recv_exact(self, n: int) -> bytes | None:
        buf = b""
        while len(buf) < n:
            try:
                chunk = self.sock.recv(n - len(buf))
                if not chunk:
                    return None
                buf += chunk
            except Exception:
                return None
        return buf

    def recv_packet(self) -> bytes | None:
        """Receive one length-prefixed packet."""
        hdr = self.recv_exact(1)
        if hdr is None:
            return None
        length = hdr[0]
        if length < 2:
            return None
        rest = self.recv_exact(length - 1)
        if rest is None:
            return None
        pkt = hdr + rest
        log_pkt("RX", self.name, pkt[1], len(pkt))
        return pkt

# =============================================================================
# SERVER STATE
# =============================================================================

_rooms      = {}          # room_id -> Room
_rooms_lock = threading.Lock()

def get_all_rooms():
    with _rooms_lock:
        return list(_rooms.values())

def add_room(room):
    with _rooms_lock:
        _rooms[room.room_id] = room
    log(f"Room #{room.room_id} created by {room.host_name}")
    log_room(room)

def remove_room(room_id):
    with _rooms_lock:
        room = _rooms.pop(room_id, None)
    if room:
        log(f"Room #{room_id} closed")

# =============================================================================
# PACKET BUILDERS
# =============================================================================

def build_roomlist() -> bytes:
    rooms = [r for r in get_all_rooms() if not r.playing]
    n     = min(len(rooms), MAX_ROOMS)
    # Each room entry: room_id(1) nHumans(1) maxHumans(1) nAI(1) status(1) hostName(16) = 21 bytes
    pkt = bytearray()
    pkt += bytes([0, SXBP_ROOMLIST, n])
    for r in rooms[:n]:
        name_bytes = r.host_name.encode("ascii", "replace")[:15].ljust(16, b'\x00')
        pkt += bytes([
            r.room_id & 0xFF,
            r.n_players & 0xFF,
            r.max_humans & 0xFF,
            r.n_ai & 0xFF,
            1 if r.playing else 0,
        ])
        pkt += name_bytes
    pkt[0] = len(pkt)
    return bytes(pkt)

def build_roominfo(room) -> bytes:
    # maxHumans(1) nAI(1) nPlayers(1) then per-player: slot(1) name(16) ready(1) = 18 bytes
    pkt = bytearray()
    pkt += bytes([0, SXBP_ROOMINFO,
                  room.max_humans & 0xFF,
                  room.n_ai & 0xFF,
                  room.n_players & 0xFF])
    for i, c in enumerate(room.clients):
        name_bytes = c.name.encode("ascii", "replace")[:15].ljust(16, b'\x00')
        pkt += bytes([i & 0xFF])
        pkt += name_bytes
        pkt += bytes([1 if room.ready.get(c, False) else 0])
    pkt[0] = len(pkt)
    return bytes(pkt)

def build_gamestart(room, terrain_seed, wind_seed) -> bytes:
    # terrainSeed(4) windSeed(4) nHumanSlots(1) nAICount(1) aiDiff[3](3)
    # nRounds(1) nStartCash(1) nWindStr(1) nTerrainType(1)
    # mySlot(1) tankType[4](4) colorIdx[4](4) playerNames[4][16](64)
    # total fixed = 4+4+1+1+3+1+1+1+1+1+4+4+64 = 91 bytes + 2 header
    base = bytearray()
    base += bytes([0, SXBP_GAMESTART])
    base += struct.pack(">II", terrain_seed, wind_seed)
    base += bytes([
        room.max_humans & 0xFF,
        room.n_ai & 0xFF,
        room.ai_diff[0] & 0xFF,
        room.ai_diff[1] & 0xFF,
        room.ai_diff[2] & 0xFF,
        room.n_rounds   & 0xFF,
        room.start_cash & 0xFF,
        room.wind_str   & 0xFF,
        room.terrain    & 0xFF,
    ])
    # mySlot placeholder -- overwritten per client below
    base += bytes([0])
    # tankType[4]
    for i in range(4):
        c = room.clients[i] if i < len(room.clients) else None
        base += bytes([c.tank_type if c else 0])
    # colorIdx[4]
    for i in range(4):
        c = room.clients[i] if i < len(room.clients) else None
        base += bytes([c.color_idx if c else 0])
    # playerNames[4][16]
    for i in range(4):
        c = room.clients[i] if i < len(room.clients) else None
        n = c.name.encode("ascii", "replace")[:15] if c else b""
        base += n.ljust(16, b'\x00')
    base[0] = len(base)
    return bytes(base)

def build_turn_relay(slot, weapon_id, angle, power, sequence) -> bytes:
    pkt = bytearray(13)
    pkt[0]  = 13
    pkt[1]  = SXBP_TURN_RELAY
    pkt[2]  = slot & 0xFF
    pkt[3]  = weapon_id & 0xFF
    struct.pack_into(">H", pkt, 4, angle)
    struct.pack_into(">H", pkt, 6, power)
    struct.pack_into(">I", pkt, 8, sequence)
    pkt[12] = 0
    return bytes(pkt)

def build_disconnect(slot) -> bytes:
    return bytes([3, SXBP_DISCONNECT, slot & 0xFF])

# =============================================================================
# PACKET HANDLERS
# =============================================================================

def handle_hello(client, pkt):
    """Client sends name."""
    if len(pkt) >= 3:
        name_raw = pkt[2:].split(b'\x00')[0]
        name     = name_raw.decode("ascii", "replace")[:15].strip() or "Unknown"
        old_name = client.name
        client.name = name
        log(f"HELLO: {old_name} identified as '{name}'")
    # Send current room list
    client.send(build_roomlist())

def handle_join(client, pkt):
    """
    Join or create room.
    pkt[2] = room_id (0 = create)
    pkt[3] = maxHumans (only for create; 0 = join)
    pkt[4] = tankType
    pkt[5] = colorIdx
    If creating: pkt[6..13] = aiCount, aiDiff[3], nRounds, nStartCash, nWindStr, nTerrain
    """
    if len(pkt) < 6:
        log(f"JOIN: malformed packet from {client.name}", "WARN")
        return

    room_id    = pkt[2]
    max_humans = pkt[3]

    if room_id == 0 and max_humans >= 2:
        # Create new room
        if client.room:
            log(f"JOIN/CREATE: {client.name} already in room #{client.room.room_id}", "WARN")
            return

        room = Room(client)
        room.max_humans = min(max(max_humans, 2), MAX_PLAYERS)

        if len(pkt) >= 14:
            room.n_ai        = pkt[4]
            room.ai_diff[0]  = pkt[5]
            room.ai_diff[1]  = pkt[6]
            room.ai_diff[2]  = pkt[7]
            room.n_rounds    = pkt[8]
            room.start_cash  = pkt[9]
            room.wind_str    = pkt[10]
            room.terrain     = pkt[11]
            client.tank_type = pkt[12]
            client.color_idx = pkt[13]

        client.room = room
        add_room(room)
        log(f"JOIN/CREATE: {client.name} created room #{room.room_id} "
            f"({room.max_humans} humans + {room.n_ai} AI)")

        # Send room info back to host
        client.send(build_roominfo(room))

    else:
        # Join existing room
        with _rooms_lock:
            room = _rooms.get(room_id)

        if not room:
            log(f"JOIN: {client.name} tried non-existent room #{room_id}", "WARN")
            client.send(build_roomlist())
            return

        with room.lock:
            if room.playing:
                log(f"JOIN: {client.name} tried to join in-progress room #{room_id}", "WARN")
                client.send(build_roomlist())
                return
            if room.n_players >= room.max_humans:
                log(f"JOIN: {client.name} tried to join full room #{room_id}", "WARN")
                client.send(build_roomlist())
                return

            room.clients.append(client)
            room.ready[client] = False
            client.room = room
            # Read tank/color from join packet
            if len(pkt) >= 6:
                client.tank_type = pkt[4]
                client.color_idx = pkt[5]
            log(f"JOIN: {client.name} joined room #{room_id} "
                f"(slot {room.get_slot(client)}, {room.n_players}/{room.max_humans})")
            log_room(room)

            # Broadcast updated room info to all
            info_pkt = build_roominfo(room)
        for c in room.clients:
            c.send(info_pkt)

def handle_ready(client, pkt):
    """Client signals ready."""
    room = client.room
    if not room:
        return

    is_ready = (len(pkt) >= 3 and pkt[2] != 0)
    with room.lock:
        room.ready[client] = is_ready
        slot = room.get_slot(client)
        log(f"READY: {client.name} (slot {slot}) = {'READY' if is_ready else 'NOT READY'} "
            f"in room #{room.room_id}")

        # Broadcast updated room info
        info_pkt = build_roominfo(room)

        # Check if host is ready and enough players present -- trigger game start
        should_start = (
            room.ready.get(room.host, False) and
            room.enough_to_start() and
            not room.playing
        )

    for c in room.clients:
        c.send(info_pkt)

    if should_start:
        start_game(room)

def handle_voice(client, pkt):
    """Relay voice frame to all other players in the room.
    Voice packets are high-frequency -- log at DEBUG level only."""
    room = client.room
    if not room:
        return
    if len(pkt) < 3:
        return
    # Rebroadcast verbatim -- slot is already embedded in pkt[2] by client
    for c in room.clients:
        if c is not client:
            c.send(pkt)

def handle_turn(client, pkt):
    """Relay a turn to all other players in the room."""
    room = client.room
    if not room or not room.playing:
        log(f"TURN: {client.name} sent turn but not in active game", "WARN")
        return
    if len(pkt) < 12:
        log(f"TURN: malformed turn packet from {client.name}", "WARN")
        return

    slot       = room.get_slot(client)
    weapon_id  = pkt[2]
    angle      = struct.unpack_from(">H", pkt, 3)[0]
    power      = struct.unpack_from(">H", pkt, 5)[0]
    sequence   = struct.unpack_from(">I", pkt, 7)[0]

    log(f"TURN: {client.name} (slot {slot}) | "
        f"weapon={weapon_id} angle={angle} power={power} seq={sequence} "
        f"in room #{room.room_id}")

    relay_pkt = build_turn_relay(slot, weapon_id, angle, power, sequence)
    for c in room.clients:
        if c is not client:
            c.send(relay_pkt)

def start_game(room):
    """Broadcast GAMESTART to all clients with a shared seed."""
    import random
    terrain_seed = random.randint(1, 0xFFFFFFFF)
    wind_seed    = random.randint(1, 0xFFFFFFFF)

    log(f"GAMESTART: room #{room.room_id} | "
        f"{room.n_players} humans + {room.n_ai} AI | "
        f"terrain_seed=0x{terrain_seed:08X} wind_seed=0x{wind_seed:08X}")
    log_room(room)

    with room.lock:
        room.playing = True
        base_pkt = build_gamestart(room, terrain_seed, wind_seed)

    # Send personalised copy to each client (mySlot differs per client)
    MY_SLOT_OFFSET = 19   # byte offset of mySlot in the packet
    for i, c in enumerate(room.clients):
        pkt = bytearray(base_pkt)
        pkt[MY_SLOT_OFFSET] = i & 0xFF
        c.send(bytes(pkt))

    log(f"GAMESTART: sent to all {room.n_players} clients in room #{room.room_id}")

def client_disconnected(client):
    """Handle a client dropping."""
    room = client.room
    log(f"DISCONNECT: {client.name} disconnected")

    if not room:
        return

    with room.lock:
        slot = room.get_slot(client)
        if client in room.clients:
            room.clients.remove(client)
        room.ready.pop(client, None)
        log(f"DISCONNECT: {client.name} (slot {slot}) removed from room #{room.room_id} "
            f"({room.n_players} remaining)")
        log_room(room)

        disc_pkt  = build_disconnect(slot)
        info_pkt  = build_roominfo(room)
        remaining = list(room.clients)
        empty     = (len(room.clients) == 0)

    for c in remaining:
        c.send(disc_pkt)
        c.send(info_pkt)

    if empty:
        remove_room(room.room_id)

# =============================================================================
# CLIENT THREAD
# =============================================================================

def client_thread(client: Client):
    log(f"CONNECT: {client.name} connected from {client.addr[0]}:{client.addr[1]}")
    client.sock.settimeout(RECV_TIMEOUT_S)

    try:
        while client.connected:
            pkt = client.recv_packet()
            if pkt is None:
                break

            ptype = pkt[1]

            # Voice packets are ~50/sec per talker -- skip verbose log
            if ptype == SXBP_VOICE:
                handle_voice(client, pkt)
                continue

            if   ptype == SXBP_HELLO:  handle_hello (client, pkt)
            elif ptype == SXBP_JOIN:   handle_join  (client, pkt)
            elif ptype == SXBP_READY:  handle_ready (client, pkt)
            elif ptype == SXBP_TURN:   handle_turn  (client, pkt)
            elif ptype == SXBP_VOICE:  handle_voice (client, pkt)
            elif ptype == SXBP_PING:
                client.send(bytes([2, SXBP_PONG]))
            elif ptype == SXBP_PONG:
                pass   # no-op
            else:
                log(f"UNKNOWN packet type 0x{ptype:02X} from {client.name}", "WARN")

    except Exception as e:
        log(f"ERROR in client thread for {client.name}: {e}", "ERROR")
    finally:
        client.connected = False
        try:
            client.sock.close()
        except Exception:
            pass
        client_disconnected(client)

# =============================================================================
# MAIN
# =============================================================================

def main():
    log_open()
    log("=" * 60)
    log(f"ScorchedEarthXB Relay Server starting")
    log(f"  Listen address : {HOST}:{PORT}")
    log(f"  Max rooms      : {MAX_ROOMS}")
    log(f"  Max players    : {MAX_PLAYERS}")
    log(f"  Min to start   : {MIN_PLAYERS}")
    log(f"  Room timeout   : {LOBBY_TIMEOUT_S}s")
    log(f"  Recv timeout   : {RECV_TIMEOUT_S}s")
    log(f"  Log file       : {LOG_FILE if LOG_TO_FILE else 'disabled'}")
    log("=" * 60)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(32)
    log(f"Listening on {HOST}:{PORT}")

    try:
        while True:
            try:
                conn, addr = srv.accept()
                client = Client(conn, addr)
                t = threading.Thread(target=client_thread, args=(client,),
                                     daemon=True, name=f"client-{addr}")
                t.start()
            except Exception as e:
                log(f"Accept error: {e}", "ERROR")
    except KeyboardInterrupt:
        log("Server shutting down (KeyboardInterrupt)")
    finally:
        srv.close()
        log_close()

if __name__ == "__main__":
    main()