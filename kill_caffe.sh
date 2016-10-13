
pidlist=$(pgrep caffe)

if [ "$pidlist" == "" ]; then
	echo "no caffe to kill"
	exit 0
fi

kill -9 $pidlist

echo "killed all caffe"

