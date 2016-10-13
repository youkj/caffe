
#======turbo.sh==================
#!/bin/sh

cores=$(cat /proc/cpuinfo | grep process | awk '{print $3}')

if [[ ${#} == 0 ]]; then
    for core in $cores
    do
        status=`rdmsr -p${core} 0x1a0 -f 38:38`
        if [[ ${status} == 1 ]]; then
            echo "Core[${core}]:Disabled"
        else
            echo "Core[${core}]:Enabled"
        fi
    done
elif [[ $1 == "0" ]]; then
    echo disable turbo
    for core in $cores
    do
        wrmsr -p${core} 0x1a0 0x4000850089
    done
elif [[ $1 == "1" ]]; then
   echo enable turbo
    for core in $cores
    do
        wrmsr -p${core} 0x1a0 0x850089
    done
fi


