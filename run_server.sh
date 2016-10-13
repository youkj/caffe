

# model server
nohup ./build/tools/model_server -workers 1 -sub_solvers 4 -solver models/bvlc_alexnet/solver.prototxt >model.log 2>&1 &

# fc client
export OMP_NUM_THREADS=4
export MKL_NUM_THREADS=4
export KMP_AFFINITY="verbose,granularity=thread,scatter"

nohup ./build/tools/fc_server -threads 1 -request models/bvlc_alexnet/fc.prototxt >fc.log 2>&1 &

# param server
nohup ./build/tools/param_server -request models/bvlc_alexnet/ps.prototxt >param.log 2>&1 &
