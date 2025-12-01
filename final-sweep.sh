#!/bin/bash

OUT="results_final.csv"
echo "transport,tcpType,group,rate,delay,loss,deadline,frameSize,queue,total,onTime,late,incomplete,ratio,ul_avg,ul_p99,ul_max" > $OUT

DEADLINE=80
DELAY_BASE=10ms
RATE_BASE=120Mbps
LOSS_BASE=0
FRAMESIZE_BASE=90000
QUEUE_BASE="100p"

RUN() {
    transport=$1
    tcpType=$2
    group=$3
    rate=$4
    delay=$5
    loss=$6
    deadline=$7
    fs=$8
    q=$9

    cmd="./ns3 run \"scratch/arvr-sim --transport=$transport --tcp=$tcpType \
         --rate=$rate --delay=$delay --loss=$loss \
         --deadline=$deadline --frameSize=$fs --queue=$q\""

    LOG=$(eval $cmd 2>&1)

    vrline=$(echo "$LOG" | grep -F "[VR-RECV]")
    ulline=$(echo "$LOG" | grep -F "[UL-IMU]")

    total=$(echo $vrline | awk '{print $2}' | cut -d= -f2)
    onTime=$(echo $vrline | awk '{print $3}' | cut -d= -f2)
    late=$(echo $vrline | awk '{print $4}' | cut -d= -f2)
    incomplete=$(echo $vrline | awk '{print $5}' | cut -d= -f2)
    ratio=$(echo $vrline | awk '{print $6}' | cut -d= -f2)

    ul_avg=$(echo $ulline | awk '{print $2}' | cut -d= -f2)
    ul_p99=$(echo $ulline | awk '{print $3}' | cut -d= -f2)
    ul_max=$(echo $ulline | awk '{print $4}' | cut -d= -f2)

    echo "$transport,$tcpType,$group,$rate,$delay,$loss,$deadline,$fs,$q,$total,$onTime,$late,$incomplete,$ratio,$ul_avg,$ul_p99,$ul_max" >> $OUT
}

########################################
# 1. DELAY SWEEP
########################################
DELAYS=("10ms" "30ms" "50ms" "70ms" "90ms" "110ms")
for d in ${DELAYS[@]}; do
    for proto in "udp none" "quic none" "tcp cubic" "tcp bbr"; do
        set -- $proto
        RUN $1 $2 dsweep $RATE_BASE $d 0 $DEADLINE $FRAMESIZE_BASE $QUEUE_BASE
    done
done

########################################
# 2. LOSS SWEEP
########################################
LOSSES=("0" "0.000001" "0.00001" "0.0001" "0.001" "0.01")
for l in ${LOSSES[@]}; do
    for proto in "udp none" "quic none" "tcp cubic" "tcp bbr"; do
        set -- $proto
        RUN $1 $2 lsweep $RATE_BASE $DELAY_BASE $l $DEADLINE $FRAMESIZE_BASE $QUEUE_BASE
    done
done

########################################
# 3. RATE SWEEP
########################################
RATES=("30Mbps" "40Mbps" "50Mbps" "60Mbps" "70Mbps" "80Mbps" "100Mbps" "120Mbps")
for r in ${RATES[@]}; do
    for proto in "udp none" "quic none" "tcp cubic" "tcp bbr"; do
        set -- $proto
        RUN $1 $2 rsweep $r $DELAY_BASE 0 $DEADLINE $FRAMESIZE_BASE $QUEUE_BASE
    done
done

########################################
# 4. FRAMESIZE SWEEP
########################################
FRAMES=("90000" "120000" "150000" "180000" "220000" "250000")
for fs in ${FRAMES[@]}; do
    for proto in "udp none" "quic none" "tcp cubic" "tcp bbr"; do
        set -- $proto
        RUN $1 $2 fsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $fs $QUEUE_BASE
    done
done

########################################
# 5. QUEUESIZE SWEEP
########################################
QUEUES=("50p" "100p" "300p")
for q in ${QUEUES[@]}; do
    for proto in "udp none" "quic none" "tcp cubic" "tcp bbr"; do
        set -- $proto
        RUN $1 $2 qsweep $RATE_BASE $DELAY_BASE 0 $DEADLINE $FRAMESIZE_BASE $q
    done
done

echo "All sweeps done. Results saved to $OUT"
