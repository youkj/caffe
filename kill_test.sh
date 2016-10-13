
pidlist=$(pgrep model_test)

if [ "$pidlist" == "" ]; then
	echo "no model_test to kill"
	exit 0
fi

kill -9 $pidlist

echo "killed all model_test"

