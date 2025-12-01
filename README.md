🎮 AR/VR Network Simulation in ns-3

This repository contains an ns-3–based end-to-end AR/VR network simulation model.

It simulates:

📡 Downlink VR streaming

Large frame size (e.g., 90 KB)

30 FPS → 1 frame every 33 ms

Fragments: 1200-byte packets with custom VrHeader

🎯 Uplink IMU/control traffic

100 Hz (every 10 ms)

Small packets (100 B)

Delay statistics: avg / p99 / max

🚚 Transport protocols supported

UDP

TCP (Cubic / BBR)

QUIC-lite pacing

Smooth 200 µs inter-packet pacing

Simulates QUIC congestion control behavior

Reduces queue build-up and burst losses

⏱ Frame-level analysis

End-to-end VR frame aggregation by frameId

Metrics computed:

total

onTime

late

incomplete

ratio = onTime / total

Configurable deadline (default: 50 ms)

📊 Flow-level statistics

Produced via ns-3 FlowMonitor:

Throughput

Packet delay / jitter

Loss & drops

TCP retransmissions

Queue dynamics

FlowMonitor XML files are stored in the xml/ directory.

📁 Repository Structure
.
├── arvr-sim.cc              # Main ns-3 simulation source code
│
├── run_quic.sh              # QUIC-lite pacing run (WITH congestion control)
├── final-sweep.sh           # Baseline UDP/TCP run (NO pacing → no CC)
│
├── results_quic.xlsx        # Results AFTER enabling pacing (CC enabled)
├── results_final.xlsx       # Results BEFORE pacing (no CC)
│
└── xml/                     # FlowMonitor XML outputs

✔ Meaning of the two .sh files
Script	Description
run_quic.sh	QUIC-lite pacing — after enabling congestion control
final-sweep.sh	Baseline UDP/TCP — before congestion control
✔ Meaning of the two result .xlsx files
Result file	Meaning
results_quic.xlsx	Performance with pacing (CC ON)
results_final.xlsx	Performance without pacing (CC OFF)
🚀 How to Run

Run from your ns-3 root:

UDP baseline
./ns3 run "scratch/arvr-sim --transport=udp --rate=120Mbps --delay=10ms"

TCP BBR
./ns3 run "scratch/arvr-sim --transport=tcp --tcp=bbr --rate=120Mbps --delay=30ms"

QUIC-lite pacing
./ns3 run "scratch/arvr-sim --transport=quic --rate=120Mbps --delay=50ms"

⚙️ Command-Line Options
Flag	Description	Example
--transport	udp / tcp / quic	--transport=quic
--tcp	cubic / bbr	--tcp=bbr
--rate	link bandwidth	--rate=120Mbps
--delay	one-way propagation delay	--delay=30ms
--loss	packet loss rate	--loss=0.001
--deadline	frame deadline	--deadline=80
--frameSize	downlink frame size	--frameSize=90000
📌 Example Output
[UL-IMU] avgDelay=10 p99=10 max=10
[VR-RECV] total=576 onTime=572 late=1 incomplete=3 ratio=0.993056


Interpretation:

total — frames that started arriving

onTime — complete + within deadline

late — complete but beyond deadline

incomplete — fragments missing

ratio — onTime / total
