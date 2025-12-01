AR/VR Network Simulation in ns-3

This repository contains an ns-3–based end-to-end AR/VR traffic model.
It simulates:

Downlink VR streaming (large frame, 30 FPS → 33 ms per frame)

Uplink IMU/control traffic (100 Hz small packets)

Transport comparison:

UDP

TCP (Cubic / BBR)

QUIC-lite pacing (smooth sending, simulating QUIC congestion control)

Frame-level deadline analysis

Flow-level statistics via FlowMonitor

Congestion-control ON/OFF comparison (your two .sh scripts and .xlsx results)

The main simulation code is in:
arvr-sim.cc (full source referenced below)


Repository Structure
.
├── arvr-sim.cc              # Main ns-3 simulation source file
│
├── run_quic.sh              # With pacing enabled (QUIC-lite) → WITH congestion control
├── final-sweep.sh           # Baseline UDP/TCP sweep → WITHOUT congestion control
│
├── results_quic.xlsx        # Experiment results WITH pacing (after congestion control)
├── results_final.xlsx       # Baseline results WITHOUT pacing (before congestion control)
│
└── xml/                     # FlowMonitor XML files for network-layer statistics

Interpretation of the two .sh files
File	Meaning
run_quic.sh	QUIC-lite mode (200µs pacing) → “after congestion control”
final-sweep.sh	Plain TCP/UDP (no pacing) → “before congestion control”
Interpretation of the two result .xlsx files
File	Meaning
results_quic.xlsx	Results after enabling pacing (CC applied)
results_final.xlsx	Baseline results without pacing (no CC)
Key Features
Downlink VR Stream

Sends a frame every 33 ms

Frame size: 90 KB (configurable)

Split into 1200-byte fragments

Each fragment contains a custom VrHeader:

frameId

pktId

pktCount

sendTsMs (timestamp)

Supports:

UDP (no congestion control)

TCP Cubic / TCP BBR

QUIC-lite pacing (smooth 200µs inter-packet gap)

Uplink IMU Stream

100 Hz (every 10 ms)

Small packets (100 bytes)

Reports:

avg delay

p99 delay

max delay

Receiver

Reassembles VR frames by frameId

Computes:

total frames

on-time frames

late frames

incomplete frames

on-time ratio

Unified logic for UDP/TCP stream reassembly

How to Run

Run from your ns-3 root directory:

./ns3 run "scratch/arvr-sim --transport=udp --rate=120Mbps --delay=10ms"

Command-Line Arguments
Flag	Description	Example
--transport	udp / tcp / quic	--transport=quic
--tcp	cubic / bbr (TCP only)	--tcp=bbr
--rate	bottleneck bandwidth	--rate=120Mbps
--delay	one-way channel delay	--delay=30ms
--loss	packet loss rate	--loss=0.001
--deadline	per-frame deadline (ms)	--deadline=80
--frameSize	downlink VR frame size	--frameSize=90000
Example Runs
UDP baseline
./ns3 run "scratch/arvr-sim --transport=udp --rate=120Mbps --delay=10ms"

TCP-BBR
./ns3 run "scratch/arvr-sim --transport=tcp --tcp=bbr --rate=120Mbps --delay=30ms"

QUIC-lite pacing (congestion control ON)
./ns3 run "scratch/arvr-sim --transport=quic --rate=120Mbps --delay=50ms"

Output Example
[UL-IMU] avgDelay=10 p99=10 max=10
[VR-RECV] total=576 onTime=572 late=1 incomplete=3 ratio=0.993056


Meaning:

total: Frames with at least one fragment received

onTime: Reconstructed & within deadline

late: Reconstructed but after deadline

incomplete: Missing fragments (usually last frames near StopTime)

ratio: onTime / total

FlowMonitor XML

The program automatically produces:

arvr_tx-udp_tcp-cubic_rate-120Mbps_delay-10ms_loss-0_deadline-50_fs-90000.xml


These XML files include:

Per-flow throughput

Per-packet delay / jitter

Loss & drops

TCP retransmissions

Queue build-up

RTT statistics

Stored inside the xml/ folder.

Before vs After Congestion Control
Mode	Script	Effect	Result File
Baseline (no pacing)	final-sweep.sh	Traditional UDP/TCP bursty traffic	results_final.xlsx
Pacing / QUIC-lite	run_quic.sh	Smooth inter-packet sending → lower queue build-up → fewer deadline misses	results_quic.xlsx
