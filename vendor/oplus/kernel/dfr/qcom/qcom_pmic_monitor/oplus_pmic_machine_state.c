/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
/*==============================================================================

FILE:      oplus_pmic_machine_state.c

DESCRIPTION:
sm8450 XBL-Loader drivers are being integrated into the XBL-SC image
OEMs will not be able to access and modify the source files that were loaded by.
We read pmic machine state from nvmem

=============================================================================*/

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/printk.h>

#include "oplus_pmic_info.h"

struct PMICGen3HistoryKernelStruct PmicGen3RecordNVMEM;
static int nvmem_pmic_info_flag = 0;

/* SDAM NVMEM register offsets: */
#define REG_PUSH_PTR		0x46
#define REG_FIFO_DATA_START	0x4B
#define REG_FIFO_DATA_END	0xBF

/* PMIC PON LOG binary format in the FIFO: */
struct pmic_pon_log_entry {
	u8	state;
	u8	event;
	u8	data1;
	u8	data0;
};

#define FIFO_SIZE		(REG_FIFO_DATA_END - REG_FIFO_DATA_START + 1)
#define FIFO_ENTRY_SIZE		(sizeof(struct pmic_pon_log_entry))
#define FIFO_MAX_ENTRY_COUNT	(FIFO_SIZE / FIFO_ENTRY_SIZE)

#define IPC_LOG_PAGES	3

struct pmic_pon_log_dev {
	struct pmic_pon_log_entry	log[FIFO_MAX_ENTRY_COUNT];
	int				log_len;
	struct nvmem_device		*nvmem;
};

enum pmic_pon_state {
	PMIC_PON_STATE_FAULT0		= 0x0,
	PMIC_PON_STATE_PON		= 0x1,
	PMIC_PON_STATE_POFF		= 0x2,
	PMIC_PON_STATE_ON		= 0x3,
	PMIC_PON_STATE_RESET		= 0x4,
	PMIC_PON_STATE_OFF		= 0x5,
	PMIC_PON_STATE_FAULT6		= 0x6,
	PMIC_PON_STATE_WARM_RESET	= 0x7,
};

enum pmic_pon_event {
	PMIC_PON_EVENT_PON_TRIGGER_RECEIVED	= 0x01,
	PMIC_PON_EVENT_OTP_COPY_COMPLETE	= 0x02,
	PMIC_PON_EVENT_TRIM_COMPLETE		= 0x03,
	PMIC_PON_EVENT_XVLO_CHECK_COMPLETE	= 0x04,
	PMIC_PON_EVENT_PMIC_CHECK_COMPLETE	= 0x05,
	PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED	= 0x06,
	PMIC_PON_EVENT_RESET_TYPE		= 0x07,
	PMIC_PON_EVENT_WARM_RESET_COUNT		= 0x08,
	PMIC_PON_EVENT_FAULT_REASON_1_2		= 0x09,
	PMIC_PON_EVENT_FAULT_REASON_3		= 0x0A,
	PMIC_PON_EVENT_PBS_PC_DURING_FAULT	= 0x0B,
	PMIC_PON_EVENT_FUNDAMENTAL_RESET	= 0x0C,
	PMIC_PON_EVENT_PON_SEQ_START		= 0x0D,
	PMIC_PON_EVENT_PON_SUCCESS		= 0x0E,
	PMIC_PON_EVENT_WAITING_ON_PSHOLD	= 0x0F,
	PMIC_PON_EVENT_PMIC_SID1_FAULT		= 0x10,
	PMIC_PON_EVENT_PMIC_SID2_FAULT		= 0x11,
	PMIC_PON_EVENT_PMIC_SID3_FAULT		= 0x12,
	PMIC_PON_EVENT_PMIC_SID4_FAULT		= 0x13,
	PMIC_PON_EVENT_PMIC_SID5_FAULT		= 0x14,
	PMIC_PON_EVENT_PMIC_SID6_FAULT		= 0x15,
	PMIC_PON_EVENT_PMIC_SID7_FAULT		= 0x16,
	PMIC_PON_EVENT_PMIC_SID8_FAULT		= 0x17,
	PMIC_PON_EVENT_PMIC_SID9_FAULT		= 0x18,
	PMIC_PON_EVENT_PMIC_SID10_FAULT		= 0x19,
	PMIC_PON_EVENT_PMIC_SID11_FAULT		= 0x1A,
	PMIC_PON_EVENT_PMIC_SID12_FAULT		= 0x1B,
	PMIC_PON_EVENT_PMIC_SID13_FAULT		= 0x1C,
	PMIC_PON_EVENT_PMIC_VREG_READY_CHECK	= 0x20,
};

int use_nvmem_pmic_info(void) {
	return !!nvmem_pmic_info_flag;
}

static int pmic_pon_log_read_entry(struct nvmem_device *nvmem,
		u16 entry_start_addr, struct pmic_pon_log_entry *entry)
{
	u8 *buf = (u8 *)entry;
	int ret, len;

	if (entry_start_addr < REG_FIFO_DATA_START ||
	    entry_start_addr > REG_FIFO_DATA_END)
		return -EINVAL;

	if (entry_start_addr + FIFO_ENTRY_SIZE - 1 > REG_FIFO_DATA_END) {
		/* The entry wraps around the end of the FIFO. */
		len = REG_FIFO_DATA_END - entry_start_addr + 1;
		ret = nvmem_device_read(nvmem, entry_start_addr, len, buf);
		if (ret < 0)
			return ret;
		ret = nvmem_device_read(nvmem, REG_FIFO_DATA_START,
					FIFO_ENTRY_SIZE - len, &buf[len]);
	} else {
		ret = nvmem_device_read(nvmem, entry_start_addr,
					FIFO_ENTRY_SIZE, buf);
	}

	return ret;
}

void *get_pmic_history_from_nvmem(void) {
	return &PmicGen3RecordNVMEM;
}

static int sort_and_push_machine_state(struct pmic_pon_log_dev *pon_dev) {
	int pmic_record_count = 0, machine_state_ptr = 0;
	int i, warm_reset_count = 0;
	struct pmic_pon_log_entry cur_machine_state;
	struct PMICGen3RecordKernelStruct PmicGen3Record;

	memset(&PmicGen3Record, 0, sizeof(struct PMICGen3RecordKernelStruct));

	for (i = 1 ; i < pon_dev->log_len; i++) {
		cur_machine_state = pon_dev->log[(pon_dev->log_len-1)- i];
		if (machine_state_ptr < MAX_STATE_RECORDS) {
			PmicGen3Record.pmic_state_machine_log[machine_state_ptr].state = cur_machine_state.state;
			PmicGen3Record.pmic_state_machine_log[machine_state_ptr].event = cur_machine_state.event;
			PmicGen3Record.pmic_state_machine_log[machine_state_ptr].data1 = cur_machine_state.data1;
			PmicGen3Record.pmic_state_machine_log[machine_state_ptr].data0 = cur_machine_state.data0;
			machine_state_ptr++;
			/* printk debug info
			printk("%d:pmic_state_machine_log[%d] state=0x%02X event=0x%02X data1=0x%02X data0=0x%02X\n",
						pmic_record_count,
						machine_state_ptr,
						cur_machine_state.state,
						cur_machine_state.event,
						cur_machine_state.data1,
						cur_machine_state.data0);
			*/
		}

		switch(cur_machine_state.event) {
		case PMIC_PON_EVENT_PON_SUCCESS:
				if ((0 == warm_reset_count || 1 == warm_reset_count) && (pmic_record_count < MAX_HISTORY_COUNT)) {
					PmicGen3RecordNVMEM.pmic_record[pmic_record_count] = PmicGen3Record;
					/* strncpy with a source string whose length (8 chars) is equal to the size argument (8) will fail to null-terminate PmicGen3RecordNVMEM.pmic_magic. */
					/* strncpy(PmicGen3RecordNVMEM.pmic_magic, PMIC_GEN3_NVMEM_MAGIC_STR, 8); VMEMPMIC*/
					PmicGen3RecordNVMEM.pmic_magic[0] = 'V';
					PmicGen3RecordNVMEM.pmic_magic[1] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[2] = 'E';
					PmicGen3RecordNVMEM.pmic_magic[3] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[4] = 'P';
					PmicGen3RecordNVMEM.pmic_magic[5] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[6] = 'I';
					PmicGen3RecordNVMEM.pmic_magic[7] = 'C';
					PmicGen3RecordNVMEM.log_count++;
					pmic_record_count++;
					machine_state_ptr = 0;
					memset(&PmicGen3Record, 0, sizeof(struct PMICGen3RecordKernelStruct));
				}
			break;

		case PMIC_PON_EVENT_FUNDAMENTAL_RESET:
				/* fundamental,push all machine state */
				if (pmic_record_count < MAX_HISTORY_COUNT) {
					PmicGen3RecordNVMEM.pmic_record[pmic_record_count] = PmicGen3Record;
					/* strncpy with a source string whose length (8 chars) is equal to the size argument (8) will fail to null-terminate PmicGen3RecordNVMEM.pmic_magic. */
					/* strncpy(PmicGen3RecordNVMEM.pmic_magic, PMIC_GEN3_NVMEM_MAGIC_STR, 8); VMEMPMIC*/
					PmicGen3RecordNVMEM.pmic_magic[0] = 'V';
					PmicGen3RecordNVMEM.pmic_magic[1] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[2] = 'E';
					PmicGen3RecordNVMEM.pmic_magic[3] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[4] = 'P';
					PmicGen3RecordNVMEM.pmic_magic[5] = 'M';
					PmicGen3RecordNVMEM.pmic_magic[6] = 'I';
					PmicGen3RecordNVMEM.pmic_magic[7] = 'C';
					PmicGen3RecordNVMEM.log_count++;
					pmic_record_count++;
					machine_state_ptr = 0;
					memset(&PmicGen3Record, 0, sizeof(struct PMICGen3RecordKernelStruct));
				}
			break;

		case PMIC_PON_EVENT_WARM_RESET_COUNT:
			warm_reset_count = 0;
			/* Byte0: warm reset count */
			if(cur_machine_state.data0 != 0) {
				warm_reset_count = cur_machine_state.data0;
			}
			break;

			default:
			break;
		}

		if (pmic_record_count >= MAX_HISTORY_COUNT)
			break;
	}

	return 0;
}

static int pmic_pon_log_parse(struct pmic_pon_log_dev *pon_dev)
{
	int ret, i, addr, addr_start, addr_end;
	struct pmic_pon_log_entry entry;
	u8 buf;

	ret = nvmem_device_read(pon_dev->nvmem, REG_PUSH_PTR, 1, &buf);
	if (ret < 0)
		return ret;
	addr_end = buf;

	/*
	 * Calculate the FIFO start address from the end address assuming that
	 * the FIFO is full.
	 */
	addr_start = addr_end - FIFO_MAX_ENTRY_COUNT * FIFO_ENTRY_SIZE;
	if (addr_start < REG_FIFO_DATA_START)
		addr_start += FIFO_SIZE;

	for (i = 0; i < FIFO_MAX_ENTRY_COUNT; i++) {
		addr = addr_start + i * FIFO_ENTRY_SIZE;
		if (addr > REG_FIFO_DATA_END)
			addr -= FIFO_SIZE;

		ret = pmic_pon_log_read_entry(pon_dev->nvmem, addr, &entry);
		if (ret < 0)
			return ret;

		if (entry.state == 0 && entry.event == 0 && entry.data1 == 0 &&
		    entry.data0 == 0) {
			/*
			 * Ignore all 0 entries which correspond to unused
			 * FIFO space in the case that the FIFO has not wrapped
			 * around.
			 */
			continue;
		}

		pon_dev->log[pon_dev->log_len++] = entry;
	}

	sort_and_push_machine_state(pon_dev);

	return 0;
}

static int pmic_pon_log_probe(struct platform_device *pdev)
{
	struct pmic_pon_log_dev *pon_dev;
	int ret = 0;

	pon_dev = devm_kzalloc(&pdev->dev, sizeof(*pon_dev), GFP_KERNEL);
	if (!pon_dev)
		return -ENOMEM;

	pon_dev->nvmem = devm_nvmem_device_get(&pdev->dev, "pon_log");
	if (IS_ERR(pon_dev->nvmem)) {
		ret = PTR_ERR(pon_dev->nvmem);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get nvmem device, ret=%d\n",
				ret);
		return ret;
	}

	platform_set_drvdata(pdev, pon_dev);

	nvmem_pmic_info_flag = 1;
	ret = pmic_pon_log_parse(pon_dev);
	if (ret < 0)
		dev_err(&pdev->dev, "PMIC PON log parsing failed, ret=%d\n",
			ret);

	return ret;
}

static int pmic_pon_log_remove(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "PMIC monitor remove\n");
	return 0;
}

static const struct of_device_id pmic_pon_log_of_match[] = {
	{ .compatible = "oplus,pmic-monitor-log" },
	{}
};
MODULE_DEVICE_TABLE(of, pmic_pon_log_of_match);

static struct platform_driver pmic_pon_log_driver = {
	.driver = {
		.name = "oplusi-pmic-monitor",
		.of_match_table	= of_match_ptr(pmic_pon_log_of_match),
	},
	.probe = pmic_pon_log_probe,
	.remove = pmic_pon_log_remove,
};

int __init pmic_pon_log_driver_init(void)
{
	return platform_driver_register(&pmic_pon_log_driver);
}

void __exit pmic_pon_log_driver_exit(void)
{
	platform_driver_unregister(&pmic_pon_log_driver);
}

MODULE_DESCRIPTION("OPLUS PMIC monitor driver");
MODULE_LICENSE("GPL v2");
