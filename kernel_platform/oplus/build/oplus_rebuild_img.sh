#!/bin/bash

function init_image_environment() {
    source kernel_platform/oplus/build/oplus_setup.sh $1 $2
    TOPDIR=${PWD}
    ACKDIR=${TOPDIR}/kernel_platform
    TOOLS=${ACKDIR}/oplus/tools
    ORIGIN_IMAGE=${ACKDIR}/oplus/prebuild/origin_img
    BOOT_TMP_IMAGE=${ACKDIR}/oplus/prebuild/boot_tmp
    IMAGE_OUT=${ACKDIR}/oplus/prebuild/out
    VENDOR_BOOT_TMP_IMAGE=${ACKDIR}/oplus/prebuild/vendor_boot_tmp
    VENDOR_DLKM_TMP_IMAGE=${ACKDIR}/oplus/prebuild/vendor_dlkm_tmp
    DT_TMP_IMAGE=${ACKDIR}/oplus/prebuild/dt_tmp
    DT_DIR=${ACKDIR}/out/msm-${pre_path}-${variants_platform}-${variants_type}/dist
    PYTHON_TOOL="${ACKDIR}/prebuilts/build-tools/path/linux-x86/python3"
    MKBOOTIMG_PATH=${ACKDIR}/"tools/mkbootimg/mkbootimg.py"
    UNPACK_BOOTIMG_TOOL="${ACKDIR}/tools/mkbootimg/unpack_bootimg.py"
    LZ4="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/lz4"
    CPIO="${ACKDIR}/prebuilts/build-tools/path/linux-x86/cpio"
    SIMG2IMG="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/simg2img"
    IMG2SIMG="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/img2simg"
    EROFS="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/mkfs.erofs"
    BUILD_IMAGE="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/build_image"
    MKDTIMG="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/mkdtimg"
    MKDTOIMG="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/mkdtboimg.py"
    MKBOOTFS="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/mkbootfs"
    AVBTOOL="${ACKDIR}/prebuilts/kernel-build-tools/linux-x86/bin/avbtool"
    STRIP="${ACKDIR}/prebuilts-master/clang/host/linux-x86/llvm-binutils-stable/llvm-strip"
    IMAGE_SERVER="xxx"
    mkdir -p ${IMAGE_OUT}
    mkdir -p ${ORIGIN_IMAGE}
}

function is_intranet() {
    ping -c1 -i1 gerrit_url > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "This is an internal network"
        IS_INTRANET="yes"
    else
        echo "This is not an internal network"
        IS_INTRANET="no"
    fi
}

function download_prebuild_image() {
    if test "$IS_INTRANET" = "yes"; then
        if [ -z ${IMAGE_SERVER} ]; then
            echo "======================================================================"
            echo ""
            echo "you need input base version like this:"
            echo "http://xxx..xxx.com/xxx/userdebug/xxx_userdebug/"
            echo ""
            echo "or you can exit it and then exoprt like this:"
            echo "export IMAGE_SERVER=http://xxx..xxx.com/xxx/userdebug/xxx_userdebug/"
            echo ""
            echo "======================================================================"
            read IMAGE_SERVER
        fi

        if ! wget -qS ${IMAGE_SERVER}/compile.ini; then
            echo "server can't connect,please set IMAGE_SERVER and try again"
            return
        fi
        if [[ ! -e "${ORIGIN_IMAGE}/vendor_boot.img" ]]; then
            mkdir -p ${ORIGIN_IMAGE}
            wget ${IMAGE_SERVER}/compile.ini  -O ${ORIGIN_IMAGE}/compile.ini
            OFP_DRI=`cat ${ORIGIN_IMAGE}/compile.ini | grep "ofp_folder =" | awk '{print $3 }'`
            wget ${IMAGE_SERVER}/${OFP_DRI}/IMAGES/boot.img -O ${ORIGIN_IMAGE}/boot.img
            wget ${IMAGE_SERVER}/${OFP_DRI}/IMAGES/vendor_boot.img -O ${ORIGIN_IMAGE}/vendor_boot.img
            wget ${IMAGE_SERVER}/${OFP_DRI}/IMAGES/vendor_dlkm.img -O ${ORIGIN_IMAGE}/vendor_dlkm.img
            wget ${IMAGE_SERVER}/${OFP_DRI}/IMAGES/dtbo.img -O ${ORIGIN_IMAGE}/dtbo.img
            wget ${IMAGE_SERVER}/${OFP_DRI}/META/misc_info.txt -O ${ORIGIN_IMAGE}/misc_info.txt
            wget ${IMAGE_SERVER}/${OFP_DRI}/META/vendor_dlkm_image_info.txt -O ${ORIGIN_IMAGE}/vendor_dlkm_image_info.txt
            wget ${IMAGE_SERVER}/build.json    -O ${ORIGIN_IMAGE}/build.json
            wget ${IMAGE_SERVER}/${OFP_DRI}/META/file_contexts.bin -O ${ORIGIN_IMAGE}/file_contexts.bin
            cp ${TOOLS}/testkey_rsa4096.pem ${ORIGIN_IMAGE}/
            cp ${TOOLS}/testkey_rsa2048.pem ${ORIGIN_IMAGE}/
            cp ${TOOLS}/testkey.avbpubkey ${ORIGIN_IMAGE}/
        fi
    fi
}

function get_image_info() {

    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/boot.img >  ${ORIGIN_IMAGE}/local_boot_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/vendor_boot.img >  ${ORIGIN_IMAGE}/local_vendor_boot_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/vendor_dlkm.img >  ${ORIGIN_IMAGE}/local_vendor_dlkm_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/dtbo.img >  ${ORIGIN_IMAGE}/local_dtbo_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/vbmeta.img >  ${ORIGIN_IMAGE}/local_vbmeta_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/vbmeta_system.img >  ${ORIGIN_IMAGE}/local_vbmeta_system_image_info.txt
    ${AVBTOOL} info_image --image  ${ORIGIN_IMAGE}/vbmeta_vendor.img >  ${ORIGIN_IMAGE}/local_vbmeta_vendor_image_info.txt

}

function sign_boot_image() {

    if [[ "$IS_INTRANET" == "yes" ]];then
        algorithm=$(awk -F '[= ]' '$1=="avb_boot_algorithm" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        partition_size=$(awk -F '[= ]' '$1=="boot_size" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        footer_args=$(awk -F '[= ]' '$1=="avb_boot_add_hash_footer_args" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
    else
        algorithm="SHA256_RSA4096"
        partition_size=`cat ${ORIGIN_IMAGE}/local_vendor_boot_image_info.txt | grep "Image size:" | awk '{print $3 }'`
    fi
    partition_name=boot
    salt=`uuidgen | sed 's/-//g'`

    ${AVBTOOL} add_hash_footer \
        --image ${IMAGE_OUT}/boot.img  \
        --partition_name ${partition_name} \
        --partition_size ${partition_size}\
        --algorithm ${algorithm} \
        --key ${ORIGIN_IMAGE}/testkey_rsa4096.pem \
        --salt ${salt} \
        ${footer_args}

    if test "$IS_INTRANET" = "yes"; then
        if [[ -e "${IMAGE_OUT}/signed_boot.img" ]]; then
            rm ${IMAGE_OUT}/signed_boot.img
        fi
        ${TOOLS}/sign_image ${IMAGE_OUT}/boot.img ${IMAGE_SERVER}
        if [[ -e "${IMAGE_OUT}/signed_boot.img" ]]; then
            mkdir -p ${SIGN_OUT}
            mv ${IMAGE_OUT}/signed_boot.img ${SIGN_OUT}/boot.img
            echo "the boot.img is signed successfully!!!"
        else
            echo "the boot.img is failed to be signed!!!"
        fi
    else
        echo "the boot.img is not signed!!!"
    fi
}

function sign_vendor_boot_image() {

    if [[ "$IS_INTRANET" == "yes" ]];then
        algorithm=$(awk -F '[= ]' '$1=="avb_vendor_boot_algorithm" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        partition_size=$(awk -F '[= ]' '$1=="vendor_boot_size" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        footer_args=$(awk -F '[= ]' '$1=="avb_vendor_boot_add_hash_footer_args" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
    else
        algorithm="SHA256_RSA4096"
        partition_size=`cat ${ORIGIN_IMAGE}/local_vendor_boot_image_info.txt | grep "Image size:" | awk '{print $3 }'`
    fi
    partition_name=vendor_boot
    salt=`uuidgen | sed 's/-//g'`

    ${AVBTOOL} add_hash_footer \
        --image ${IMAGE_OUT}/vendor_boot.img  \
        --partition_name ${partition_name} \
        --partition_size ${partition_size}\
        --algorithm ${algorithm} \
        --key ${ORIGIN_IMAGE}/testkey_rsa4096.pem \
        --salt ${salt} \
        ${footer_args}

    if test "$IS_INTRANET" = "yes"; then
        if [[ -e "${IMAGE_OUT}/signed_vendor_boot.img" ]]; then
            rm ${IMAGE_OUT}/signed_vendor_boot.img
        fi
        ${TOOLS}/sign_image ${IMAGE_OUT}/vendor_boot.img ${IMAGE_SERVER}
        if [[ -e "${IMAGE_OUT}/signed_vendor_boot.img" ]]; then
            mkdir -p ${SIGN_OUT}
            mv ${IMAGE_OUT}/signed_vendor_boot.img ${SIGN_OUT}/vendor_boot.img
            echo "the vendor_boot.img is signed successfully!!!"
        else
            echo "the vendor_boot.img is failed to be signed!!!"
        fi
    else
        echo "the vendor_boot.img is not signed!!!"
    fi
}

function sign_dtbo_image() {

    if [[ "$IS_INTRANET" == "yes" ]];then
        algorithm=$(awk -F '[= ]' '$1=="avb_dtbo_algorithm" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        partition_size=$(awk -F '[= ]' '$1=="dtbo_size" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
        footer_args=$(awk -F '[= ]' '$1=="avb_dtbo_add_hash_footer_args" {$1="";print}' ${ORIGIN_IMAGE}/misc_info.txt)
    else
        algorithm="SHA256_RSA4096"
        partition_size=`cat ${ORIGIN_IMAGE}/local_dtbo_image_info.txt | grep "Image size:" | awk '{print $3 }'`
    fi
    partition_name=dtbo
    salt=`uuidgen | sed 's/-//g'`

    ${AVBTOOL} add_hash_footer \
        --image ${IMAGE_OUT}/dtbo.img  \
        --partition_name ${partition_name} \
        --partition_size ${partition_size}\
        --algorithm ${algorithm} \
        --key ${ORIGIN_IMAGE}/testkey_rsa4096.pem \
        --salt ${salt} \
        ${footer_args}

    if test "$IS_INTRANET" = "yes"; then
        if [[ -e "${IMAGE_OUT}/signed_dtbo.img" ]]; then
            rm ${IMAGE_OUT}/signed_dtbo.img
        fi
        ${TOOLS}/sign_image ${IMAGE_OUT}/dtbo.img ${IMAGE_SERVER}
        if [[ -e "${IMAGE_OUT}/signed_dtbo.img" ]]; then
            mkdir -p ${SIGN_OUT}
            mv ${IMAGE_OUT}/signed_dtbo.img ${SIGN_OUT}/dtbo.img
            echo "the dtbo.img is signed successfully!!!"
        else
            echo "the dtbo.img is failed to be signed!!!"
        fi
    else
        echo "the dtbo.img is not signed!!!"
    fi
}

function sign_vendor_dlkm_image() {

    if [[ "$IS_INTRANET" == "yes" ]];then
        algorithm=$(awk -F '[= ]' '$1=="avb_vendor_dlkm_algorithm" {$1="";print}' ${ORIGIN_IMAGE}/vendor_dlkm_image_info.txt)
        partition_size=$(awk -F '[= ]' '$1=="vendor_dlkm_size" {$1="";print}' ${ORIGIN_IMAGE}/vendor_dlkm_image_info.txt)
        footer_args=$(awk -F '[= ]' '$1=="avb_vendor_dlkm_add_hashtree_footer_args" {$1="";print}' ${ORIGIN_IMAGE}/vendor_dlkm_image_info.txt)
    else
        algorithm="SHA256_RSA4096"
        partition_size=`cat ${ORIGIN_IMAGE}/local_vendor_dlkm_image_info.txt | grep "Image size:" | awk '{print $3 }'`
    fi
    partition_name=vendor_dlkm
    salt=`uuidgen | sed 's/-//g'`

    ${AVBTOOL} add_hashtree_footer \
        --partition_name ${partition_name} \
        --partition_size ${partition_size}\
        --do_not_generate_fec \
        --image ${IMAGE_OUT}/vendor_dlkm.img  \
        --hash_algorithm sha256 \
        --salt ${salt}  \
        ${footer_args}

    if test "$IS_INTRANET" = "yes"; then
        if [[ -e "${IMAGE_OUT}/signed_vendor_dlkm.img" ]]; then
            rm ${IMAGE_OUT}/signed_vendor_dlkm.img
        fi
        ${TOOLS}/sign_image ${IMAGE_OUT}/vendor_dlkm.img ${IMAGE_SERVER}
        if [[ -e "${IMAGE_OUT}/signed_vendor_dlkm.img" ]]; then
            mkdir -p ${SIGN_OUT}
            mv ${IMAGE_OUT}/signed_vendor_dlkm.img ${SIGN_OUT}/vendor_dlkm.img
            echo "the vendor_dlkm.img is signed successfully!!!"
        else
            echo "the vendor_dlkm.img is failed to be signed!!!"
        fi
    else
        echo "the vendor_dlkm.img is not signed!!!"
    fi
}

rebuild_boot_image() {
    echo "rebuild boot.img"
    rm -rf ${BOOT_TMP_IMAGE}/*
    boot_mkargs=$(${PYTHON_TOOL} ${UNPACK_BOOTIMG_TOOL} --boot_img ${ORIGIN_IMAGE}/boot.img --out ${BOOT_TMP_IMAGE} --format=mkbootimg)
    cp ${DT_DIR}/Image ${BOOT_TMP_IMAGE}/kernel
    bash -c "${PYTHON_TOOL} ${MKBOOTIMG_PATH} ${boot_mkargs} -o ${IMAGE_OUT}/boot.img"
    sign_boot_image
}

vendor_boot_modules_all_update() {

    echo "vendor_boot module update"

    mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/dist/
    mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/tmp/

    ko_list=`cat ${DT_DIR}/modules.load | xargs -L 1 basename`
    for ko in  $ko_list
    do
        current=`find ${DT_DIR} -name ${ko}`
        if [ -n "${current}" ]; then
            cp ${current} ${VENDOR_BOOT_TMP_IMAGE}/dist/
            ${STRIP} -S ${VENDOR_BOOT_TMP_IMAGE}/dist/${ko} -o ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko}
            cp ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/
            echo  ${ko} >> ${VENDOR_BOOT_TMP_IMAGE}/tmp/modules.load
        fi
    done
    cp ${VENDOR_BOOT_TMP_IMAGE}/tmp/modules.load ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules.load
}

rebuild_vendor_boot_image() {
    echo "rebuild vendor_boot.img"
    rm -rf ${VENDOR_BOOT_TMP_IMAGE}/*
    boot_mkargs=$(${PYTHON_TOOL} ${UNPACK_BOOTIMG_TOOL} --boot_img ${ORIGIN_IMAGE}/vendor_boot.img --out ${VENDOR_BOOT_TMP_IMAGE}/origin --format=mkbootimg)
    #rebuild_dtb_image
    index="00"
    for index in  $index
    do
        echo " index  $index "
        mv ${VENDOR_BOOT_TMP_IMAGE}/origin/vendor_ramdisk${index} ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        ${LZ4} -d ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        rm ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}
        mv ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}
        pushd  ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}
        ${CPIO} -idu < ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}

        popd
        rm ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}

        vendor_boot_modules_all_update
        ${MKBOOTFS} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index} > ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
        ${LZ4} -l -12 --favor-decSpeed ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
        mv ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4 ${VENDOR_BOOT_TMP_IMAGE}/origin/vendor_ramdisk${index}
        rm ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
    done
    bash -c "${PYTHON_TOOL} ${MKBOOTIMG_PATH} ${boot_mkargs} --vendor_boot ${IMAGE_OUT}/vendor_boot.img"
    sign_vendor_boot_image
}

rebuild_vendor_dlkm_image() {
    echo "rebuild vendor_dlkm.img"
    mkdir -p ${VENDOR_DLKM_TMP_IMAGE}
    ${SIMG2IMG} ${ORIGIN_IMAGE}/vendor_dlkm.img  ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img
    ${TOOLS}/7z_new ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm_out
    ${BUILD_IMAGE} ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm_out ${TOOLS}/vendor_dlkm_image_info.txt ${IMAGE_OUT}/vendor_dlkm.img /dev/null
}

vendor_dlkm_modules_update() {
    echo "vendor_dlkm module update"
    mkdir -p ${VENDOR_DLKM_TMP_IMAGE}/dist/
    mkdir -p ${VENDOR_DLKM_TMP_IMAGE}/tmp/
    ko_list=`cat ${KERNEL_OUT}/vendor_dlkm/modules.load | xargs -L 1 basename`
    for ko in  $ko_list
    do
        current=`find ${KERNEL_OUT} -name ${ko}`
        if [ -n "${current}" ]; then
            cp ${current} ${VENDOR_DLKM_TMP_IMAGE}/dist/
            ${STRIP} -S ${VENDOR_DLKM_TMP_IMAGE}/dist/${ko} -o ${VENDOR_DLKM_TMP_IMAGE}/tmp/${ko}
            cp ${VENDOR_DLKM_TMP_IMAGE}/tmp/${ko} ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm/lib/modules/
            echo  ${ko} >> ${VENDOR_DLKM_TMP_IMAGE}/tmp/modules.load
        fi
    done
    python3 ${check_module_tool} \
            ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm/lib/modules/modules.load \
            ${VENDOR_DLKM_TMP_IMAGE}/tmp/modules.load
}
modules_update_check() {
    echo "modules_update_check update"
    find ${DIST_INSTALL}/ -name "*.ko" -printf "%f\n" | sort -bfd| uniq  > ${DIST_INSTALL}/local_origin_list_ko_sort.txt
    cat ${VENDOR_BOOT_TMP_IMAGE}/origin_list_ko_sort.txt  \
        ${VENDOR_DLKM_TMP_IMAGE}/origin_list_ko_sort.txt \
        | sort -bfd| uniq  > ${DIST_INSTALL}/server_origin_list_ko_sort.txt
}

rebuild_vendor_dlkm_config() {
   sed  '/vendor_dlkm_selinux_fc*/d;/block_list*/d' ${ORIGIN_IMAGE}/vendor_dlkm_image_info.txt > ${TOOLS}/local_vendor_dlkm_image_info.txt
   echo vendor_dlkm_selinux_fc=${ORIGIN_IMAGE}/file_contexts.bin >> ${TOOLS}/local_vendor_dlkm_image_info.txt
   echo block_list=${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.map >> ${TOOLS}/local_vendor_dlkm_image_info.txt
}

rebuild_vendor_dlkm_erofs_image() {
    echo "rebuild vendor_dlkm.img"
    rm -rf ${VENDOR_DLKM_TMP_IMAGE}/*
    mkdir -p ${VENDOR_DLKM_TMP_IMAGE}
    ${SIMG2IMG} ${ORIGIN_IMAGE}/vendor_dlkm.img  ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img
    ${TOOLS}/erofs_unpack.sh ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img  ${VENDOR_DLKM_TMP_IMAGE}/mnt ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm
    vendor_dlkm_modules_update
    touch ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm/lib/modules/readme.txt
    echo $(date +"%Y_%m_%d_%H_%M_%S") >${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm/lib/modules/readme.txt
    rebuild_vendor_dlkm_config
    PATH=${BIN}/:$PATH \
        ${TOOLS}/bin/build_image \
        ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm \
        ${TOOLS}/local_vendor_dlkm_image_info.txt \
        ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img \
        ${VENDOR_DLKM_TMP_IMAGE}/
    cp ${VENDOR_DLKM_TMP_IMAGE}/vendor_dlkm.img ${IMAGE_OUT}/vendor_dlkm.img
    sign_vendor_dlkm_image
}

rebuild_dtb_image() {
    echo "rebuild dtb.img"
    mkdir -p ${DT_TMP_IMAGE}
    cp ${DT_DIR}/*.dtb ${DT_TMP_IMAGE}
    ${MKDTIMG} create  ${DT_TMP_IMAGE}/dtb.img  ${DT_TMP_IMAGE}/*.dtb
}

rebuild_dtbo_image() {
    echo "rebuild dtbo.img"
    mkdir -p ${DT_TMP_IMAGE}
    cp ${DT_DIR}/*.dtbo ${DT_TMP_IMAGE}
    ${MKDTOIMG} create ${IMAGE_OUT}/dtbo.img  --page_size=4096 ${DT_TMP_IMAGE}/*.dtbo
    sign_dtbo_image
}


init_image_environment $1 $2
is_intranet
if [[ "$IS_INTRANET" == "no" ]]; then
    cp ${TOOLS}/testkey_rsa4096.pem ${ORIGIN_IMAGE}/
    cp ${TOOLS}/testkey_rsa2048.pem ${ORIGIN_IMAGE}/
    cp ${TOOLS}/testkey.avbpubkey ${ORIGIN_IMAGE}/
    if [[ -z "$(find ${ORIGIN_IMAGE} -name *.img)" ]];then
        echo "please copy stock image to ${ORIGIN_IMAGE}"
        exit 1
    fi
fi
#download_prebuild_image
get_image_info
rebuild_boot_image
rebuild_dtbo_image
rebuild_vendor_boot_image
#rebuild_vendor_dlkm_image
