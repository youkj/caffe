# model server
nohup ./build/tools/model_server.bin -workers 2 -sub_solvers 4 -solver models/googlenet_v2/solver.prototxt >model.log 2>&1 &

# fc client
export OMP_NUM_THREADS=4
export MKL_NUM_THREADS=4
#export KMP_AFFINITY="verbose,granularity=thread,scatter"

nohup ./build/tools/fc_server.bin -threads 2 -request models/googlenet_v2/fc1.prototxt >fc1.log 2>&1 &
nohup ./build/tools/fc_server.bin -threads 2 -request models/googlenet_v2/fc2.prototxt >fc2.log 2>&1 &
nohup ./build/tools/fc_server.bin -threads 2 -request models/googlenet_v2/fc3.prototxt >fc3.log 2>&1 &

# param server
nohup ./build/tools/param_server.bin -request models/googlenet_v2/ps.prototxt >param.log 2>&1 &
