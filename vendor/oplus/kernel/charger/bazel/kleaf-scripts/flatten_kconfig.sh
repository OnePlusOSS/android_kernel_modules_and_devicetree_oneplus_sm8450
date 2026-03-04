#SPDX-License-Identifier: GPL-2.0-only
#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

#!/usr/bin/env bash

set -e

while IFS= read -r line ; do
	if [[ "$line" =~ source\ \"(.*)\" ]] ; then
		f="${BASH_REMATCH[1]/(/{}"
		f="${f/)/\}}"
		# shellcheck disable=SC2086
		"$0" "$(eval echo "${f}")"
		echo ${OUT_DIR}
	else
		printf "%s\n" "$line"
	fi
done < $1
