#! /bin/bash
# Copyright (c) 2020, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of The Linux Foundation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

ROOT_DIR=$(realpath $(dirname $0)/../..)

set -e

source "${ROOT_DIR}/build/_setup_env.sh"

rm -rf $3
mkdir $3

set -x
$ROOT_DIR/build/android/merge_dtbs.py $1 $2 $3
set +x

function dtbo_pack_by_proj_name(){
	pushd ${1}
	#1. get project name, such as xueying  zonda xigua....
	proj_name=()
	DTBO=$(find ./ -type f -name '*.dtbo' -print)
	for dtbo in ${DTBO[@]}
	do
		#echo ".dtbo" $dtbo
		prop=`basename $dtbo | cut -d "-" -f 1`
		proj_name+=" "${prop}
		#echo "proj_name" ${proj_name}
	done
	proj_name=($(awk -v RS=' ' '!a[$1]++' <<< ${proj_name[@]}))

	#debug print project name
	for e in ${proj_name[@]}
	do
		echo "proj_name2" ${e}
	done

	#2.get project number str  and  match dtbo list(.dtbo file for zonda xueying...) + build dtbo
	for name in ${proj_name[@]}
	do
		echo "start build" ${name}
		proj_num=()
		dtbo_list=()
		for dtbo_tmp in ${DTBO[@]}
		do
			#echo "search DTBO"${dtbo_tmp}
			if [[ "${dtbo_tmp}" =~ "${name}" ]];then
				#echo "dtbo file name match" ${dtbo_tmp}
				#get .dtbo list of project name
				dtbo_list+=" "${dtbo_tmp}
				#get project number of project name
				prop=($(fdtget -t i ${dtbo_tmp} / oplus,project-id))
				proj_num+=" "${prop[@]}
			fi
		done
		proj_num=($(awk -v RS=' ' '!a[$1]++' <<< ${proj_num[@]}))
		proj_num_str=''
		for tmp in ${proj_num[@]}
		do
			if [ $tmp -gt 100000 ];then
				proj_num_str+="-"`printf '%X' $tmp`
			else
				proj_num_str+="-"$tmp
			fi
		done
		dtbo_name_str=${name}${proj_num_str}
		#echo "dtbo list" ${dtbo_list}
		#echo "dtbo_name_str"  ${dtbo_name_str}
		echo "pack ${dtbo_name_str}-dtbo.img with ${dtbo_list[@]}"
		mkdtboimg.py create ${dtbo_name_str}-dtbo.img --page_size=${PAGE_SIZE} ${dtbo_list[@]}
	done
	popd
}
#dtbo_pack ${3}
dtbo_pack_by_proj_name ${3}
[[ -n "$(find ${3} -type f -name '*.dtb')" ]] && cat ${3}/*.dtb > ${3}/dtb.img
#[[ -n "$(find ${3} -type f -name '*.dtbo')" ]] && mkdtboimg.py create${3}/dtbo.img --page_size=${PAGE_SIZE} ${3}/*.dtbo
[[ -n "$(find ${3} -type f -name '*dtbo.img')" ]] && (img=($(find ${3} -type f -name '*dtbo.img'));cp ${img[0]} ${3}/dtbo.img)
exit 0
