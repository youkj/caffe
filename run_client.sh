
export OMP_NUM_THREADS=42
export MKL_NUM_THREADS=42
export KMP_AFFINITY="compact,granularity=fine"

nohup ./build/tools/conv_client -ip 192.168.10.100 >conv.log 2>&1 &

