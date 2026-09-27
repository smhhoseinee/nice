# TCP Nice for Linux

A Linux kernel module that implements TCP Nice, a congestion control algorithm for background transfers. Nice uses
spare bandwidth when the network is idle and backs off quickly when other traffic appears. The algorithm is from
Venkataramani, Kokku, and Dahlin, "TCP Nice: A Mechanism for Background Transfers", OSDI 2002
([paper](https://www.usenix.org/conference/osdi-02/tcp-nice-mechanism-background-transfers)).

Author: Seyed Mohammad Hossein (Saam) Hosseini, University of Colorado Boulder. Written for CSCI 7000 Advanced
Network Protocols, Spring 2025. The full write-up with plots is in [nice_report.pdf](nice_report.pdf).

## How it works

The module starts from Linux's TCP Vegas (`net/ipv4/tcp_vegas.c`) and adds Nice's congestion detector:

- Each ACK gives an RTT sample. The module tracks the smallest RTT seen (`baseRTT`, the propagation delay) and the
  largest RTT in the current window (`maxRTT`, the delay with a full queue).
- A sample counts as delayed when it exceeds `baseRTT + 10% of (maxRTT - baseRTT)` (`THRESHOLD` in `tcp_nice.c`; the
  report's runs used 5%).
- If more than 20% of a window's packets are delayed (`FRACTION`), the congestion window is halved.
- Otherwise it follows Vegas.

One difference from the paper: Linux requires a congestion window of at least 2 packets, so Nice's fractional windows
are not implemented.

## Build and run

Use a test VM, not a production machine. Loading a kernel module needs root, and a bug can crash the kernel.

```bash
make                          # builds tcp_nice.ko and the test tools
sudo insmod tcp_nice.ko       # or: make load
cat /proc/sys/net/ipv4/tcp_available_congestion_control   # should list "nice"
```

Pick Nice for one connection with `setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, "nice", 4)`, or with iperf3:

```bash
iperf3 -s                     # on the receiver
iperf3 -c <receiver> -C nice  # on the sender
```

`server.c` listens on port 8080. `test.c` connects to it with Nice and prints the congestion window and slow-start
threshold. Set `SERVER_IP` in `test.c` first. `reload.sh` rebuilds and reloads the module.

## Results

From [nice_report.pdf](nice_report.pdf), compared against Cubic, Reno, and Vegas:

- With no competing traffic, Nice grows its window the same way Vegas does.
- When a Cubic or Reno flow starts, Nice yields bandwidth, then recovers when the competitor stops.
- Against Vegas on an emulated link (100 Mb/s with `tc`, plus 5% packet loss), Nice takes less throughput than Vegas.

## License

GPL-2.0, as a derivative of the Linux kernel's TCP Vegas module. See [LICENSE](LICENSE).
