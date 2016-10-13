

pidfc=$(pgrep fc_server)
pidpm=$(pgrep param_server)
pidmd=$(pgrep model_server)

num=0

if [ "$pidfc" == "" ]; then
	echo "no fc_server to kill"
	((num++))
fi

if [ "$pidpm" == "" ]; then
    echo "no param_server to kill"
   ((num++))
fi

if [ "$pidmd" == "" ]; then
    echo "no model_server to kill"
    ((num++))
fi

if [ $num -eq 3 ]; then
	echo "no server to kill"
	exit 0
fi

kill -9 $pidfc $pidpm $pidmd

echo "killed all servers"

