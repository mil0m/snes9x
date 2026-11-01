# Remote control from a browser: plan (roadmap #53)

**Status (2026-11-01):** planned, being built. Phase 12 on the roadmap; #54 (chat plays)
builds on it.

**Goal.** Erik (or Mikael, from a phone) controls a corruption session running on
Mikael's computer from a web browser anywhere: corruption on and off, Chaos, re-roll,
rules on and off and their strength, hotkeys, *It crashed: go back*, *Back to normal*, and
a live picture of a memory to watch and paint in. The game itself is seen and heard on a
stream (Discord, Twitch), so the page sends **no video or sound**.

**Decided with Mikael:** only Erik and Mikael connect; no game picture; the page is
reached through a Cloudflare Tunnel on Mikael's own domain, behind Cloudflare Access.

## Shape

```
browser ──https──▶ <your-host> (Cloudflare Access: one-time PIN, allowed emails only)
                          │  cloudflared on the computer connects out; no open port
                   cloudflared ──▶ http://127.0.0.1:<port>  ← RemoteServer inside snes9x-qt
```

- **In the app** (this repo): `RemoteServer` (Qt HttpServer + WebSockets, both in Qt 6)
  listens on **127.0.0.1 only**, serves one page bundled in the app's resources, and
  speaks a small JSON protocol over a WebSocket. It knows nothing about tunnels, domains or
  Cloudflare; it works the same from `http://127.0.0.1:<port>` on the same computer.
- **On the computer only** (never in this repo): the tunnel's config and credentials
  (`~/.cloudflared/`), the hostname, the Access policy (which emails may in). How to set
  it up is documented below with placeholders; the real names live in the owner's private
  notes.
- **In the app's settings** (the user's `snes9x.conf`, outside the repo): whether remote
  control is on, the port (default 8095), the public address to put in links, and the
  access token.

## Keeping private things out of the code

- The code holds no hostname, email, token or tunnel id. Defaults are `127.0.0.1` and a
  port; the public address is typed into the panel once and saved in the user's settings.
- The **token** (32 random bytes, base64url) is made on first use with the system's
  random source, saved in the user's settings, and can be renewed from the panel (which
  drops every connection).
- The **link** the panel copies is `<public address>/#<token>`: the token is in the URL's
  fragment, which browsers never send to a server, so it doesn't reach Cloudflare's or
  anyone's logs, nor the Referer header. The page reads it and sends it over the
  WebSocket as its first message.
- **A check before every commit:** `tools/privacy_check.sh` fails if a tracked file holds a
  word from an untracked, git-ignored list (`.private-words`, one per line: the domain, the
  email, the tunnel id), or anything shaped like a tunnel credential (`"TunnelSecret"`,
  `cert.pem`, `-----BEGIN`). `tools/install-hooks.sh` makes it the repo's pre-commit hook;
  `build-mac.sh test` runs it too. The list itself is never committed.

## Safety (what the remote can and can't do)

The protocol is a **fixed list of actions**, each checked in `corrupt/remote_api` (Qt-free,
unit-tested) before it reaches the session:

| Action | Arguments, checked |
|---|---|
| `active` | on/off |
| `back_to_normal`, `reroll`, `mark_crash` | none |
| `chaos` | 0-1 |
| `rule_enabled`, `rule_strength` | an index that exists; on/off or 0-1 |
| `hotkey` | 1-9 |
| `watch` | a memory (WRAM, VRAM, SRAM, ARAM, OAM, CGRAM, ROM), a window (offset, length up to 128 KB) |
| `paint` | a memory, up to 4096 offsets inside it, a value 0-255; at most 20 a second |

Nothing else: no file paths, no loading or saving recipes or ROMs, no library, no backups,
no export, no settings. Painting only changes the emulated SNES, which the crash guard
already protects; the worst a remote can do is crash the game it's corrupting.

Other layers:

- **127.0.0.1 only**, so nothing on the network reaches it except through the tunnel.
- **Cloudflare Access** in front (outside the app): one-time PIN to Erik's and Mikael's
  emails.
- **The token**, compared in constant time; a wrong one closes the connection, and five
  wrong ones in a minute make the server refuse for a minute.
- **The WebSocket's Origin must match its Host** (so a web page in a browser on the same
  computer can't drive the app through `127.0.0.1`).
- **Off by default**: *Allow remote control* in the panel, which also shows who is
  connected (the `Cf-Access-Authenticated-User-Email` header Access adds, or *this
  computer*) and a button to renew the token.
- The page is served with `Content-Security-Policy: default-src 'self'`,
  `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`; only the page's own paths are
  served (a fixed map, no file system).
- Every remote action is a status line in the panel: *Remote (erik@…): pressed hotkey 1*.

## The page

One HTML file with its script and style, in the app's resources, light and dark, phone
width first:

- **Top:** the game, corruption on/off (big), Chaos slider, Re-roll, It crashed, Back to
  normal, the status line and crash count, and who else is connected.
- **Rules:** each rule's name, what it does (the same words as the panel), on/off and
  strength.
- **Hotkeys:** the recipe's hotkey rules as buttons 1-9.
- **Memory:** a memory picker, the picture (bytes coloured like the viewer: the app sends
  its 256-colour table once, so the colours can't drift), zoom, row width, and a paint
  mode with a value; a tap or drag paints.

Updates: status pushed 4 times a second and on every change; the memory window as a binary
message up to 5 times a second, only while someone watches it.

## Protocol

WebSocket at `/ws`. Text messages are JSON; the first must be `{"hello": 1, "token": …}`,
answered by `{"welcome": 1, "colours": [...], "status": {...}}` or a close. Then:

- client → `{"do": "<action>", ...arguments, "id": n}`; server → `{"done": n}` or
  `{"error": "...", "id": n}`;
- server → `{"status": {...}}` whenever it changes (and at most 4 a second);
- server → binary: 4 bytes `S9XM`, memory id (1), offset (4, little-endian), then the bytes.

## How to put it on your domain (owner's notes, with placeholders)

1. Panel: tick *Allow remote control*. It listens on `127.0.0.1:<port>`.
2. A tunnel route `<your-host>` → `http://localhost:<port>` on this computer's
   `cloudflared` (a second tunnel if the main one runs elsewhere).
3. Cloudflare Zero Trust → Access → a self-hosted application for `<your-host>`, policy:
   the allowed emails, one-time PIN. **Never route it without this.**
4. Panel: type `https://<your-host>` as the public address, *Copy link*, send it to Erik.

## Tests

- `corrupt/remote_api` with a fake control: every action reaches the control with its
  arguments; out-of-range and unknown actions are refused with a reason and change
  nothing; paint caps (count, offsets inside the memory, rate); the hello/token handshake
  and the lock-out; the status JSON; the binary memory message.
- Qt, offscreen: `RemoteServer` on port 0 binds to 127.0.0.1; serves the page with the
  headers and 404s anything else (`/../`, `/ws` over plain HTTP); a WebSocket with the
  right token gets the welcome and the status, a wrong one is closed, a bad Origin is
  refused; an action changes the session; a watched memory arrives as binary.
- The page: a headless browser test of the JS rendering against a recorded welcome
  message (status, rules, a memory frame), light and dark, 390 px wide.
- `tools/privacy_check.sh` on a planted word in a temporary repo.
- Then by hand (QA.md section 31): through the tunnel from a phone.

## Later

- **#54 chat plays** on top: the app connects *out* to Twitch chat (no inbound), and
  commands map to the same action list (`!swap` → a hotkey, `!chaos 80`), with votes and a
  cooldown. Its OAuth token lives in the user's settings like the access token.
- A game picture (a 256×224 JPEG over the same socket) if streaming ever gets in the way.
