
export OMP_NUM_THREADS=2
export MKL_NUM_THREADS=2
#export KMP_AFFINITY="verbose,granularity=thread,scatter"


N_ITER=1000
CAFFE=./build/tools/caffe
solver=models/googlenet_v2/solver.prototxt
model=models/googlenet_v2/train_val.prototxt

logfile=log_score_6000.log
rm -f $logfile
wgt="models/googlenet_v2/bvlc_googlenet_iter_6000.caffemodel"


nohup $CAFFE "test" --model=$model -weights=$wgt -iterations=$N_ITER >$logfile 2>&1 &



logfile=log_score_5000.log
rm -f $logfile
wgt="models/googlenet_v2/bvlc_googlenet_iter_5000.caffemodel"


nohup $CAFFE "test" --model=$model -weights=$wgt -iterations=$N_ITER >$logfile 2>&1 &



