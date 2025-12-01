//
// AR/VR traffic model in ns-3:
// - Downlink: periodic, frame-based, large packets (e.g., 30 FPS, 33ms/frame)
// - Uplink: high-frequency, small packets (e.g., 100 Hz IMU/control)
// - Receiver: groups packets by frameId and computes on-time frame ratio
// - Link params (rate/delay/loss) are configurable from command line
//
// Usage examples:
//   ./ns3 run "scratch/arvr-sim --rate=100Mbps --delay=10ms"
//   ./ns3 run "scratch/arvr-sim --rate=50Mbps  --delay=50ms --loss=0.01 --deadline=20"
//

#include <string>
#include <cstdint>
#include <map>          // for std::map
#include <vector>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

//
// 1. VR packet header (frameId, pktId, totalPkts, sendTsMs)
//   - attached to every downlink packet
//   - receiver can reconstruct frames and check deadline
//
class VrHeader : public Header
{
public:
  VrHeader ()
    : m_frameId (0),
      m_pktId (0),
      m_pktCount (0),
      m_sendTsMs (0)
  {}

  VrHeader (uint32_t frameId, uint16_t pktId, uint16_t pktCount, uint32_t sendTsMs)
    : m_frameId (frameId),
      m_pktId (pktId),
      m_pktCount (pktCount),
      m_sendTsMs (sendTsMs)
  {}

  uint32_t DeserializeFromRaw(const uint8_t* data)
  {
      m_frameId =
          ((uint32_t)data[0] << 24) |
          ((uint32_t)data[1] << 16) |
          ((uint32_t)data[2] <<  8) |
          ((uint32_t)data[3]);

      m_pktId =
          ((uint16_t)data[4] << 8) |
          ((uint16_t)data[5]);

      m_pktCount =
          ((uint16_t)data[6] << 8) |
          ((uint16_t)data[7]);

      m_sendTsMs =
          ((uint32_t)data[8]  << 24) |
          ((uint32_t)data[9]  << 16) |
          ((uint32_t)data[10] <<  8) |
          ((uint32_t)data[11]);

      return 12;
  }


  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::VrHeader")
      .SetParent<Header> ()
      .SetGroupName ("Applications")
      .AddConstructor<VrHeader> ();
    return tid;
  }

  virtual TypeId GetInstanceTypeId () const override
  {
    return GetTypeId ();
  }

  // network-order serialization
  virtual void Serialize (Buffer::Iterator start) const override
  {
    start.WriteHtonU32 (m_frameId);
    start.WriteHtonU16 (m_pktId);
    start.WriteHtonU16 (m_pktCount);
    start.WriteHtonU32 (m_sendTsMs);
  }

  virtual uint32_t Deserialize (Buffer::Iterator start) override
  {
    m_frameId   = start.ReadNtohU32 ();
    m_pktId     = start.ReadNtohU16 ();
    m_pktCount  = start.ReadNtohU16 ();
    m_sendTsMs  = start.ReadNtohU32 ();
    return GetSerializedSize ();
  }

  virtual uint32_t GetSerializedSize () const override
  {
    return 4 + 2 + 2 + 4;  // 12 bytes
  }

  virtual void Print (std::ostream &os) const override
  {
    os << "frameId=" << m_frameId
       << " pktId=" << m_pktId << "/" << m_pktCount
       << " sendTsMs=" << m_sendTsMs;
  }

  // accessors
  uint32_t GetFrameId ()  const { return m_frameId; }
  uint16_t GetPktId ()    const { return m_pktId; }
  uint16_t GetPktCount () const { return m_pktCount; }
  uint32_t GetSendTsMs () const { return m_sendTsMs; }

  void SetFrameId   (uint32_t v)  { m_frameId = v; }
  void SetPktId     (uint16_t v)  { m_pktId = v; }
  void SetPktCount  (uint16_t v)  { m_pktCount = v; }
  void SetSendTsMs  (uint32_t v)  { m_sendTsMs = v; }

private:
  uint32_t m_frameId;
  uint16_t m_pktId;
  uint16_t m_pktCount;
  uint32_t m_sendTsMs;
};

//
// 2. Downlink app: send one VR frame every frameInterval
//    A frame is split into multiple packets, each with VrHeader
//
class VrDownlinkApp : public Application
{
public:
  VrDownlinkApp ()
    : m_frameSize(0),
      m_frameInterval(MilliSeconds(33)),
      m_pktSize(1200),
      m_frameCounter(0),
      m_usePacing(false),
      m_pacingInterval(MicroSeconds(200))   // 默认 200us 一包
  {}

  // 新的 Setup：多了 usePacing 和 pacingInterval 两个参数（有默认值）
  void Setup (Ptr<Socket> socket, Address peer,
              uint32_t frameSizeBytes, Time frameInterval,
              uint32_t pktSize,
              bool usePacing = false,
              Time pacingInterval = MicroSeconds (200))
  {
    m_socket        = socket;
    m_peer          = peer;
    m_frameSize     = frameSizeBytes;
    m_frameInterval = frameInterval;
    m_pktSize       = pktSize;
    m_usePacing     = usePacing;
    m_pacingInterval= pacingInterval;
  }

private:
  virtual void StartApplication () override
  {
    m_socket->Connect (m_peer);
    SendFrame ();
  }

  // 发送整个一帧（按是否启用 pacing 走不同路径）
  void SendFrame ()
  {
    // #pkts = ceil(frameSize / pktSize)
    uint32_t pkts    = (m_frameSize + m_pktSize - 1) / m_pktSize;
    uint32_t frameId = m_frameCounter++;

    if (!m_usePacing)
    {
      // 原来的“一口气发完所有 fragment”的版本
      for (uint32_t i = 0; i < pkts; ++i)
      {
        Ptr<Packet> p = Create<Packet> (m_pktSize);

        VrHeader hdr (frameId,
                      (uint16_t)i,
                      (uint16_t)pkts,
                      (uint32_t) Simulator::Now ().GetMilliSeconds ());
        p->AddHeader (hdr);

        m_socket->Send (p);
      }

      // 原来的：直接 schedule 下一帧
      Simulator::Schedule (m_frameInterval, &VrDownlinkApp::SendFrame, this);
    }
    else
    {
      // QUIC-lite：启动“按间隔发 fragment”的流程
      SendOneFragment (frameId, pkts, 0);
    }
  }

  // QUIC-lite：一帧里的第 idx 个 fragment
  void SendOneFragment (uint32_t frameId, uint32_t pkts, uint32_t idx)
  {
    Ptr<Packet> p = Create<Packet> (m_pktSize);

    VrHeader hdr (frameId,
                  (uint16_t)idx,
                  (uint16_t)pkts,
                  (uint32_t) Simulator::Now ().GetMilliSeconds ());
    p->AddHeader (hdr);

    m_socket->Send (p);

    if (idx + 1 < pkts)
    {
      // 还没发完这一帧 → 过一个 pacingInterval 再发下一片
      Simulator::Schedule (m_pacingInterval,
                           &VrDownlinkApp::SendOneFragment,
                           this, frameId, pkts, idx + 1);
    }
    else
    {
      Time remaining = m_frameInterval - (pkts * m_pacingInterval);
      if (remaining.IsPositive())
      {
          Simulator::Schedule (remaining, &VrDownlinkApp::SendFrame, this);
      }
      else
      {
          Simulator::Schedule (MicroSeconds(1), &VrDownlinkApp::SendFrame, this);
      }
    }
  }

  Ptr<Socket> m_socket;
  Address     m_peer;
  uint32_t    m_frameSize;
  Time        m_frameInterval;
  uint32_t    m_pktSize;
  uint32_t    m_frameCounter;

  bool        m_usePacing;       // true = QUIC-lite 模式
  Time        m_pacingInterval;  // 每个 fragment 之间的发送间隔
};


//
// 3. Receiver app: collect packets by frameId and check deadline
//
// ===================== Receiver App Supporting UDP + TCP =====================

// 修复后的 VrReceiverApp - 正确处理TCP流
class VrReceiverApp : public Application
{
public:
  VrReceiverApp ()
    : m_deadlineMs (33),
      m_totalFrames (0),
      m_onTimeFrames (0),
      m_lateFrames (0),
      m_incompleteFrames (0),
      m_useTcp(false),
      m_tcpBufferSize(200000),
      m_packetSize(12 + 1200)   // header(12B) + payload(1200B)
  {
    m_tcpBuffer.reserve(m_tcpBufferSize);
  }

  void SetDeadlineMs (uint32_t d) { m_deadlineMs = d; }
  void SetUseTcp(bool useTcp) { m_useTcp = useTcp; }
  void SetPacketSize(uint32_t p) { m_packetSize = p; }

  uint32_t GetTotalFrames () const { return m_totalFrames; }
  uint32_t GetOnTimeFrames () const { return m_onTimeFrames; }
  uint32_t GetLateFrames () const { return m_lateFrames; }
  uint32_t GetIncompleteFrames () const { return m_incompleteFrames; }

  // 下行 per-frame delay 统计
  std::vector<uint32_t> m_delays;

  double GetAvgDelay() const {
    if (m_delays.empty()) return 0.0;
    uint64_t sum = 0;
    for (uint32_t d : m_delays) sum += d;
    return double(sum) / m_delays.size();
  }

  uint32_t GetP99Delay() const {
    if (m_delays.empty()) return 0;
    std::vector<uint32_t> sorted = m_delays;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(sorted.size() * 0.99);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
  }

  uint32_t GetMaxDelay() const {
    if (m_delays.empty()) return 0;
    return *std::max_element(m_delays.begin(), m_delays.end());
  }

private:
  struct FrameState {
    uint16_t pktCount = 0;   // 这一帧一共有多少 fragment
    uint16_t arrived  = 0;   // 到了多少个 fragment
    uint32_t sendTsMs = 0;   // 这一帧的发送时间戳（ms）
    bool     counted  = false; // 是否已经统计过 totalFrames
    bool     done     = false; // 是否已经完成（onTime 或 late）
  };

  // ===== Application 生命周期 =====
  virtual void StartApplication () override
  {
    if (m_useTcp)
    {
      // TCP: 监听 5000 端口，等待下行连接
      m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
      InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 5000);
      m_socket->Bind(local);
      m_socket->Listen();
      m_socket->SetAcceptCallback(
        MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
        MakeCallback(&VrReceiverApp::HandleTcpAccept, this)
      );
      // 这里不再把 m_tcpBufferSize 设成 0！
      m_tcpBuffer.clear();
    }
    else
    {
      // UDP: 直接 Bind+RecvCallback
      m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
      m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 5000));
      m_socket->SetRecvCallback (MakeCallback (&VrReceiverApp::HandleRead, this));
    }
  }

  virtual void StopApplication () override
  {
    if (m_socket)
    {
      m_socket->Close ();
      m_socket = nullptr;
    }

    // 对所有已经“计入 totalFrames 但没完成”的帧，视作 incomplete
    for (auto &kv : m_frames)
    {
      auto &st = kv.second;
      if (st.counted && !st.done)
      {
        m_incompleteFrames += 1;
      }
    }
  }

  // ===== TCP: accept 新连接 =====
  void HandleTcpAccept(Ptr<Socket> s, const Address&)
  {
    s->SetRecvCallback(MakeCallback(&VrReceiverApp::HandleTcpRead, this));
  }

  // ===== TCP: 处理流式数据 =====
  void HandleTcpRead (Ptr<Socket> socket)
  {
    Address from;
    Ptr<Packet> pkt = socket->RecvFrom(from);

    // 1) 先把收到的所有 segment 全部 append 到 m_tcpBuffer
    while (pkt && pkt->GetSize() > 0)
    {
      uint32_t sz = pkt->GetSize();

      if (m_tcpBuffer.size() + sz > m_tcpBufferSize)
      {
        NS_LOG_UNCOND("TCP buffer overflow, clear buffer");
        m_tcpBuffer.clear();
        return;
      }

      size_t oldSize = m_tcpBuffer.size();
      m_tcpBuffer.resize(oldSize + sz);
      pkt->CopyData(&m_tcpBuffer[oldSize], sz);

      pkt = socket->RecvFrom(from);
    }

    // 2) 只要 buffer 里还有 >= 1 个完整的“header+payload”块，就反复解析
    while (m_tcpBuffer.size() >= m_packetSize)
    {
      // 前 12 字节是 VrHeader（network-order）
      VrHeader hdr;
      hdr.DeserializeFromRaw(m_tcpBuffer.data());

      // 调统一的处理逻辑
      ProcessPacket(hdr);

      // 丢掉整个一块：header(12B) + payload(1200B)
      m_tcpBuffer.erase(
        m_tcpBuffer.begin(),
        m_tcpBuffer.begin() + m_packetSize
      );
    }
  }

  // ===== UDP: 每个 packet 自带 header，直接拆掉 =====
  void HandleRead (Ptr<Socket> socket)
  {
    Address from;
    Ptr<Packet> p = socket->RecvFrom (from);
    if (!p) return;

    VrHeader hdr;
    p->RemoveHeader(hdr);   // ns-3 会自动识别 12B header（Serialize/Deserialize）

    ProcessPacket(hdr);
  }

  // ===== 统一的 per-fragment 处理逻辑（UDP/TCP 共用） =====
  void ProcessPacket(const VrHeader& hdr)
  {
    uint32_t fid   = hdr.GetFrameId();
    uint32_t nowMs = Simulator::Now().GetMilliSeconds();

    auto &st = m_frames[fid];

    if (!st.counted)
    {
      st.counted   = true;
      st.pktCount  = hdr.GetPktCount();
      st.sendTsMs  = hdr.GetSendTsMs();
      m_totalFrames += 1;   // 只要这一帧有第一个 fragment 到达，就算一帧
    }

    st.arrived += 1;

    // 这一帧第一次达到“所有 fragment 到齐”的时刻 → 判定 delay & onTime/late
    if (!st.done && st.arrived == st.pktCount)
    {
      uint32_t delta = nowMs - st.sendTsMs;
      m_delays.push_back(delta);

      if (delta <= m_deadlineMs)
        m_onTimeFrames += 1;
      else
        m_lateFrames += 1;

      st.done = true;
    }
  }

  // ===== 成员变量 =====
  Ptr<Socket> m_socket;
  bool m_useTcp;

  // TCP 流重组缓冲区
  std::vector<uint8_t> m_tcpBuffer;
  uint32_t m_tcpBufferSize;
  uint32_t m_packetSize;   // header + payload 的总长度（默认 12+1200）

  // 每帧的聚合状态（无论 UDP/TCP）
  std::map<uint32_t, FrameState> m_frames;

  // 指标统计
  uint32_t m_deadlineMs;
  uint32_t m_totalFrames;
  uint32_t m_onTimeFrames;
  uint32_t m_lateFrames;
  uint32_t m_incompleteFrames;
};


class UplinkHeader : public Header
{
public:
  UplinkHeader (uint32_t ts = 0) : m_ts(ts) {}

  void SetTs (uint32_t ts) { m_ts = ts; }
  uint32_t GetTs () const { return m_ts; }

  static TypeId GetTypeId (void) {
    static TypeId tid = TypeId("UplinkHeader")
      .SetParent<Header>()
      .AddConstructor<UplinkHeader>();
    return tid;
  }

  virtual TypeId GetInstanceTypeId (void) const { return GetTypeId(); }
  virtual void Print (std::ostream& os) const { os << m_ts; }

  virtual uint32_t GetSerializedSize (void) const { return 4; }

  virtual void Serialize (Buffer::Iterator start) const {
    start.WriteHtonU32(m_ts);
  }

  virtual uint32_t Deserialize (Buffer::Iterator start) {
    m_ts = start.ReadNtohU32();
    return 4;
  }

private:
  uint32_t m_ts;
};

//
// 4. Uplink app: send small packets periodically (e.g., every 10ms)
//
class VrUplinkApp : public Application
{
public:
  VrUplinkApp () {}

  void Setup (Ptr<Socket> socket, Address peer,
              Time interval, uint32_t pktSize)
  {
    m_socket   = socket;
    m_peer     = peer;
    m_interval = interval;
    m_pktSize  = pktSize;
  }

private:
  virtual void StartApplication () override
  {
    m_socket->Connect (m_peer);
    SendOne ();
  }

  void SendOne ()
  {
    uint32_t nowMs = Simulator::Now ().GetMilliSeconds ();

    Ptr<Packet> p = Create<Packet> (m_pktSize);
    UplinkHeader hdr(nowMs);
    p->AddHeader(hdr);

    m_socket->Send (p);

    Simulator::Schedule (m_interval, &VrUplinkApp::SendOne, this);
  }

  Ptr<Socket> m_socket;
  Address     m_peer;
  Time        m_interval;
  uint32_t    m_pktSize;
};

class VrUplinkReceiver : public Application
{
public:
  std::vector<uint32_t> m_delays;

private:
  virtual void StartApplication() override
  {
    Ptr<Socket> s =
      Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    s->Bind(InetSocketAddress(Ipv4Address::GetAny(), 6000));
    s->SetRecvCallback(MakeCallback(&VrUplinkReceiver::HandleRead, this));
    m_socket = s;
  }

  void HandleRead(Ptr<Socket> socket)
  {
    Address from;
    Ptr<Packet> p = socket->RecvFrom(from);
    if (!p) return;

    UplinkHeader hdr;
    p->RemoveHeader(hdr);

    uint32_t sendTs = hdr.GetTs();
    uint32_t now    = Simulator::Now().GetMilliSeconds();
    m_delays.push_back(now - sendTs);
  }

  Ptr<Socket> m_socket;
};

//
// 5. main: build 2-node topology and run AR/VR traffic
//
int
main (int argc, char *argv[])
{
  std::string transport       = "udp";
  std::string tcpType         = "cubic";   // or "bbr"
  std::string bottleneckRate  = "100Mbps";
  std::string bottleneckDelay = "10ms";
  std::string queueSize       = "100p";
  uint32_t    deadlineMs      = 50;
  double      loss            = 0.0;
  uint32_t    frameSize       = 90000;  

  CommandLine cmd;
  cmd.AddValue ("transport", "Transport protocol: udp or tcp", transport);
  cmd.AddValue ("tcp",       "tcp type: cubic or bbr",         tcpType);
  cmd.AddValue ("rate",      "Bottleneck data rate",           bottleneckRate);
  cmd.AddValue ("delay",     "Bottleneck delay",               bottleneckDelay);
  cmd.AddValue ("deadline",  "Per-frame deadline (ms)",        deadlineMs);
  cmd.AddValue ("loss",      "Packet loss rate [0..1.0]",      loss);
  cmd.AddValue ("frameSize", "Downlink frame size in bytes",   frameSize);
  cmd.AddValue ("queue",     "queue buffer size",              queueSize);
  cmd.Parse (argc, argv);

  NodeContainer nodes;
  nodes.Create (2);

  // point-to-point bottleneck
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue (bottleneckRate));
  p2p.SetChannelAttribute ("Delay",   StringValue (bottleneckDelay));
  p2p.SetQueue("ns3::DropTailQueue<Packet>",
             "MaxSize", QueueSizeValue(QueueSize(queueSize)));

  NetDeviceContainer devs = p2p.Install (nodes);

  // optional: emulate wireless/last-hop loss on receiver side
  if (loss > 0.0)
  {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorRate", DoubleValue (loss));
    devs.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
  }

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifs = address.Assign (devs);

  // downlink: VR frames from node0 -> node1
  //Ptr<Socket> sock = Socket::CreateSocket (nodes.Get (0), UdpSocketFactory::GetTypeId ());
  if (transport == "tcp")
  {
      if (tcpType == "bbr")
      {
          Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                            TypeIdValue(ns3::TcpBbr::GetTypeId()));
      }
      else  // cubic
      {
          Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                            TypeIdValue(ns3::TcpCubic::GetTypeId()));
      }
  }

  Ptr<Socket> sock;

  if (transport == "tcp")
  {
      sock = Socket::CreateSocket (nodes.Get(0), TcpSocketFactory::GetTypeId());
  }
  else if (transport == "udp" || transport == "quic")
  {
      sock = Socket::CreateSocket (nodes.Get(0), UdpSocketFactory::GetTypeId());
  }
  else
  {
      NS_FATAL_ERROR ("Unknown transport: " << transport);
  }

  Address peer;
  if (transport == "tcp")
  {
      peer = InetSocketAddress(ifs.GetAddress(1), 5000);
  }
  else
  {
      peer = InetSocketAddress(ifs.GetAddress(1), 5000);
  }

  // 是否启用 QUIC-lite pacing：只有 transport == "quic" 时才开
  bool usePacing = (transport == "quic");

  // 帧间隔：你之前就是 33ms，这里先保持一致
  Time frameInterval = MilliSeconds (33);

  Ptr<VrDownlinkApp> app = CreateObject<VrDownlinkApp> ();
  app->Setup (sock, peer,
              frameSize,        // frame size
              frameInterval,    // frame 间隔
              1200,             // payload per packet
              usePacing,        // 是否启用 pacing
              MicroSeconds (200)); // fragment 间 pacing，可以之后自己调
  nodes.Get (0)->AddApplication (app);
  app->SetStartTime (Seconds (1.0));
  app->SetStopTime  (Seconds (10.0));


  // receiver: measure on-time frame ratio
  Ptr<VrReceiverApp> recv = CreateObject<VrReceiverApp> ();
  recv->SetDeadlineMs (deadlineMs);
  nodes.Get (1)->AddApplication (recv);
  recv->SetUseTcp( transport == "tcp" );
  recv->SetStartTime (Seconds (0.0));
  recv->SetStopTime  (Seconds (10.0));


  // uplink: periodic sensor/control packets, node1 -> node0
  uint16_t ulPort = 6000;
  Ptr<Socket> upSock = Socket::CreateSocket (nodes.Get (1), UdpSocketFactory::GetTypeId ());
  Ptr<VrUplinkApp> up = CreateObject<VrUplinkApp> ();
  up->Setup (upSock,
             InetSocketAddress (ifs.GetAddress (0), ulPort),
             MilliSeconds (10),  // 100 Hz
             100);               // 100 B
  nodes.Get (1)->AddApplication (up);
  up->SetStartTime (Seconds (1.0));
  up->SetStopTime  (Seconds (10.0));

  // sink for uplink packets on node0
  /*PacketSinkHelper ulSink ("ns3::UdpSocketFactory",
                           InetSocketAddress (Ipv4Address::GetAny (), ulPort));
  ApplicationContainer ulSinkApp = ulSink.Install (nodes.Get (0));
  ulSinkApp.Start (Seconds (0.0));
  ulSinkApp.Stop  (Seconds (10.0));
  */

  Ptr<VrUplinkReceiver> ulRecv = CreateObject<VrUplinkReceiver>();
  nodes.Get(0)->AddApplication(ulRecv);
  ulRecv->SetStartTime(Seconds(0.0));
  ulRecv->SetStopTime(Seconds(10.0));

  // collect flow-level stats
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (20.0));
  Simulator::Run ();

  auto &m_delays = ulRecv->m_delays;

  if (!m_delays.empty()) {
      uint64_t sum = 0;
      uint32_t maxd = 0;
      for (uint32_t d : m_delays) {
          sum += d;
          maxd = std::max(maxd, d);
      }

      double avg = double(sum) / m_delays.size();

      std::sort(m_delays.begin(), m_delays.end());
      size_t idx = static_cast<size_t>(m_delays.size() * 0.99);
      if (idx >= m_delays.size()) idx = m_delays.size() - 1;
      uint32_t p99 = m_delays[idx];

      std::cout << "[UL-IMU] avgDelay=" << avg
                << " p99=" << p99
                << " max=" << maxd << std::endl;
  } else {
      std::cout << "[UL-IMU] noSamples=1 avgDelay=0 p99=0 max=0" << std::endl;
  }



  std::ostringstream oss;
  oss << "arvr_"
        << "tx-"        << transport
        << "_tcp-"      << tcpType
        << "_rate-"     << bottleneckRate
        << "_delay-"    << bottleneckDelay
        << "_loss-"     << loss
        << "_deadline-" << deadlineMs
        << "_fs-"       << frameSize
        << "_queue-"    << queueSize
        << ".xml";

  monitor->SerializeToXmlFile (oss.str (), true, true);

  // print values
  uint32_t total      = recv->GetTotalFrames ();
  uint32_t ontime     = recv->GetOnTimeFrames ();
  uint32_t late       = recv->GetLateFrames ();
  uint32_t incomplete = recv->GetIncompleteFrames ();

  double ratio = total ? (double)ontime / total : 0.0;

  std::cout << "[VR-RECV] total=" << total
            << " onTime=" << ontime
            << " late=" << late
            << " incomplete=" << incomplete
            << " ratio=" << ratio
            << std::endl;

  Simulator::Destroy ();
  return 0;
}
