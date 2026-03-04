/////////////////////////////////////////////////////////////////////////////
// File Name    : bu24721_fw.c
// Function        : Various function for OIS control
//
// Copyright(c)    Rohm Co.,Ltd. All rights reserved
//
/***** ROHM Confidential ***************************************************/
//#define    _USE_MATH_DEFINES                            // 

#ifndef BU24721_FW_C
#define BU24721_FW_C
#endif
#include <linux/types.h>
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "fw_download_interface.h"

#include "bu24721_fw.h"
#include "FLASH_DATA_BIN.h"


struct 		_FACT_ADJ	FADJ_CAL = { 0x0000, 0x0000};
struct 		_FACT_ADJ	FADJ_PL = { 0x0000,0x0000};
//static ois_dev_t *g_rohm24721_dev;



//  *****************************************************
//  **** Change 2'c Hex value to the decimal data
//  **** ------------------------------------------------
//    **** IN
//  ****     OIS_UWORD    inpdat  (16bit)
//    ****        Change below
//  ****             0x8000---0xFFFF,0x00000--- 0x7FFF
//    ****         to
//  ****            -32868---    -1,0      --- +32768
//    **** OUT
//  ****     OIS_WORD    decimal data
//  *****************************************************
OIS_WORD H2D( OIS_UWORD u16_inpdat ){

    OIS_WORD s16_temp;
    s16_temp = u16_inpdat;
    if( u16_inpdat > 32767 ){
        s16_temp = (OIS_WORD)(u16_inpdat - 65536L);
    }

    return s16_temp;
}

//  *****************************************************
//  **** Change the decimal data to the 2'c Hex
//  **** ------------------------------------------------
//    **** IN
//  ****     OIS_WORD    inpdat  (16bit)
//    ****        Change below
//  ****            -32868---    -1,0      --- +32768
//    ****         to
//  ****             0x8000---0xFFFF,0x00000--- 0x7FFF
//    **** OUT
//  ****     OIS_UWORD    2'c data
//  *****************************************************
OIS_UWORD D2H( OIS_WORD s16_inpdat){

    OIS_UWORD u16_temp;

    if( s16_inpdat < 0 ){
        u16_temp = (OIS_UWORD)(s16_inpdat + 65536L);
    }
    else{
        u16_temp = s16_inpdat;
    }
    return u16_temp;
}

OIS_UWORD cnv_fm_addr(OIS_UWORD u16_dat1){
    OIS_UWORD u16_dat2;
    OIS_UWORD u16_dat3;
    u16_dat2 = u16_dat1>>2;
    u16_dat3 = (u16_dat2>>8)+(u16_dat2<<8);
    return u16_dat3;
}

OIS_UBYTE F024_Polling(void){
    OIS_LONG	u16_i;
    OIS_UBYTE	u16_dat;
    for( u16_i = 1; u16_i <= Poling_times; u16_i ++ ){
	u16_dat = I2C_OIS_8bit__read(OIS_status);
	CAM_ERR(CAM_OIS, "polling %d\n",u16_dat);
	if((u16_dat&0x01) == 1) {
	    break;
	}
	Wait(1);
    }
    return u16_dat;
}

void Erase_flash(void){

    OIS_UWORD subaddr_FM_era;
    /* Erase : 0x0000-0x9FFF (40kB) */
    subaddr_FM_era = ((0x0000 >> 2) | 0x8000);  //0x8000
    I2C_FM_8bit_write(subaddr_FM_era, 0xE5);
    Wait(40000);//wait 40ms

    /* Erase : 0xA000-0xA7FF (2kB) */
    subaddr_FM_era = ((0xA000 >> 2) | 0x8000); //0xA800
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xA800-0xAFFF (2kB) */
    subaddr_FM_era = ((0xA800 >> 2) | 0x8000); //0xAA00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xB000-0xB7FF (2kB) */
    subaddr_FM_era = ((0xB000 >> 2) | 0x8000); //0xAC00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xB800-0xBFFF (2kB) */
    subaddr_FM_era = ((0xB800 >> 2) | 0x8000); //0xAE00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xC000-0xC7FF (2kB) */
    subaddr_FM_era = ((0xC000 >> 2) | 0x8000); //0xB000
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xC800-0xCFFF (2kB) */
    subaddr_FM_era = ((0xC800 >> 2) | 0x8000); //0xB200
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xD000-0xD7FF (2kB) */
    subaddr_FM_era = ((0xD000 >> 2) | 0x8000); //0xB400
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xD800-0xDFFF (2kB) */
    subaddr_FM_era = ((0xD800 >> 2) | 0x8000); //0xB600
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xE000-0xE7FF (2kB) */
    subaddr_FM_era = ((0xE000 >> 2) | 0x8000); //0xB800
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xE800-0xEFFF (2kB) */
    subaddr_FM_era = ((0xE800 >> 2) | 0x8000); //0xBA00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

    /* Erase : 0xF000-0xF7FF (2kB) */
    subaddr_FM_era = ((0xF000 >> 2) | 0x8000); //0xBC00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE9);
    Wait(2000);//wait 2ms

/*
	 !!KEEP!! : 0xF800-0xF9FF as Gyro Gain Calibration data area (512B) is NOT erased
*/

    /* Erase : 0xF800-0xF9FF (512B)
    subaddr_FM_era = ((0xF800 >> 2) | 0x8000); //0xBE00
    I2C_FM_8bit_write(subaddr_FM_era, 0xE0);
    Wait(2000);//wait 2ms
	*/

	/* Erase : 0xFA00-0xFBFF (512B) */
    subaddr_FM_era = ((0xFA00 >> 2) | 0x8000); //0xBE80
    I2C_FM_8bit_write(subaddr_FM_era, 0xE0);
    Wait(2000);//wait 2ms

    /*
        !!KEEP!! : 0xFC00-0xFDFF as Calibration data area (512B) is NOT erased
    */

    /* Erase : 0xFE00-0xFFFF (512B) */
    subaddr_FM_era = ((0xFE00 >> 2) | 0x8000); //0xBF80
    I2C_FM_8bit_write(subaddr_FM_era, 0xE0);
    Wait(2000);//wait 2ms
}

void Prog_flash(void){

    OIS_UBYTE out[10];
    OIS_ULONG wdata;
    OIS_UWORD addr;

    // Program FLASH
    addr = cnv_fm_addr(0xFE00);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x00;
    out[5] = 0x15;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    addr = cnv_fm_addr(0xFE04);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    out[2] = 0x42;
    out[3] = 0x44;
    out[4] = 0x58;
    out[5] = 0x1C;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    addr = cnv_fm_addr(0xFE08);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    out[2] = 0xA5;
    out[3] = 0x00;
    out[4] = 0x00;
    out[5] = 0xDB;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFE0C, out);
    addr = cnv_fm_addr(0xFE0C);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x63;
    out[5] = 0xE1;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFE10, out);
    addr = cnv_fm_addr(0xFE10);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    out[2] = 0x03;
    out[3] = 0x03;
    out[4] = 0x00;
    out[5] = 0x00;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFFEC, out);
    addr = cnv_fm_addr(0xFFEC);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    wdata = SUM_D;
    out[2] = (wdata >> 24) & 0xFF;
    out[3] = (wdata >> 16) & 0xFF;
    out[4] = (wdata >>	8) & 0xFF;
    out[5] = (wdata >>	0) & 0xFF;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFFF0, out);
    addr = cnv_fm_addr(0xFFF0);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    wdata = (LEN_6 << 16) | (LEN_5 << 0);
    out[2] = (wdata >> 24) & 0xFF;
    out[3] = (wdata >> 16) & 0xFF;
    out[4] = (wdata >>	8) & 0xFF;
    out[5] = (wdata >>	0) & 0xFF;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFFF4, out);
    addr = cnv_fm_addr(0xFFF4);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    wdata = (LEN_3 << 16) | (LEN_1 << 0);
    out[2] = (wdata >> 24) & 0xFF;
    out[3] = (wdata >> 16) & 0xFF;
    out[4] = (wdata >>	8) & 0xFF;
    out[5] = (wdata >>	0) & 0xFF;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFFF8, out);
    addr = cnv_fm_addr(0xFFF8);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    wdata = (LEN_4 << 16) | (LEN_2 << 0);
    out[2] = (wdata >> 24) & 0xFF;
    out[3] = (wdata >> 16) & 0xFF;
    out[4] = (wdata >>	8) & 0xFF;
    out[5] = (wdata >>	0) & 0xFF;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );

    //cnv_fm_addr(0xFFFC, out);
    addr = cnv_fm_addr(0xFFFC);
    out[0] = addr&0xff;
    out[1] = (addr>>8)&0xff;

    wdata = (SUMSEL << 31) | (AB_TYPE << 29) | (D_COPY << 27) | (SUM_C << 0);
    out[2] = (wdata >> 24) & 0xFF;
    out[3] = (wdata >> 16) & 0xFF;
    out[4] = (wdata >>	8) & 0xFF;
    out[5] = (wdata >>	0) & 0xFF;
    I2C_FM_block_write(out,6);
    //WR_I2C( FLASH_SLVADR, 6, out );
}

/*
void Flash_sla_change (void){

    OIS_UBYTE out[10];
    out[0] = 0xF0;
    out[1] = 0x80;
    out[2] = 0xC4;
    out[3] = 0x00;
    out[4] = 0x44;
    out[5] = 0xE1;
    I2C_FM_block_write(out,6);

    out[0] = 0xF0;
    out[1] = 0x84;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x52;
    out[5] = FLASH_SLVADR;
    I2C_FM_block_write(out,6);
}
*/

// FLASH Slave Address (Just after power on RESET)

//  *****************************************************
//  Write SPI Param, Set FW, DSP data
//  *****************************************************
OIS_LONG bu24_write_flash_fw_data(void){
    
    OIS_ULONG		sts = 0;
    OIS_UBYTE		buf[32 + 2];
    const OIS_UBYTE 	*param2;
    OIS_UWORD 		subaddr_FLASH_DATA_BIN_1;
    OIS_UWORD 		subaddr_FLASH_DATA_BIN_5;
    OIS_UWORD 		subaddr_FLASH_DATA_BIN_6;
#if    0
    OIS_UWORD 		subaddr_FLASH_DATA_BIN_7;
#endif
//    OIS_UBYTE out[10];
    OIS_ULONG		i, len;
	OIS_ULONG 		fw_status;

//    OIS_ULONG wdata;

    //change Flash slave address to 0x70
    //Flash_sla_change();

    // Access Mode
    I2C_FM_8bit_write(FM_acc_mode, 0x01);//0xF000

    // Disable Write protect
    I2C_FM_8bit_write(FM_wri_pro, 0xA5);//0xFF0F
    Wait(1000);

	fw_status  =  I2C_FW_8bit__read(FM_status);
	CAM_ERR(CAM_OIS, "fw_status = %x",fw_status);

	
#if    1
    // Erase exept for Calibration data area
    Erase_flash();
#else
    // Erase All
    I2C_FM_8bit_write(FM_era, 0xEA);//0x8000
    Wait(8000);
#endif
    //Program Flash
    Prog_flash();
    //Download FW
    param2 = FLASH_DATA_BIN_1;
    len = FLASH_DATA_BIN_1_LEN / 0x20;
    subaddr_FLASH_DATA_BIN_1 = FLASH_DATA_BIN_1_START;//0x0000

    for (i = 0; i < len; i++) {
        buf[0] = (subaddr_FLASH_DATA_BIN_1 >> 8);
        buf[1] = (subaddr_FLASH_DATA_BIN_1 & 0xFF);
        memcpy(&buf[2],param2, 0x20);  ///change all
        I2C_FM_block_write(buf,0x20 + 2);
        //WR_I2C( FLASH_SLVADR, 0x20 + 2, buf );
        //Wait(1000);//320ms

        subaddr_FLASH_DATA_BIN_1 += (0x20 >> 2);
        param2  += 0x20;
    }

    param2 = FLASH_DATA_BIN_5;
    len = FLASH_DATA_BIN_5_LEN / 0x20;
    subaddr_FLASH_DATA_BIN_5 = FLASH_DATA_BIN_5_START;//0x0A00

    for (i = 0; i < len; i++) {
        buf[0] = (subaddr_FLASH_DATA_BIN_5 >> 8);
        buf[1] = (subaddr_FLASH_DATA_BIN_5 & 0xFF);
        memcpy(&buf[2], param2, 0x20);
        I2C_FM_block_write(buf,0x20 + 2);
        //WR_I2C( FLASH_SLVADR, 0x20 + 2, buf );
        //Wait(1000);//992ms

        subaddr_FLASH_DATA_BIN_5 += (0x20 >> 2);
        param2  += 0x20;
    }

    param2 = FLASH_DATA_BIN_6;
    len = FLASH_DATA_BIN_6_LEN / 0x20;
    subaddr_FLASH_DATA_BIN_6 = FLASH_DATA_BIN_6_START;//0x2580

    for (i = 0; i < len; i++) {
        buf[0] = (subaddr_FLASH_DATA_BIN_6 >> 8);
        buf[1] = (subaddr_FLASH_DATA_BIN_6 & 0xFF);
        memcpy(&buf[2], param2, 0x20);
        I2C_FM_block_write(buf,0x20 + 2);
        //WR_I2C( FLASH_SLVADR, 0x20 + 2, buf );
        //Wait(1000);//112ms

        subaddr_FLASH_DATA_BIN_6 += (0x20 >> 2);
        param2  += 0x20;
    }


#if    0
    /*
        Calibration Data region is involved in FLASH_DATA_BIN_7

        Therefore writing FLASH_DATA_BIN_7 is invalidated
        so that existing Calibration Data written by LuxVision cannot be overwritten.
    */
    param2 = FLASH_DATA_BIN_7;
    len = FLASH_DATA_BIN_7_LEN / 0x20;
    subaddr_FLASH_DATA_BIN_7 = FLASH_DATA_BIN_7_START;

    for (i = 0; i < len; i++) {
        buf[0] = (subaddr_FLASH_DATA_BIN_7 >> 8);
        buf[1] = (subaddr_FLASH_DATA_BIN_7 & 0xFF);
        memcpy_s(&buf[2], param2, 0x20);
        I2C_FM_block_write(buf,0x20 + 2);
        //WR_I2C( g_rohm24721_dev_d, 0x20 + 2, buf );
        Wait(20000);

        subaddr_FLASH_DATA_BIN_7 += (0x20 >> 2);
        param2  += 0x20;
    }
#endif

    return sts;

}

/*
void Gyro_gain_set(float X_gain, float Y_gain){
    OIS_UWORD X_ori, Y_ori;
    X_ori = X_gain*I2C_OIS_16bit__read(Gyro_gain_x);//read 0xF07A
    Y_ori = Y_gain*I2C_OIS_16bit__read(Gyro_gain_y);//read 0xF07C
    I2C_OIS_16bit_write(Gyro_gain_x, X_ori);
    I2C_OIS_16bit_write(Gyro_gain_y, Y_ori);
}
*/

struct _FACT_ADJ Gyro_offset_cal(void){ 

    I2C_OIS_8bit_write(OIS_control, Servo_on);//0xF020,0x01
    F024_Polling();
    I2C_OIS_8bit_write(OIS_SPI, SPI_Monitor);//0xF02A,0x04 SPI Monitor mode
    F024_Polling();
    I2C_OIS_8bit_write(OIS_gyro_mode, OIS_gyro_on);//0xF023,0x00
    F024_Polling();
    //X offset
    I2C_OIS_8bit_write(Gyro_offset_lat, 0x00); //0xF088,0x00 select X axis
    Wait(70000);
    F024_Polling();
    FADJ_CAL.gl_GX_OFS = I2C_OIS_16bit__read(Gyro_offset_val);//0xF08A
    //Y offset
    I2C_OIS_8bit_write(Gyro_offset_lat, 0x01); //0xF088,0x01 select Y axis
    Wait(70000);
    F024_Polling();
    FADJ_CAL.gl_GY_OFS = I2C_OIS_16bit__read(Gyro_offset_val);//0xF08A

    //update to register
    I2C_OIS_8bit_write(Gyro_offset_req,0x00);//0xF09C,0x00 select X axis to update
    I2C_OIS_16bit_write(Gyro_offset_diff,FADJ_CAL.gl_GX_OFS);//write the X offset value to 0xF09D
    I2C_OIS_8bit_write(Gyro_offset_req,0x01);//0xF09C,0x01 select Y axis to update
    I2C_OIS_16bit_write(Gyro_offset_diff,FADJ_CAL.gl_GY_OFS);//write the Y offset value to 0xF09D

    //verify gyro offset
    I2C_OIS_8bit_write(Gyro_offset_req,0x02);//0xF09C,0x02 to output X diff value
    F024_Polling();
    I2C_OIS_16bit__read(Gyro_offset_diff);//read 0xF09D to get the X diff value
    I2C_OIS_8bit_write(Gyro_offset_req,0x03);//0xF09C,0x02 to output Y diff value
    F024_Polling();
    I2C_OIS_16bit__read(Gyro_offset_diff);//read 0xF09D to get the X diff value
    return FADJ_CAL;
}

void Update_GyroCalib_to_flash(OIS_UWORD gyro_X_offset, OIS_UWORD gyro_Y_offset, OIS_UWORD gyro_X_gain, OIS_UWORD gyro_Y_gain){

    OIS_UBYTE 	buf[0x200 + 2];
    OIS_ULONG 	rdata;
    OIS_ULONG 	i, j;
    OIS_UBYTE 	buf_34[0x20+2];
	
    I2C_FM_8bit_write(FM_acc_mode, 0x01);//0xF000,0x01
    for (i = 0; i < (0x200 / 4); i++) {
        // READ 512byte from Start address of Gyro Calibration data on FLASH before ERASE
        rdata = I2C_FM_32bit__read(FLASH_ADDR_GYRO_CALIB + i);

        buf[((i * 4) + 0) + 2] = (rdata >> 24);        // Store rdata[31:24]
        buf[((i * 4) + 1) + 2] = (rdata >> 16);        // Store rdata[23:16]
        buf[((i * 4) + 2) + 2] = (rdata >>  8);        // Store rdata[15: 8]
        buf[((i * 4) + 3) + 2] = (rdata >>  0);        // Store rdata[ 7: 0]
    }
    // Disable Write protect
    I2C_FM_8bit_write(FM_wri_pro, 0xA5);//0xFF0F
    Wait(100);

    // ERASE 512byte from Start address of Gyro Calibration data on FLASH
    I2C_FM_8bit_write(FLASH_ADDR_GYRO_CALIB | 0x8000, 0xE0);//0xF800 >> 2
    Wait(2000);//wait 2ms

    /* OverWrite Gyro Calibration data and PROGRAM to FLASH */
    buf[0] = (FLASH_ADDR_GYRO_CALIB >> 8);
    buf[1] = (FLASH_ADDR_GYRO_CALIB & 0xFF);
    if ((gyro_X_offset != 0)|(gyro_Y_offset != 0)){
    buf[2] = (gyro_X_offset >> 8);
    buf[3] = (gyro_X_offset & 0xFF);
    buf[4] = (gyro_Y_offset >> 8);
    buf[5] = (gyro_Y_offset & 0xFF);
    }
    if ((gyro_X_gain != 0)&(gyro_Y_gain != 0)){
    buf[6] = (gyro_X_gain >> 8);
    buf[7] = (gyro_X_gain & 0xFF);
    buf[8] = (gyro_Y_gain >> 8);
    buf[9] = (gyro_Y_gain & 0xFF);
    buf[14] = 0xCC;
    buf[15] = 0x01; //mark address for update
    }
    else {    buf[15] = 0x00; }
    for (i = 0; i < (0x200 / 32); i++) {
        buf_34[0] = (FLASH_ADDR_GYRO_CALIB + i*8) >> 8;
        buf_34[1] = ((FLASH_ADDR_GYRO_CALIB + i*8) & 0xFF);
        for (j = 0; j < 0x20; j++){
            buf_34[2 + j] = buf[i*8 + 2 + j];
	}
        I2C_FM_block_write(buf_34, 0x20 + 2);
        //WR_I2C( g_rohm24721_dev, 0x20 + 2, buf_34 );
        Wait(1000);
    }

//    WR_I2C( g_rohm24721_dev, 0x200 + 2, buf );
    Wait(1000);
}

// OIS mode set mode=0 Servo off mode = 1 servo on mode=2 OIS on
void OIS_mode_set(OIS_UWORD mode){
    if(mode == 0){
        F024_Polling();
        I2C_OIS_8bit_write(OIS_control, OIS_standby);//0xF020,0x00
    }else if(mode == 1){
        F024_Polling();
        I2C_OIS_8bit_write(OIS_control, Servo_on);//0xF020,0x01
    }else{
        F024_Polling();
        I2C_OIS_8bit_write(OIS_control, OIS_on);//0xF020,0x02
    }
}
void OIS_manual_set(OIS_UWORD X_pos, OIS_UWORD Y_pos){

    I2C_OIS_8bit_write(OIS_control, Servo_on);//0xF020,0x01
    F024_Polling();
    I2C_OIS_16bit_write(Reg_x_pos, X_pos);//0xF1DE
    I2C_OIS_16bit_write(Reg_y_pos, Y_pos);//0xF1E0
    I2C_OIS_16bit_write(Manu_en, 0x03);//0xF1DC
    Wait(50);
    F024_Polling();
}
void Update_Gyro_offset_gain_cal_from_flash(void){
    /*
        Just After FLASH boot is successful
    */
    OIS_UWORD en_gyrogain_update;

    en_gyrogain_update = I2C_FM_32bit__read(FLASH_ADDR_GYRO_CALIB);//(0xF800 >> 2)
    en_gyrogain_update = en_gyrogain_update&0x01;
    /* Notice the Start address of Gyro Calibration data on FLASH */
    I2C_OIS_16bit_write(0xF1E2, FLASH_ADDR_GYRO_CALIB);

    if (en_gyrogain_update) {
        /* Gyro Gain Update from FLASH is performed */
        I2C_OIS_8bit_write(0xF1E4, 1);
    } else {
        /* Gyro Gain Update from FLASH is not performed */
        I2C_OIS_8bit_write(0xF1E4, 0);
    }
}
#define ACCURACY 0x04
void Circle_test(void){

    OIS_UWORD Error = 0;
    OIS_UWORD Error_x, Error_y;
    I2C_OIS_8bit_write(OIS_control, Servo_on);//0XF020,0x01
    Wait(100);
    F024_Polling();
    I2C_OIS_8bit_write(CIR_thr, ACCURACY);//0xF1A0,0x04
    F024_Polling();

    I2C_OIS_8bit_write(CIR_amp_x, 0x4D);//0xF1A4,0x4D
    F024_Polling();
    I2C_OIS_8bit_write(CIR_amp_y, 0x1A);//0xF1A5,0x1A
    F024_Polling();
    I2C_OIS_8bit_write(CIR_sta, 0x01);//0xF1A7,0x01
    Wait(3000);
    F024_Polling();
    Error = I2C_OIS_8bit__read(CIR_stu);//Read 0xF1A8 to get the error value
    if (Error == 4){
            // CIR test OK
    } else{
        Error_x = I2C_OIS_16bit__read(CIR_err_x);
        Error_y = I2C_OIS_16bit__read(CIR_err_y);
        CAM_ERR(CAM_OIS, "Error_x = %04X Error_y = %04X", Error_x,Error_y);
        // rdata_x = I2C_OIS_read_burst(0xF200, 0x200)
        // rdata_y = I2C_OIS_read_burst(0xF400, 0x200)
    }

}
void OIS_mag_check(OIS_UWORD *srv_x, OIS_UWORD *srv_y){
    OIS_UWORD	x = 0;
    OIS_UWORD	y = 0;
    OIS_mode_set(1);
    Wait(3000);
    F024_Polling();
    I2C_OIS_8bit_write(ADC_seq, ADC_x);
    F024_Polling();
    //ois_read_16bit_reg_16bit_data(g_rohm24721_dev, 0xF062, &x);
    //*srv_x = I2C_OIS_16bit_read(ADC_val);
    *srv_x = (OIS_UWORD)x;
    I2C_OIS_8bit_write(ADC_seq, ADC_y);
    F024_Polling();
    //ois_read_16bit_reg_16bit_data(g_rohm24721_dev, 0xF062, &x);
    //*srv_y = I2C_OIS_16bit_read(ADC_val);
    *srv_y = (OIS_UWORD)y;
    OIS_mode_set(1);
}

void OIS_cal_check(void){
    //OIS_UBYTE	cal_dat[128];
    //SHT3x_full_init(FLASH_SLVADR);//change IIC slave address to Flash
    //I2C_read_status(0x3F70,cal_dat,128);
    //SHT3x_full_init(SLV_OIS_24721);
}

void OIS_soft_reset(OIS_ULONG Prog_ID){
    if(Prog_ID!=BU24721_FW_VERSION){
        I2C_OIS_8bit_write(OIS_reset_1,0x00);
        Wait(100);
        I2C_OIS_8bit_write(OIS_reset_2,0x00);
        Wait(6000);//wait 6ms
        F024_Polling();
        I2C_OIS_8bit_write(OIS_release_Standby,0x00);
        Wait(100);
        F024_Polling();
    }
}

void Boot_err_sla_change (void){
    OIS_UBYTE out[10];
	I2C_OIS_8bit_write(OIS_boot_mode,0x55);
	OIS_soft_reset(0);
	I2C_OIS_8bit_write(OIS_boot_mode,0x00);
    out[0] = 0xF0;
    out[1] = 0x80;
    out[2] = 0xC4;
    out[3] = 0x00;
    out[4] = 0x44;
    out[5] = 0xE1;
    I2C_OIS_block_write(out,6);

    out[0] = 0xF0;
    out[1] = 0x84;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x52;
    out[5] = FLASH_SLVADR;
    I2C_OIS_block_write(out,6);
	F024_Polling();
//	Wait(100000);
//	I2C_OIS_32bit__read(0xF080);
//	I2C_OIS_32bit__read(0xF084);
}


int Rohm_bu24721_fw_download(void)
{
	OIS_ULONG	Prog_ID;
	int ret = 0;

	I2C_OIS_8bit_write(OIS_release_Standby, 0x00);  //release standby
	ret = F024_Polling();
	CAM_ERR(CAM_OIS, "polling ret  = %d",ret);

	Prog_ID = I2C_OIS_32bit__read(FW_ID);//check 0xF01C program ID
	CAM_ERR(CAM_OIS, "Prog_ID = %08x FW_Version = %08x",Prog_ID,BU24721_FW_VERSION);

	//just for TEST
	//Gyro_offset_cal();
	//test end

	if(Prog_ID < BU24721_FW_VERSION)
	{
		if(Prog_ID == 0){
			Boot_err_sla_change();
			CAM_ERR(CAM_OIS, "boot err change flash to %x",FLASH_SLVADR<<1);
		}else{
			I2C_OIS_8bit_write(Fla_strl, (FLASH_SLVADR<<1));  //change slave addr to 0x70
			CAM_ERR(CAM_OIS, "change flash to %x",FLASH_SLVADR<<1);
		}
		ret = bu24_write_flash_fw_data();
		if(!ret)
		{
			CAM_ERR(CAM_OIS, "DL Done");
			//need checksum ..

			// pre_software_reset
			I2C_OIS_8bit_write(PRE_SOFTWARE_RESET, 0x00);//0xF097
			Wait(1000);
			// software_reset
			I2C_OIS_8bit_write(SOFTWARE_RESET, 0x00);//0xF058
			Wait(1000);
			I2C_OIS_8bit_write(OIS_release_Standby, 0x00);  //release standby
			ret = F024_Polling();
			CAM_ERR(CAM_OIS, "tele_ois: read 0xF004 = %d", ret);
			Prog_ID = I2C_OIS_32bit__read(FW_ID);//check 0xF01C program ID
			CAM_ERR(CAM_OIS, "After DL Prog_ID = %08x FW ID:%8x",Prog_ID,BU24721_FW_VERSION);
			if(BU24721_FW_VERSION == Prog_ID)
			{
				CAM_ERR(CAM_OIS, "Tele DL sucessful");
				ret = 0;
			}
			else
			{
				CAM_ERR(CAM_OIS, "Tele DL Failed");
				ret = -1;
			}
		}
	}
	else
	{
		CAM_ERR(CAM_OIS, "FW is New no need DL FW");
		ret = 0;
	}
	return ret;
}




