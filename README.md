# tap-mirror

A small Linux learning project in C: create a virtual TAP interface and mirror
every frame received on a real network interface into it.

```
wlp0s20f3 (real NIC) --AF_PACKET raw socket--> interface.c --write()--> tap0
```

It is a **one-way frame mirror** (like a SPAN port on a switch). It is not a
bridge (no MAC learning, one direction only) and not a VPN (no routing, no
encryption).

## Files

| File | What it does |
|---|---|
| `interface.c` | Creates `tap0` through `/dev/net/tun`, gives it an IP, then runs a `poll()` loop: frames read from the physical NIC are written into the TAP fd, and anything the kernel transmits out of `tap0` is read and discarded. |
| `socket.c` | Test reader. Binds a raw socket to `tap0` and prints each frame's ethertype, plus IPv4 source/destination when present. |
| `Makefile` | Builds both programs. |

## Build

```
make
```

Needs `gcc` and the Linux kernel headers. Root (or `CAP_NET_ADMIN`) is needed
to run.

## Run

Terminal 1, the mirror (it owns the TAP fd, so it must stay running):

```
sudo ./interface                # mirrors wlp0s20f3
sudo ./interface wlp0s20f3      # or name another interface
```

Terminal 2, bring the interface up:

```
sudo ip link set tap0 up
```

Terminal 3, watch the mirrored frames, with either tool:

```
sudo tcpdump -e -n -i tap0
sudo ./socket                   # or: sudo ./socket tap0
```

Generate some traffic (`ping -c 3 <router ip>`, open a web page). You should
see ARP, IPv4 and IPv6 frames from other devices on your network. The RX
counters should climb too: `ip -s link show tap0`.

Press Ctrl+C in terminal 1 to stop; it prints how many frames were forwarded
and `tap0` disappears when the fd closes.

## How it works

1. `open("/dev/net/tun")` plus `ioctl(TUNSETIFF, IFF_TAP | IFF_NO_PI)` creates
   `tap0` and returns a file descriptor that acts as its "cable".
2. A raw `AF_PACKET` socket is **bound** to the physical NIC. Binding matters:
   without it the socket would also hear `tap0` and re-forward its own frames.
3. `poll()` waits on both descriptors. Frames from the NIC are written into the
   TAP fd, so the kernel sees them as arriving on `tap0`. Outgoing frames
   (`PACKET_OUTGOING`) are skipped, so only incoming traffic is mirrored.
4. Frames the kernel sends out of `tap0` (IPv6 router solicitations, ARP) are
   read from the TAP fd and thrown away, otherwise its queue would fill up.
5. `SIGINT`, `SIGTERM` and `SIGHUP` only set a flag; cleanup happens in `main`.

## Notes

- `interface.c` assigns `7.7.7.7` to `tap0` with `SIOCSIFADDR`. That ioctl
  picks the classful netmask, so `tap0` gets a `7.0.0.0/8` route once it is up,
  and traffic to real `7.x.x.x` hosts would be sent into the TAP and dropped.
  Change the address in `main` if that matters to you.
- Only frames the NIC actually receives are mirrored. Without promiscuous mode
  that means unicast to your machine plus broadcast and multicast.
- Wi-Fi cards deliver frames larger than 1514 bytes when offloads are on, which
  is why the buffer is 64 KiB.

## Ideas for next steps

- Count frames per ethertype and print per-second statistics.
- Add the TAP to NIC direction (careful with loops) and a MAC table to make it a
  real bridge.
- Switch to `IFF_TUN` plus a UDP socket for a toy VPN.

## License

MIT, see `LICENSE`.
