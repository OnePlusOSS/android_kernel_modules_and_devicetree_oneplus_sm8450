#!/bin/bash

set -e

# Save environment for mixed build support.
OLD_ENVIRONMENT=$(mktemp)
export -p > ${OLD_ENVIRONMENT}

# rel_path <to> <from>
# Generate relative directory path to reach directory <to> from <from>
function rel_path() {
  local to=$1
  local from=$2
  local path=
  local stem=
  local prevstem=
  [ -n "$to" ] || return 1
  [ -n "$from" ] || return 1
  to=$(readlink -e "$to")
  from=$(readlink -e "$from")
  [ -n "$to" ] || return 1
  [ -n "$from" ] || return 1
  stem=${from}/
  while [ "${to#$stem}" == "${to}" -a "${stem}" != "${prevstem}" ]; do
    prevstem=$stem
    stem=$(readlink -e "${stem}/..")
    [ "${stem%/}" == "${stem}" ] && stem=${stem}/
    path=${path}../
  done
  echo ${path}${to#$stem}
}

# $1 directory of kernel modules ($1/lib/modules/x.y)
# $2 flags to pass to depmod
function run_depmod() {
  (
    local ramdisk_dir=$1
    local DEPMOD_OUTPUT

    cd ${ramdisk_dir}
    if ! DEPMOD_OUTPUT="$(depmod $2 -F ${DIST_DIR}/System.map -b . 0.0 2>&1)"; then
      echo "$DEPMOD_OUTPUT" >&2
      exit 1
    fi
    echo "$DEPMOD_OUTPUT"
    if { echo "$DEPMOD_OUTPUT" | grep -q "needs unknown symbol"; }; then
      echo "ERROR: kernel module(s) need unknown symbol(s)" >&2
      exit 1
    fi
  )
}

# $1 MODULES_LIST, <File contains the list of modules that should go in the ramdisk>
# $2 MODULES_STAGING_DIR    <The directory to look for all the compiled modules>
# $3 IMAGE_STAGING_DIR  <The destination directory in which MODULES_LIST is
#                        expected, and it's corresponding modules.* files>
# $4 MODULES_BLOCKLIST, <File contains the list of modules to prevent from loading>
# $5 flags to pass to depmod
function create_modules_staging() {
  local modules_list_file=$1
  local src_dir=$2/lib/modules/*
  # Depmod requires a version number; use 0.0 instead of determining the
  # actual kernel version since it is not necessary and will be removed for
  # the final initramfs image.
  local dest_dir=$3/lib/modules/0.0
  local dest_stage=$3
  local modules_blocklist_file=$4
  local depmod_flags=$5

  rm -rf ${dest_dir}
  mkdir -p ${dest_dir}/kernel
  find ${src_dir}/kernel/ -maxdepth 1 -mindepth 1 \
    -exec cp -r {} ${dest_dir}/kernel/ \;
  # The other modules.* files will be generated by depmod
  cp ${src_dir}/modules.order ${dest_dir}/modules.order
  cp ${src_dir}/modules.builtin ${dest_dir}/modules.builtin

  if [ -n "${EXT_MODULES}" ]; then
    mkdir -p ${dest_dir}/extra/
    cp -r ${src_dir}/extra/* ${dest_dir}/extra/
    (cd ${dest_dir}/ && \
      find extra -type f -name "*.ko" | sort >> modules.order)
  fi

  if [ -n "${DO_NOT_STRIP_MODULES}" ]; then
    # strip debug symbols off initramfs modules
    find ${dest_dir} -type f -name "*.ko" \
      -exec ${OBJCOPY:-${CROSS_COMPILE}objcopy} --strip-debug {} \;
  fi

  if [ -n "${modules_list_file}" ]; then
    echo "========================================================"
    echo " Reducing modules.order to:"
    # Need to make sure we can find modules_list_file from the staging dir
    if [[ -f "${ROOT_DIR}/${modules_list_file}" ]]; then
      modules_list_file="${ROOT_DIR}/${modules_list_file}"
    elif [[ "${modules_list_file}" != /* ]]; then
      echo "modules list must be an absolute path or relative to ${ROOT_DIR}: ${modules_list_file}"
      exit 1
    elif [[ ! -f "${modules_list_file}" ]]; then
      echo "Failed to find modules list: ${modules_list_file}"
      exit 1
    fi

    local modules_list_filter=$(mktemp)
    local old_modules_list=$(mktemp)

    # Remove all lines starting with "#" (comments)
    # Exclamation point makes interpreter ignore the exit code under set -e
    ! grep -v "^\#" ${modules_list_file} > ${modules_list_filter}

    # grep the modules.order for any KOs in the modules list
    cp ${dest_dir}/modules.order ${old_modules_list}
    ! grep -w -f ${modules_list_filter} ${old_modules_list} > ${dest_dir}/modules.order
    rm -f ${modules_list_filter} ${old_modules_list}
    cat ${dest_dir}/modules.order | sed -e "s/^/  /"
  fi

  if [ -n "${modules_blocklist_file}" ]; then
    # Need to make sure we can find modules_blocklist_file from the staging dir
    if [[ -f "${ROOT_DIR}/${modules_blocklist_file}" ]]; then
      modules_blocklist_file="${ROOT_DIR}/${modules_blocklist_file}"
    elif [[ "${modules_blocklist_file}" != /* ]]; then
      echo "modules blocklist must be an absolute path or relative to ${ROOT_DIR}: ${modules_blocklist_file}"
      exit 1
    elif [[ ! -f "${modules_blocklist_file}" ]]; then
      echo "Failed to find modules blocklist: ${modules_blocklist_file}"
      exit 1
    fi

    cp ${modules_blocklist_file} ${dest_dir}/modules.blocklist
  fi

  if [ -n "${TRIM_UNUSED_MODULES}" ]; then
    echo "========================================================"
    echo " Trimming unused modules"
    local used_blocklist_modules=$(mktemp)
    local blocklist_flag=
    if [ -f ${dest_dir}/modules.blocklist ]; then
      # TODO: the modules blocklist could contain module aliases instead of the filename
      sed -n -E -e 's/blocklist (.+)/\1/p' ${dest_dir}/modules.blocklist > $used_blocklist_modules
      blocklist_flag="-f $used_blocklist_modules"
    fi

    # Trim modules from tree that aren't mentioned in modules.order
    (
      cd ${dest_dir}
      find * -type f -name "*.ko" | grep -v -w -f modules.order $blocklist_flag - | xargs -r rm
    )
    rm $used_blocklist_modules
  fi

  # Re-run depmod to detect any dependencies between in-kernel and external
  # modules. Then, create modules.order based on all the modules compiled.
  run_depmod ${dest_stage} "${depmod_flags}"
  cp ${dest_dir}/modules.order ${dest_dir}/modules.load

  mv ${dest_stage}/lib/modules/0.0/* ${dest_stage}/lib/modules/.
  rmdir ${dest_stage}/lib/modules/0.0
}

function build_vendor_dlkm() {
  echo "========================================================"
  echo " Creating vendor_dlkm image"

  create_modules_staging "${VENDOR_DLKM_MODULES_LIST}" "${MODULES_STAGING_DIR}" \
    "${VENDOR_DLKM_STAGING_DIR}" "${VENDOR_DLKM_MODULES_BLOCKLIST}"

  cp ${VENDOR_DLKM_STAGING_DIR}/lib/modules/modules.load ${DIST_DIR}/vendor_dlkm.modules.load
  if [ -e ${VENDOR_DLKM_STAGING_DIR}/lib/modules/modules.blocklist ]; then
    cp ${VENDOR_DLKM_STAGING_DIR}/lib/modules/modules.blocklist \
      ${DIST_DIR}/vendor_dlkm.modules.blocklist
  fi

  local vendor_dlkm_props_file

  if [ -z "${VENDOR_DLKM_PROPS}" ]; then
    vendor_dlkm_props_file="$(mktemp)"
    echo -e "vendor_dlkm_fs_type=ext4\n" >> ${vendor_dlkm_props_file}
    echo -e "use_dynamic_partition_size=true\n" >> ${vendor_dlkm_props_file}
    echo -e "ext_mkuserimg=mkuserimg_mke2fs\n" >> ${vendor_dlkm_props_file}
    echo -e "ext4_share_dup_blocks=true\n" >> ${vendor_dlkm_props_file}
  else
    vendor_dlkm_props_file="${VENDOR_DLKM_PROPS}"
    if [[ -f "${ROOT_DIR}/${vendor_dlkm_props_file}" ]]; then
      vendor_dlkm_props_file="${ROOT_DIR}/${vendor_dlkm_props_file}"
    elif [[ "${vendor_dlkm_props_file}" != /* ]]; then
      echo "VENDOR_DLKM_PROPS must be an absolute path or relative to ${ROOT_DIR}: ${vendor_dlkm_props_file}"
      exit 1
    elif [[ ! -f "${vendor_dlkm_props_file}" ]]; then
      echo "Failed to find VENDOR_DLKM_PROPS: ${vendor_dlkm_props_file}"
      exit 1
    fi
  fi
  build_image "${VENDOR_DLKM_STAGING_DIR}" "${vendor_dlkm_props_file}" \
    "${DIST_DIR}/vendor_dlkm.img" /dev/null
}

function build_super() {
  echo "========================================================"
  echo " Creating super.img"

  local super_props_file=$(mktemp)
  local dynamic_partitions=""
  # Default to 256 MB
  local super_image_size="$((${SUPER_IMAGE_SIZE:-268435456}))"
  local group_size="$((${super_image_size} - 0x400000))"
  echo -e "lpmake=lpmake" >> ${super_props_file}
  echo -e "super_metadata_device=super" >> ${super_props_file}
  echo -e "super_block_devices=super" >> ${super_props_file}
  echo -e "super_super_device_size=${super_image_size}" >> ${super_props_file}
  echo -e "super_partition_size=${super_image_size}" >> ${super_props_file}
  echo -e "super_partition_groups=kb_dynamic_partitions" >> ${super_props_file}
  echo -e "super_kb_dynamic_partitions_group_size=${group_size}" >> ${super_props_file}

  for image in "${SUPER_IMAGE_CONTENTS}"; do
    echo "  Adding ${image}"
    partition_name=$(basename -s .img "${image}")
    dynamic_partitions="${dynamic_partitions} ${partition_name}"
    echo -e "${partition_name}_image=${image}" >> ${super_props_file}
  done

  echo -e "dynamic_partition_list=${dynamic_partitions}" >> ${super_props_file}
  echo -e "super_kb_dynamic_partitions_partition_list=${dynamic_partitions}" >> ${super_props_file}
  build_super_image -v ${super_props_file} ${DIST_DIR}/super.img
  rm ${super_props_file}

  echo "super image created at ${DIST_DIR}/super.img"
}

export ROOT_DIR=$(readlink -f $(dirname $0)/../..)

# For module file Signing with the kernel (if needed)
FILE_SIGN_BIN=scripts/sign-file
SIGN_SEC=certs/signing_key.pem
SIGN_CERT=certs/signing_key.x509
SIGN_ALGO=sha512

# Save environment parameters before being overwritten by sourcing
# BUILD_CONFIG.
CC_ARG="${CC}"

source "${ROOT_DIR}/build/_setup_env.sh"

if [ -n "${GKI_BUILD_CONFIG}" ]; then
  GKI_OUT_DIR=${GKI_OUT_DIR:-${COMMON_OUT_DIR}/gki_kernel}
  GKI_DIST_DIR=${GKI_DIST_DIR:-${GKI_OUT_DIR}/dist}

  # Inherit SKIP_MRPROPER unless overridden by GKI_SKIP_MRPROPER
  GKI_ENVIRON="SKIP_MRPROPER=${SKIP_MRPROPER}"
  # Explicitly unset GKI_BUILD_CONFIG in case it was set by in the old environment
  # e.g. GKI_BUILD_CONFIG=common/build.config.gki.x86 ./build/build.sh would cause
  # gki build recursively
  GKI_ENVIRON+=" GKI_BUILD_CONFIG="
  # Any variables prefixed with GKI_ get set without that prefix in the GKI build environment
  # e.g. GKI_BUILD_CONFIG=common/build.config.gki.aarch64 -> BUILD_CONFIG=common/build.config.gki.aarch64
  GKI_ENVIRON+=" $(export -p | sed -n -E -e 's/.*GKI_([^=]+=.*)$/\1/p' | tr '\n' ' ')"
  GKI_ENVIRON+=" OUT_DIR=${GKI_OUT_DIR}"
  GKI_ENVIRON+=" DIST_DIR=${GKI_DIST_DIR}"
  ( env -i bash -c "source ${OLD_ENVIRONMENT}; rm -f ${OLD_ENVIRONMENT}; export ${GKI_ENVIRON}; ./build/build.sh" )
else
  rm -f ${OLD_ENVIRONMENT}
fi

export MAKE_ARGS=$*
export MAKEFLAGS="-j$(nproc) ${MAKEFLAGS}"
export MODULES_STAGING_DIR=$(readlink -m ${COMMON_OUT_DIR}/staging)
export MODULES_PRIVATE_DIR=$(readlink -m ${COMMON_OUT_DIR}/private)
export KERNEL_UAPI_HEADERS_DIR=$(readlink -m ${COMMON_OUT_DIR}/kernel_uapi_headers)
export INITRAMFS_STAGING_DIR=${MODULES_STAGING_DIR}/initramfs_staging
export VENDOR_DLKM_STAGING_DIR=${MODULES_STAGING_DIR}/vendor_dlkm_staging

BOOT_IMAGE_HEADER_VERSION=${BOOT_IMAGE_HEADER_VERSION:-3}

cd ${ROOT_DIR}

export CLANG_TRIPLE CROSS_COMPILE CROSS_COMPILE_COMPAT CROSS_COMPILE_ARM32 ARCH SUBARCH MAKE_GOALS

# Restore the previously saved CC argument that might have been overridden by
# the BUILD_CONFIG.
[ -n "${CC_ARG}" ] && CC="${CC_ARG}"

# CC=gcc is effectively a fallback to the default gcc including any target
# triplets. An absolute path (e.g., CC=/usr/bin/gcc) must be specified to use a
# custom compiler.
[ "${CC}" == "gcc" ] && unset CC && unset CC_ARG

TOOL_ARGS=()

# LLVM=1 implies what is otherwise set below; it is a more concise way of
# specifying CC=clang LD=ld.lld NM=llvm-nm OBJCOPY=llvm-objcopy <etc>, for
# newer kernel versions.
if [[ -n "${LLVM}" ]]; then
  TOOL_ARGS+=("LLVM=1")
  # Reset a bunch of variables that the kernel's top level Makefile does, just
  # in case someone tries to use these binaries in this script such as in
  # initramfs generation below.
  HOSTCC=clang
  HOSTCXX=clang++
  CC=clang
  LD=ld.lld
  AR=llvm-ar
  NM=llvm-nm
  OBJCOPY=llvm-objcopy
  OBJDUMP=llvm-objdump
  READELF=llvm-readelf
  OBJSIZE=llvm-size
  STRIP=llvm-strip
else
  if [ -n "${HOSTCC}" ]; then
    TOOL_ARGS+=("HOSTCC=${HOSTCC}")
  fi

  if [ -n "${CC}" ]; then
    TOOL_ARGS+=("CC=${CC}")
    if [ -z "${HOSTCC}" ]; then
      TOOL_ARGS+=("HOSTCC=${CC}")
    fi
  fi

  if [ -n "${LD}" ]; then
    TOOL_ARGS+=("LD=${LD}" "HOSTLD=${LD}")
    custom_ld=${LD##*.}
    if [ -n "${custom_ld}" ]; then
      TOOL_ARGS+=("HOSTLDFLAGS=-fuse-ld=${custom_ld}")
    fi
  fi

  if [ -n "${NM}" ]; then
    TOOL_ARGS+=("NM=${NM}")
  fi

  if [ -n "${OBJCOPY}" ]; then
    TOOL_ARGS+=("OBJCOPY=${OBJCOPY}")
  fi
fi

if [ -n "${LLVM_IAS}" ]; then
  TOOL_ARGS+=("LLVM_IAS=${LLVM_IAS}")
  # Reset $AS for the same reason that we reset $CC etc above.
  AS=clang
fi

if [ -n "${DEPMOD}" ]; then
  TOOL_ARGS+=("DEPMOD=${DEPMOD}")
fi

if [ -n "${DTC}" ]; then
  TOOL_ARGS+=("DTC=${DTC}")
fi

# Allow hooks that refer to $CC_LD_ARG to keep working until they can be
# updated.
CC_LD_ARG="${TOOL_ARGS[@]}"

DECOMPRESS_GZIP="gzip -c -d"
DECOMPRESS_LZ4="lz4 -c -d -l"
if [ -z "${LZ4_RAMDISK}" ] ; then
  RAMDISK_COMPRESS="gzip -c -f"
  RAMDISK_DECOMPRESS="${DECOMPRESS_GZIP}"
  RAMDISK_EXT="gz"
else
  RAMDISK_COMPRESS="lz4 -c -l -12 --favor-decSpeed"
  RAMDISK_DECOMPRESS="${DECOMPRESS_LZ4}"
  RAMDISK_EXT="lz4"
fi


echo "========================================================"
echo " Building external oplus modules"
echo " current OPLUS_KO_PATH=${OPLUS_KO_PATH}"
echo " if you didn't set it you need set again \"export OPLUS_KO_PATH=drivers/input/touchscreen/focaltech_touch\""
set -x
pwd
make -C ${OUT_DIR} M=${ROOT_DIR}/common/${OPLUS_KO_PATH} O=${OUT_DIR}/ "${TOOL_ARGS[@]}" clean
make -C ${OUT_DIR} M=${ROOT_DIR}/common/${OPLUS_KO_PATH} O=${OUT_DIR}/ "${TOOL_ARGS[@]}" modules
set +x
echo " current export OPLUS_KO_PATH=${OPLUS_KO_PATH}"