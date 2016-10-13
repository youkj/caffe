
if [ $# -ne 1 ]; then
	echo "input 1 to enable / 0 to disable"
	exit 0
fi

if [ $1 -ne 0 ] && [ $1 -ne 1 ]; then
	echo "input 1 to enable / 0 to disable"
	exit 0
fi

CPUFILE=/root/THREAD_SIBLINGS_LIST
if [ ! -f "$CPUFILE" ]; then
	cat /sys/devices/system/cpu/cpu*/topology/thread_siblings_list >$CPUFILE
fi

for cpunum in $(
cat $CPUFILE |
cut -s -d, -f2- | tr ',' '\n' | sort -un);
do
	echo "$1 --> "$cpunum
	echo $1 > /sys/devices/system/cpu/cpu$cpunum/online
done

unset CPUFILE