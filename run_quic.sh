#!/bin/bash

OUT="results.csv"
echo "transport,tcpType,group,rate,delay,loss,deadline,frameSize,total,onTime,late,incomplete,ratio" > $OUT

# 固定参数
DEADLINE=80
DELAY_BASE=10ms
RATE_BASE=120Mbps
LOSS_BASE=0

# ---- 包装一层统一执行函数 ----
RUN() {
    transport=$1   # udp / quic / tcp
    tcpType=$2     # none / cubic / bbr
    group=$3       # dsweep / lsweep / rsweep / fsweep
    rate=$4
    delay=$5
    loss=$6
    deadline=$7
    fs=$8

    cmd="./ns3 run \"scratch/arvr-sim --transport=$transport --tcp=$tcpType \
         --rate=$rate --delay=$delay --loss=$loss \
         --deadline=$deadline --frameSize=$fs\""

    LOG=$(eval $cmd 2>&1 | grep -F "[VR-RECV]")

    # 提取数字
    total=$(echo $LOG | awk '{print $2}' | cut -d= -f2)
    onTime=$(echo $LOG | awk '{print $3}' | cut -d= -f2)
    late=$(echo $LOG | awk '{print $4}'   | cut -d= -f2)
    incomplete=$(echo $LOG | awk '{print $5}' | cut -d= -f2)
    ratio=$(echo $LOG | awk '{print $6}'  | cut -d= -f2)

    echo "$transport,$tcpType,$group,$rate,$delay,$loss,$deadline,$fs,$total,$onTime,$late,$incomplete,$ratio" >> $OUT
}

############################################################
# 1. DELAY SWEEP
############################################################
echo "Running delay sweep..."

DELAYS=("10ms" "30ms" "50ms" "70ms" "90ms" "110ms")

for d in ${DELAYS[@]}; do
    RUN udp none  dsweep $RATE_BASE $d 0 $DEADLINE 90000
    RUN quic none dsweep $RATE_BASE $d 0 $DEADLINE 90000
    RUN tcp cubic dsweep $RATE_BASE $d 0 $DEADLINE 90000
    RUN tcp bbr   dsweep $RATE_BASE $d 0 $DEADLINE 90000
done

############################################################
# 2. LOSS SWEEP
############################################################
echo "Running loss sweep..."

LOSSES=("0" "0.000001" "0.00001" "0.0001" "0.001" "0.01")

for l in ${LOSSES[@]}; do
    RUN udp none  lsweep $RATE_BASE $DELAY_BASE $l $DEADLINE 90000
    RUN quic none lsweep $RATE_BASE $DELAY_BASE $l $DEADLINE 90000
    RUN tcp cubic lsweep $RATE_BASE $DELAY_BASE $l $DEADLINE 90000
    RUN tcp bbr   lsweep $RATE_BASE $DELAY_BASE $l $DEADLINE 90000
done

############################################################
# 3. RATE SWEEP
############################################################
echo "Running rate sweep..."

RATES=("30Mbps" "40Mbps" "50Mbps" "60Mbps" "70Mbps" "80Mbps" "100Mbps" "120Mbps")

for r in ${RATES[@]}; do
    RUN udp none  rsweep $r $DELAY_BASE 0 $DEADLINE 90000
    RUN quic none rsweep $r $DELAY_BASE 0 $DEADLINE 90000
    RUN tcp cubic rsweep $r $DELAY_BASE 0 $DEADLINE 90000
    RUN tcp bbr   rsweep $r $DELAY_BASE 0 $DEADLINE 90000
done

############################################################
# 4. FRAMESIZE SWEEP
############################################################
echo "Running frameSize sweep..."

FRAMES=("90000" "120000" "150000" "180000" "220000" "250000")

for fs in ${FRAMES[@]}; do
    RUN udp none  fsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $fs
    RUN quic none fsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $fs
    RUN tcp cubic fsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $fs
    RUN tcp bbr   fsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $fs
done

echo "All sweeps complete! Results saved to $OUT"
