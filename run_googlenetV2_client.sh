
export OMP_NUM_THREADS=2
export MKL_NUM_THREADS=2
export KMP_AFFINITY="compact,granularity=fine"

#nohup ./build/tools/conv_client.bin -ip 192.168.10.12 >conv.log 2>&1 &

nohup ./build/tools/conv_client.bin -threads 20 -ip 192.168.10.12 >conv.log 2>&1 &


