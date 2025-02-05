/////////////////////////////////////////////////////////////////////////////
// File Name	: BU24721_FW_H
// Function		: Header file

//
// Copyright(c)	Rohm Co.,Ltd. All rights reserved
//
/***** ROHM Confidential ***************************************************/
#ifndef BU24721_FW_H
#define BU24721_FW_H

#define	FLASH_SLVADR	0x38 //0x50-->0x   //0x38
#define	SLV_OIS_24721	0x3E

// The following definition depend on F/W revision
#define FM_acc_mode		0xF000
#define FM_wri_pro		0xFF0F
#define FM_era			0x8000

#define FM_status		0xFFFF


#define	SUM_C	0x002983C1
#define	SUM_D	0x0001FA0D
#define	LEN_1	0x1000
#define	LEN_2	0x0000
#define	LEN_3	0x0000
#define	LEN_4	0x0000
#define	LEN_5	0x7C00
#define	LEN_6	0x0E00
#define	SUMSEL	0x0
#define	AB_TYPE	0x2
#define	D_COPY	0x0

#define	FLASH_DATA_BIN_1_LEN	0x2800
#define	FLASH_DATA_BIN_5_LEN	0x7C00
#define	FLASH_DATA_BIN_6_LEN	0x0E00
#define	FLASH_DATA_BIN_7_LEN	0x4C00

#define	FLASH_DATA_BIN_1_START	0x0000
#define	FLASH_DATA_BIN_5_START	0x0A00
#define	FLASH_DATA_BIN_6_START	0x2900
#define	FLASH_DATA_BIN_7_START	0x2C80

//
#define FLASH_DATA_CAL_START	0x3F70
#define Pro_ID_0				0x1A890251
#define Poling_times			500
// ==>HF 2020/9/14

#define FW_ID					0xF01C
#define OIS_status				0xF024
#define OIS_control				0xF020
#define OIS_ctl					0xF17C
#define OIS_mode				0xF021
#define OIS_gyro_mode			0xF023
#define OIS_pantilt				0xF18E
#define OIS_reset_1				0xF097
#define OIS_reset_2				0xF058
#define OIS_SPI					0xF02A
#define OIS_release_Standby		0xF050
#define OIS_angle_limit			0xF025
#define OIS_boot_mode			0xF0C0
#define Dat_len					0x1000
//OIS mode
#define Servo_on				0x01
#define OIS_on					0x02
#define OIS_standby				0x00
#define OIS_cal					0x03
#define OIS_still				0x63
#define OIS_View				0x79
#define OIS_movie				0x61
#define OIS_exp					0x03
#define OIS_zero				0x6B
//OIS cross talk & linearity compensation
#define OIS_ctl_on				0x01
#define OIS_ctl_off				0x00

// gyro mode
#define OIS_gyro_on				0x00
#define OIS_gyro_con			0x02
#define OIS_gyro_mon1			0x40
#define OIS_gyro_mon2			0x50
#define OIS_gyro_mas1			0x00
#define OIS_gyro_mas2			0x10
#define OIS_gyro_sla1			0x20
#define OIS_gyro_sla2			0x30

//Flash ctrl
#define Fla_strl				0xF007

//gyro gain
#define Gyro_gain_x				0xF07A
#define Gyro_gain_y				0xF07C

//gyro offset
#define Gyro_offset_lat			0xF088
#define Gyro_offset_val			0xF08A
#define Gyro_offset_req			0xF09C
#define Gyro_offset_diff		0xF09D

//SPI MODE
#define SPI_Master				0x00
#define SPI_Monitor				0x04

//Circle test
#define CIR_thr					0xF1A0
#define CIR_amp_x				0xF1A4
#define CIR_amp_y				0xF1A5
#define CIR_sta					0xF1A7
#define CIR_stu					0xF1A8
#define CIR_err_x				0xF182
#define CIR_err_y				0xF186

//ADC
#define ADC_seq					0xF060
#define ADC_val					0xF062
#define ADC_x					0x00
#define ADC_y					0x01
//manual position
#define Reg_x_pos				0xF1DE
#define Reg_y_pos				0xF1E0
#define Manu_en					0xF1DC
// Start address of Gyro Calibration data on FLASH
#define	FLASH_ADDR_GYRO_CALIB	(0xF800 >> 2)

//After DL FW,need reset
#define PRE_SOFTWARE_RESET 0xF097
#define SOFTWARE_RESET     0xF058



typedef		char						OIS_BYTE;
typedef		short int					OIS_WORD;
typedef		int							OIS_LONG;
typedef		unsigned char				OIS_UBYTE;
typedef		unsigned short int			OIS_UWORD;
typedef		unsigned  int				OIS_ULONG;

typedef		volatile char				OIS_vBYTE;
typedef		volatile short int			OIS_vWORD;
typedef		volatile  int				OIS_vLONG;
typedef		volatile unsigned char		OIS_vUBYTE;
typedef		volatile unsigned short int	OIS_vUWORD;
typedef		volatile unsigned  int		OIS_vULONG;


struct _FACT_ADJ{
	OIS_UWORD	gl_GX_OFS;
	OIS_UWORD	gl_GY_OFS;
};

//#define 	Wait(a)     Wait_usec(a*1000UL)


//void		POWER_UP_AND_PS_DISABLE( void );
//void		POWER_DOWN_AND_PS_ENABLE( void );

OIS_UBYTE   F024_Polling(void);
int			bu24_write_flash_fw_data(void);
OIS_UWORD 	cnv_fm_addr(OIS_UWORD u16_dat1);
void		Erase_flash(void);
void 		Prog_flash(void);
int			bu24_write_flash_fw_data(void);
void 		OIS_soft_reset(OIS_ULONG Prog_ID);
void 		Gyro_gain_set(float X_gain, float Y_gain);
struct 		_FACT_ADJ Gyro_offset_cal(void);
void 		Update_GyroCalib_to_flash(OIS_UWORD gyro_X_offset, OIS_UWORD gyro_Y_offset, OIS_UWORD gyro_X_gain, OIS_UWORD gyro_Y_gain);

void		Update_Gyro_offset_gain_cal_from_flash(void);
void 		OIS_mode_set(OIS_UWORD mode);
void 		OIS_manual_set(OIS_UWORD X_pos, OIS_UWORD Y_pos);
void 		Circle_test(void);
void 		OIS_mag_check(OIS_UWORD *srv_x, OIS_UWORD *srv_y);
void		OIS_cal_check(void);
void        Boot_err_sla_change (void);

int         Rohm_bu24721_fw_download(void);


//int			debug_print(const char * format,...)(const char *format, ...);

#endif  // BU24721_FW_H
