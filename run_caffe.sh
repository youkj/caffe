
#export OMP_NUM_THREADS=44

N_ITER=
CAFFE=./build/tools/caffe

function usage()
{
	echo "Usage:"
	echo "./run.sh time/train/score alexnet/googlenet (snapshot)"
	echo "for training, snapshot is the solverstart to resume the training"
	echo "for score, snapshot is the weight"
}

if [ $# -lt 2 ]; then
	usage
	exit 0
fi

if [ $# -eq 3 ]; then
	N_ITER=$3
else
	N_ITER=100
fi


logfile=log_$1_$2.log
rm -f $logfile

if [ $2 != "alexnet" ] && [ $2 != "googlenet" ]; then
	echo "Only support alexnet and googlenet yet!"
	exit 0
fi

solver=models/bvlc_$2/solver.prototxt
model=models/bvlc_$2/train_val.prototxt

if [ $1 == "time" ]; then

	$CAFFE $1 --model=$model -iterations=$N_ITER 2>&1 | tee -a $logfile

elif [ $1 == "train" ]; then

	nohup $CAFFE $1 --solver=$solver -snapshot=$3 >$logfile 2>&1 &
	echo -e "Start training"\\n"Please check $logfile"

elif [ $1 == "score" ]; then

	if [ $# -eq 2 ]; then
		echo "Please input weight models"
		usage
		exit 0
	fi
	$CAFFE "test" --model=$model -weights=$3 -iterations=$N_ITER 2>&1 | tee -a $logfile 
	
else
	echo "Unknow input cmd!"
	usage
	exit 0
fi

