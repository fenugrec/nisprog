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

/* SID_FLREQ (0x34 RequestDownload) neg response codes */
#define SID34_BADFCCS	0x81
#define SID34_BADRAMER	0x82
#define SID34_BADDL_ERASE	0x83
#define SID34_BADDL_WRITE	0x84
#define SID34_BADINIT_ERASE	0x85
#define SID34_BADINIT_WRITE	0x86


/**** Common flash error codes for all platforms.
 * adjusted to fit with 180nm error codes (different from possible FPFR return values)
 * and double as the iso14230 NRC*/

#define PF_SILICON 0x81	//Not running on correct silicon (180 / 350nm)

#define PFEB_BADBLOCK (0x84 | 0x00)	//bad block #
#define PFEB_VERIFAIL (0x84 | 0x01)	//erase verify failed

#define PFWB_OOB (0x88 | 0x00)		//dest out of bounds
#define PFWB_MISALIGNED (0x88 | 0x01)	//dest not on 128B boundary
#define PFWB_LEN (0x88 | 0x02)		//len not multiple of 128
#define PFWB_VERIFAIL (0x88 | 0x03)	//post-write verify failed


/**** 7055 (350nm) codes  */
#define PFWB_MAXRET (0x88 | 0x04)	//max # of rewrite attempts
#define PF_ERROR 0x80		//generic flashing error : FWE, etc


#endif	//_NPK_ERRCODES_H
