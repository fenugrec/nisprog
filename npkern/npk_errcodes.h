#ifndef _NPK_ERRCODES_H
#define _NPK_ERRCODES_H
/* Supplemental negative response codes for kernel errors */

/* (c) copyright fenugrec 2017
 * GPLv3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* SID_CONF error codes */
#define SID_CONF_CKS1_BADCKS	0x77	//NRC when crc is bad


/**** Common flash error codes for all platforms. */

/* some standard iso14230 errors */
#define ISO_NRC_GR	0x10	/* generalReject */
#define ISO_NRC_SNS	0x11	/* serviceNotSupported */
#define ISO_NRC_SFNS_IF	0x12	/* subFunctionNotSupported-Invalid Format */
#define ISO_NRC_CNCORSE	0x22	/* conditionsNoteCorrectOrRequestSequenceError */
#define ISO_NRC_IK	0x35	/* invalidKey */
#define ISO_NRC_CNDTSA	0x42	/* canNotDownloadToSpecifiedAddress */


/* Custom errors adjusted to fit with 180nm error codes (different from possible FPFR return values)
 * and serve as the iso14230 NRC
 *
 * i.e. FPFR has bits 0..2 defined. Map those errors as (0x80 | FPFR)
 */
#define PF_ERROR 0x80		//generic flashing error : FWE, etc

// FPFR block : 0x81-0x87 (180nm only)
// keep only bits 1,2 ? (&= 0x06)
#define PF_FPFR_BASE 0x80		//just for |= op
#define PF_FPFR_BADINIT 0x81	//initialization failed
#define PF_FPFR_FQ 0x82		//Setting of operating freq abnormal
#define PF_FPFR_UB 0x84		//user branch setting abnormal (not used)
//0x85 combined errors, not sure if possible
//0x86 ..
//0x87 ..

/****  general block */
#define PFWB_OOB 0x88		//dest out of bounds
#define PFWB_MISALIGNED 0x89	//dest not on 128B boundary
#define PFWB_LEN 0x8A		//len not multiple of 128
#define PFWB_VERIFAIL 0x8B	//post-write verify failed

#define PFEB_BADBLOCK 0x8C	//erase: bad block #
#define PFEB_VERIFAIL 0x8D	//erase verify failed

#define PF_SILICON 0x8E	//Not running on correct silicon (180 / 350nm)

/**** 7055 (350nm) codes  */
#define PFWB_MAXRET 0x8F	//max # of rewrite attempts

/**** consult-2 codes : defines at least 0x90-0x95. skip those */
//0x90
//..
//0x9F

/**** 7051 (350nm) codes */
#define PF_ERROR_AFTERASE 0xA0
#define PF_ERROR_B4WRITE 0xA1
#define PF_ERROR_AFTWRITE 0xA2
#define PF_ERROR_VERIF 0xA3
//0xA4
//..
//0xA7

/**** 180nm SID_FLREQ ( RequestDownload) codes */
#define SID34_BADFCCS	0xA8
#define SID34_BADRAMER	0xA9
#define SID34_BADDL_ERASE	0xAA
#define SID34_BADDL_WRITE	0xAB
#define SID34_BADINIT_ERASE	0xAC
#define SID34_BADINIT_WRITE	0xAD
#define SID34_BADINIT_TDER 0xAE
#define SID34_BADINIT_FTDAR 0xAF

/**** 180nm SID_FLREQ DPFR codes
 * actual DPFR value is masked to keep bits 1,2 then |= 0xB0 .
 */
#define SID34_DPFR_BASE 0xB0	//just for |= op
#define SID34_DPFR_SF 0xB1	//success/fail... never set by itself ?
#define SID34_DPFR_FK 0xB2	//flash key register error
#define SID34_DPFR_SS 0xB4	//source select error
//0xB5 combined errrors
//0xB6 ..
//0xB7 ..



#endif	//_NPK_ERRCODES_H
