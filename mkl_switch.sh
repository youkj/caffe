
ETC_MKL=/etc/profile.d/mkl.sh
PATH_MKL_SET=/home/cldai/ips
PATH_MKL2017=/home/zhouting/mkl_2017b1_20160513_lnx/__release_lnx
PATH_MKL2016=/home/cldai/ips/compilers_and_libraries_2016.3.210/linux
PATH_MKL0801=/home/tangjian/mkl_0801/__release_lnx
PATH_MKL0513=/home/tangjian/mkl_0513/__release_lnx


function usage()
{
	echo "Usage:"
	echo "./mkl_switch 0801/0513/2017/2016 (turbo) (HT)"
	echo "turbo : 0/1"
	echo "HT : 0/1"
}

if [ $# -eq 0 ]; then
	echo "input 2016 or 2017 or 0801 or 0513"
	usage
	exit 0
fi

tb=
ht=

config="CPU_ONLY := 1"\\n
config+="SHARE_DATA_LAYER := 1"\\n
config+="BLAS := mkl"\\n

if [ $1 == "2016" ]; then
	
	tb=0
	ht=0
	if  [[ $2 == "0" ]] || [[ $2 == "1" ]]; then
		tb=$2
	fi
	if  [[ $3 == "0" ]] || [[ $3 == "1" ]]; then
		ht=$3
	fi
	
	echo -e "source $PATH_MKL2016/mkl/bin/mklvars.sh intel64">$ETC_MKL
	source $ETC_MKL

	cd $PATH_MKL_SET
	rm -rf tbb mkl

	ln -s $PATH_MKL2016/tbb tbb
	ln -s $PATH_MKL2016/mkl mkl

	cd -
	
	config+="BLAS_INCLUDE := $PATH_MKL2016/mkl/include"\\n
	config+="BLAS_LIB := $PATH_MKL2016/mkl/lib"\\n
	
elif [ $1 == "2017" ]; then

	tb=0
	ht=0
	if  [[ $2 == "0" ]] || [[ $2 == "1" ]]; then
		tb=$2
	fi
	if  [[ $3 == "0" ]] || [[ $3 == "1" ]]; then
		ht=$3
	fi
	
	echo -e "source $PATH_MKL2017/mkl/bin/mklvars.sh intel64">$ETC_MKL
	source $ETC_MKL
	
	cd $PATH_MKL_SET
        rm -rf tbb mkl
	
	ln -s $PATH_MKL2017/tbb tbb
        ln -s $PATH_MKL2017/mkl mkl

	cd -

	config+="USE_MKL2017_AS_DEFAULT_ENGINE := 1"\\n
	config+="BLAS_INCLUDE := $PATH_MKL2017/mkl/include"\\n
	config+="BLAS_LIB := $PATH_MKL2017/mkl/lib"\\n

elif [ $1 == "0801" ]; then

	tb=0
    ht=0
    if  [[ $2 == "0" ]] || [[ $2 == "1" ]]; then
        tb=$2
    fi
    if  [[ $3 == "0" ]] || [[ $3 == "1" ]]; then
        ht=$3
    fi

    echo -e "source $PATH_MKL0801/mkl/bin/mklvars.sh intel64">$ETC_MKL
    source $ETC_MKL

    cd $PATH_MKL_SET
    rm -rf tbb mkl

    ln -s $PATH_MKL0801/tbb tbb
    ln -s $PATH_MKL0801/mkl mkl

    cd -

    config+="USE_MKL2017_AS_DEFAULT_ENGINE := 1"\\n
    config+="BLAS_INCLUDE := $PATH_MKL0801/mkl/include"\\n
    config+="BLAS_LIB := $PATH_MKL0801/mkl/lib"\\n

elif [ $1 == "0513" ]; then

	tb=0
    ht=0
    if  [[ $2 == "0" ]] || [[ $2 == "1" ]]; then
        tb=$2
    fi
    if  [[ $3 == "0" ]] || [[ $3 == "1" ]]; then
        ht=$3
    fi

    echo -e "source $PATH_MKL0513/mkl/bin/mklvars.sh intel64">$ETC_MKL
    source $ETC_MKL

    cd $PATH_MKL_SET
    rm -rf tbb mkl

    ln -s $PATH_MKL0513/tbb tbb
    ln -s $PATH_MKL0513/mkl mkl

    cd -

    config+="USE_MKL2017_AS_DEFAULT_ENGINE := 1"\\n
    config+="BLAS_INCLUDE := $PATH_MKL0513/mkl/include"\\n
    config+="BLAS_LIB := $PATH_MKL0513/mkl/lib"\\n

else
	echo "wrong input"
	usage
	exit 0
fi

echo "$tb --> Turbo"
echo "$ht --> HT"
systemctl stop firewalld.service
./set_turbo.sh $tb
source set_ht.sh $ht
	
python_inc="/usr/include/python2.7 /usr/lib/python2.7/dist-packages/numpy/core/include"
python_lib="/usr/lib"
config+="PYTHON_INCLUDE := $python_inc"\\n
config+="PYTHON_LIB := $python_lib"\\n
config+="INCLUDE_DIRS := $python_inc /usr/local/include"\\n
config+="LIBRARY_DIRS := $python_lib /usr/local/lib /usr/lib"\\n
config+="USE_PKG_CONFIG := 1"\\n
config+="BUILD_DIR := build"\\n
config+="DISTRIBUTE_DIR := distribute"\\n
config+="TEST_GPUID := 0"\\n
config+="Q ?= @"\\n

rm -f Makefile.config
echo -e $config>Makefile.config

source set_mklthread.sh 0

unset tb
unset ht
unset ETC_MKL
unset PATH_MKL_SET
unset PATH_MKL2017
unset PATH_MKL2016
unset python_inc
unset python_lib
unset config


