# AR/VR Network Simulation in ns-3

This repository contains an ns-3–based end-to-end AR/VR network simulation model.  
It includes VR downlink streaming, IMU uplink traffic, transport protocol comparison, frame-level deadline analysis, and FlowMonitor-based network statistics.

---

## Features

### Downlink (VR Streaming)
- Large VR frames (default: 90 KB)
- 30 FPS (one frame every 33 ms)
- Frames are fragmented into 1200-byte packets
- Each fragment carries a custom `VrHeader`:
  - frameId  
  - pktId  
  - pktCount  
  - sendTsMs (timestamp)

### Uplink (IMU/Control Traffic)
- 100 Hz (one packet every 10 ms)
- Small payload (100 bytes)
- Measures uplink delay: average, p99, maximum

### Transport Protocols
- UDP  
- TCP (Cubic / BBR)  
- QUIC-lite pacing (200 µs smooth send interval)

### Receiver-Side Aggregation
- Reassembles fragments into full VR frames
- Computes:
  - total frames
  - on-time frames
  - late frames
  - incomplete frames
  - on-time ratio
- Deadline is configurable (default: 50 ms)

### FlowMonitor Integration
FlowMonitor XML files include:
- Throughput  
- Packet delay and jitter  
- Loss and drops  
- Queue dynamics  
- TCP retransmissions  

Files are stored in the `xml/` directory.

---

## Repository Structure

```
.
├── arvr-sim.cc              # Main ns-3 simulation code
│
├── run_quic.sh              # QUIC-lite pacing experiment (congestion control ON)
├── final-sweep.sh           # Baseline UDP/TCP sweep (congestion control OFF)
│
├── results_quic.xlsx        # Results with pacing enabled
├── results_final.xlsx       # Results without pacing
│
└── xml/                     # FlowMonitor XML outputs
```

### Relationship Between Scripts and Results

| File | Description |
|------|-------------|
| run_quic.sh | Experiments using QUIC-lite pacing (after congestion control) |
| final-sweep.sh | Baseline UDP/TCP experiments (before congestion control) |
| results_quic.xlsx | Results with pacing (congestion control ON) |
| results_final.xlsx | Baseline results (congestion control OFF) |

---

## How to Run

Run from the ns-3 root directory:

### UDP
```
./ns3 run "scratch/arvr-sim --transport=udp --rate=120Mbps --delay=10ms"
```

### TCP BBR
```
./ns3 run "scratch/arvr-sim --transport=tcp --tcp=bbr --rate=120Mbps --delay=30ms"
```

### QUIC-lite Pacing
```
./ns3 run "scratch/arvr-sim --transport=quic --rate=120Mbps --delay=50ms"
```

---

## Command-Line Options

| Flag | Description | Example |
|------|-------------|---------|
| `--transport` | udp / tcp / quic | `--transport=quic` |
| `--tcp` | cubic / bbr (only for TCP mode) | `--tcp=bbr` |
| `--rate` | Link bandwidth | `--rate=120Mbps` |
| `--delay` | One-way propagation delay | `--delay=30ms` |
| `--loss` | Packet loss rate | `--loss=0.001` |
| `--deadline` | VR frame deadline | `--deadline=80` |
| `--frameSize` | Downlink VR frame size | `--frameSize=90000` |

---

## Example Output

```
[UL-IMU] avgDelay=10 p99=10 max=10
[VR-RECV] total=576 onTime=572 late=1 incomplete=3 ratio=0.993056
```

Meaning:
- total – frames for which at least one fragment arrived
- onTime – completed within deadline
- late – completed but exceeded deadline
- incomplete – missing fragments
- ratio – onTime / total

---

## Source Code

The full simulation logic is implemented in:

`arvr-sim.cc`

