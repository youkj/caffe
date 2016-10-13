
pidlist=$(pgrep conv_client)

if [ "$pidlist" == "" ]; then
	echo "no conv_client to kill"
	exit 0
fi

kill -9 $pidlist

echo "killed all conv_client"

