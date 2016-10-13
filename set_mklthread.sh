
if [ $# -lt 1 ] || [ $1 -gt 44 ] || [ $1 -lt 0 ]; then
	echo "please input mkl threads number(0~44)"
	echo "0 represent unset"
	exit 0
fi

if [ $1 -eq 0 ]; then
	unset OMP_NUM_THREADS
        unset MKL_NUM_THREADS
        echo "unset OMP_NUM_THREADS, MKL_NUM_THREADS"
else 
	export OMP_NUM_THREADS=$1
	export MKL_NUM_THREADS=$1
	echo "OMP_NUM_THREADS=$1"
	echo "MKL_NUM_THREADS=$1"
fi


