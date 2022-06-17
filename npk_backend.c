/*
 *	nisprog - Nissan ECU communications utility
 *
 * Copyright (c) 2014-2016 fenugrec
 *
 * Licensed under GPLv3
 *
 * back-end functions to interact with npkern
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stypes.h"

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_os.h"
#include "diag_iso14230.h"  //for NRC decoding

#include "npk_backend.h"
#include "nissutils/cli_utils/nislib.h"
#include "npkern/iso_cmds.h"
#include "npkern/npk_errcodes.h"


#define CURFILE "npk_backend.c" //HAAAX

/* flash block definitions */
const struct flashblock fblocks_7058[] = {
	{0x00000000,    0x00001000},
	{0x00001000,    0x00001000},
	{0x00002000,    0x00001000},
	{0x00003000,    0x00001000},
	{0x00004000,    0x00001000},
	{0x00005000,    0x00001000},
	{0x00006000,    0x00001000},
	{0x00007000,    0x00001000},
	{0x00008000,    0x00018000},
	{0x00020000,    0x00020000},
	{0x00040000,    0x00020000},
	{0x00060000,    0x00020000},
	{0x00080000,    0x00020000},
	{0x000A0000,    0x00020000},
	{0x000C0000,    0x00020000},
	{0x000E0000,    0x00020000},
};

const struct flashblock fblocks_7055[] = {
	{0x00000000,    0x00001000},
	{0x00001000,    0x00001000},
	{0x00002000,    0x00001000},
	{0x00003000,    0x00001000},
	{0x00004000,    0x00001000},
	{0x00005000,    0x00001000},
	{0x00006000,    0x00001000},
	{0x00007000,    0x00001000},
	{0x00008000,    0x00008000},
	{0x00010000,    0x00010000},
	{0x00020000,    0x00010000},
	{0x00030000,    0x00010000},
	{0x00040000,    0x00010000},
	{0x00050000,    0x00010000},
	{0x00060000,    0x00010000},
	{0x00070000,    0x00010000},
};

const struct flashblock fblocks_7051[] = {
	{0x00000000,    0x00008000},
	{0x00008000,    0x00008000},
	{0x00010000,    0x00008000},
	{0x00018000,    0x00008000},
	{0x00020000,    0x00008000},
	{0x00028000,    0x00008000},
	{0x00030000,    0x00008000},
	{0x00038000,    0x00007000},
	{0x0003F000,    0x00000400},
	{0x0003F400,    0x00000400},
	{0x0003F800,    0x00000400},
	{0x0003FC00,    0x00000400},
};

const struct flashdev_t flashdevices[] = {
	{ "7051", SH7051, 256 * 1024, 12, fblocks_7051 },
	{ "7055", SH7055, 512 * 1024, 16, fblocks_7055 },
	{ "7058", SH7058, 1024 * 1024, 16, fblocks_7058 },
	{ NULL, SH_INVALID, 0, 0, NULL },
};


/** Decode negative response code into a short error string.
 *
 * rxdata[] must contain at least 3 bytes, "7F <SID> <NRC>"
 * returns a static char * that must not be free'd !
 */
static const char *decode_nrc(uint8_t *rxdata);



/*** CRC16 implementation adapted from Lammert Bies
 * https://www.lammertbies.nl/comm/info/crc-calculation.html
 *
 *
 */
#define NPK_CRC16   0xBAAD  //koopman, 2048bits (256B)
static bool crc_tab16_init = 0;
static u16 crc_tab16[256];

static void init_crc16_tab( void ) {
	u32 i, j;
	u16 crc, c;

	for (i=0; i<256; i++) {
		crc = 0;
		c   = (u16) i;

		for (j=0; j<8; j++) {
			if ( (crc ^ c) & 0x0001 ) {
				crc = ( crc >> 1 ) ^ NPK_CRC16;
			} else {
				crc =   crc >> 1;
			}
			c = c >> 1;
		}
		crc_tab16[i] = crc;
	}

	crc_tab16_init = 1;

}  /* init_crc16_tab */


static u16 crc16(const u8 *data, u32 siz) {
	u16 crc;

	if ( !crc_tab16_init ) {
		init_crc16_tab();
	}

	crc = 0;

	while (siz > 0) {
		u16 tmp;
		u8 nextval;

		nextval = *data++;
		tmp =  crc       ^ nextval;
		crc = (crc >> 8) ^ crc_tab16[ tmp & 0xff ];
		siz -= 1;
	}
	return crc;
}

/** compare CRC of source data at *src to ROM
 * the area starting at src[0] is compared to the area of ROM
 * starting at <start>, for a total of <len> bytes (rounded up)
 *
 * Caller must have validated parameters
 *
 * @param modified: result of crc check is written to that variable
 * @return 0 if comparison completed correctly
 */
#define ROMCRC_NUMCHUNKS 4
#define ROMCRC_CHUNKSIZE 256
#define ROMCRC_ITERSIZE (ROMCRC_NUMCHUNKS * ROMCRC_CHUNKSIZE)
#define ROMCRC_LENMASK ((ROMCRC_NUMCHUNKS * ROMCRC_CHUNKSIZE) - 1)  //should look like 0x3FF
static int check_romcrc(const uint8_t *src, uint32_t start, uint32_t len, bool *modified) {
	uint8_t txdata[4 + (2*ROMCRC_NUMCHUNKS)];   //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	uint8_t rxbuf[10];
	int errval;
	uint16_t chunko;
	//uint16_t maxchunks;

	len = (len + ROMCRC_LENMASK) & ~ROMCRC_LENMASK;

	chunko = start / ROMCRC_CHUNKSIZE;

	//request format : <SID_CONF> <SID_CONF_CKS1> <CNH> <CNL> <CRC0H> <CRC0L> ...<CRC3H> <CRC3L>
	//verify if <CRCH:CRCL> hash is valid for n*256B chunk of the ROM (starting at <CNH:CNL> * 256)
	unsigned txi;
	txdata[0]=SID_CONF;
	txdata[1]=SID_CONF_CKS1;

	nisreq.data=txdata;


	for (; len > 0; len -= ROMCRC_ITERSIZE, chunko += ROMCRC_NUMCHUNKS) {
		txi = 2;
		txdata[txi++] = chunko >> 8;
		txdata[txi++] = chunko & 0xFF;

		//fill the request with n*CRCs
		unsigned chunk_cnt;
		for (chunk_cnt = 0; chunk_cnt < ROMCRC_NUMCHUNKS; chunk_cnt++) {
			u16 chunk_crc = crc16(src, ROMCRC_CHUNKSIZE);
			src += ROMCRC_CHUNKSIZE;
			txdata[txi++] = chunk_crc >> 8;
			txdata[txi++] = chunk_crc & 0xFF;
		}
		nisreq.len = txi;


		//rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("\nl2_send error!\n");
			return -1;
		}

		//responses :	01 <SID_CONF+0x40> <cks> for good CRC
		//				03 7F <SID_CONF> <SID_CONF_CKS1_BADCKS> <cks> for bad CRC
		// anything else is an error that causes abort
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, rxbuf, 3, 50);
		if (errval != 3) {
			printf("\nno response @ chunk %X\n", (unsigned) chunko);
			goto badexit;
		}

		if (rxbuf[1] == SID_CONF + 0x40) {
			continue;
		}
		//so, it's a 03 7F <SID_CONF> <NRC> <cks> response. Get remainder of packet
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, rxbuf+3, 2, 50);
		if (errval != 2) {
			printf("\nweirdness @ chunk %X\n", (unsigned) chunko);
			goto badexit;
		}

		if ((rxbuf[2] != SID_CONF) || (rxbuf[3] != SID_CONF_CKS1_BADCKS)) {
			printf("\ngot bad SID_FLASH_CKS1 response : ");
			goto badexit;
		}
		//confirmed bad CRC, we can exit
		*modified = 1;
		return 0;
	}   //for

	*modified = 0;
	return 0;

badexit:
	diag_data_dump(stdout, rxbuf, sizeof(rxbuf));
	printf("\n");
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	return -1;
}


int get_changed_blocks(const uint8_t *src, const uint8_t *orig_data, const struct flashdev_t *fdt, bool *modified) {

	unsigned blockno;

	printf("\n");
	for (blockno = 0; blockno < (fdt->numblocks); blockno++) {
		u32 bs, blen;
		bs = fdt->fblocks[blockno].start;
		blen = fdt->fblocks[blockno].len;

		printf("\rchecking block %02u/%02u (%06lX-%06lX)...",
		       blockno, fdt->numblocks -1, (unsigned long) bs, (unsigned long) bs + blen -1);
		fflush(stdout);
		/* compare with caller's buffer if provided: */
		if (orig_data) {
			if (memcmp(&src[bs], &orig_data[bs], blen) == 0) {
				modified[blockno] = 0;
			} else {
				modified[blockno] = 1;
			}
		} else {
			/* otherwise do CRC comparison with ECU */
			if (check_romcrc(&src[bs], bs, blen, &modified[blockno])) {
				return -1;
			}
		}
	}
	printf(" done.\n");
	return 0;
}




/* special checksum for reflash blocks:
 * "one's complement" checksum; if adding causes a carry, add 1 to sum. Slightly better than simple 8bit sum
 */
static uint8_t cks_add8(uint8_t *data, unsigned len) {
	uint16_t sum = 0;
	for (; len; len--, data++) {
		sum += *data;
		if (sum & 0x100) {
			sum += 1;
		}
		sum = (uint8_t) sum;
	}
	return sum;
}


/* ret 0 if ok. For use by reflash_block(),
 * assumes parameters have been validated,
 * and appropriate block has been erased
 */

static int npk_raw_flashblock(const uint8_t *src, uint32_t start, uint32_t len) {

	/* program 128-byte chunks */
	uint32_t remain = len;

	uint8_t txdata[134];    //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	int errval;
	nisreq.data = txdata;

	unsigned long t0, chrono;

	if ((len & (128 - 1)) ||
	    (start & (128 - 1))) {
		printf("error: misaligned start / length ! \n");
		return -1;
	}

	txdata[0]=SID_FLASH;
	txdata[1]=SIDFL_WB;
	nisreq.len = 134;   //2 (header) + 3 (addr) + 128 (payload) + 1 (extra CRC)

	t0 = diag_os_getms();


	while (remain) {
		uint8_t rxbuf[10];
		unsigned curspeed, tleft;

		chrono = diag_os_getms() - t0;
		if (!chrono) {
			chrono += 1;
		}
		curspeed = 1000 * (len - remain) / chrono;  //avg B/s
		if (!curspeed) {
			curspeed += 1;
		}
		tleft = remain / curspeed;  //s
		if (tleft > 9999) {
			tleft = 9999;
		}

		printf("\rwriting chunk @ 0x%06X (%3u %%, %5u B/s, ~ %4u s remaining)", start, (unsigned) 100 * (len - remain) / len,
		       curspeed, tleft);
		fflush(stdout);

		txdata[2] = start >> 16;
		txdata[3] = start >> 8;
		txdata[4] = start >> 0;
		memcpy(&txdata[5], src, 128);
		txdata[133] = cks_add8(&txdata[2], 131);

		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("l2_send error!\n");
			return -1;
		}

		/* expect exactly 3 bytes, but with generous timeout */
		//rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, rxbuf, 3, 800);
		if (errval <= 1) {
			printf("\n\tProblem: no response @ %X\n", (unsigned) start);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}
		if (errval < 3) {
			printf("\n\tProblem: incomplete response @ %X\n", (unsigned) start);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			return -1;
		}

		if (rxbuf[1] != (SID_FLASH + 0x40)) {
			//maybe negative response, if so, get the remaining packet
			printf("\n\tProblem: bad response @ %X: ", (unsigned) start);

			int needed = 1 + rxbuf[0] - errval;
			if (needed > 0) {
				errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, &rxbuf[errval], needed, 300);
			}
			if (errval < 0) {
				errval = 0;             //floor
			}
			printf("%s\n", decode_nrc(&rxbuf[1]));
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}

		remain -= 128;
		start += 128;
		src += 128;

	}   //while len
	printf("\nWrite complete.\n");

	return 0;
}



int reflash_block(const uint8_t *newdata, const struct flashdev_t *fdt, unsigned blockno, bool practice) {
	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	int errval;
	struct diag_msg *rxmsg;

	uint32_t start;
	uint32_t len;

	nisreq.data=txdata;

	if (blockno >= fdt->numblocks) {
		printf("block # out of range !\n");
		return -1;
	}

	start = fdt->fblocks[blockno].start;
	len = fdt->fblocks[blockno].len;

	/* 1- requestdownload */
	txdata[0]=SID_FLREQ;
	nisreq.len = 1;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		goto badexit;
	}
	if (rxmsg->data[0] != (SID_FLREQ + 0x40)) {
		printf("got bad RequestDownload response : %s\n", decode_nrc(rxmsg->data));
		diag_freemsg(rxmsg);
		goto badexit;
	}

	/* 2- Unprotect maybe */
	if (!practice) {
		txdata[0]=SID_FLASH;
		txdata[1]=SIDFL_UNPROTECT;
		txdata[2]=~SIDFL_UNPROTECT;
		nisreq.len = 3;
		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL) {
			goto badexit;
		}
		if (rxmsg->data[0] != (SID_FLASH + 0x40)) {
			printf("got bad Unprotect response : %s\n", decode_nrc(rxmsg->data));
			diag_freemsg(rxmsg);
			goto badexit;
		}
		printf("Entered flashing_enabled (unprotected) mode\n");
	}

	/* 3- erase block */
	printf("Erasing block %u (0x%06X-0x%06X)...\n",
	       blockno, (unsigned) start, (unsigned) start + len - 1);
	txdata[0] = SID_FLASH;
	txdata[1] = SIDFL_EB;
	txdata[2] = blockno;
	nisreq.len = 3;
	/* Problem : erasing can take a lot more than the default P2max for iso14230 */
	uint16_t old_p2max = global_l2_conn->diag_l2_p2max;
	global_l2_conn->diag_l2_p2max = 1800;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	global_l2_conn->diag_l2_p2max = old_p2max;  //restore p2max; the rest should be OK
	if (rxmsg==NULL) {
		printf("no ERASE_BLOCK response?\n");
		goto badexit;
	}
	if (rxmsg->data[0] != (SID_FLASH + 0x40)) {
		printf("got bad ERASE_BLOCK response : %s\n", decode_nrc(rxmsg->data));
		diag_freemsg(rxmsg);
		goto badexit;
	}

	/* 4- write */
	errval = npk_raw_flashblock(newdata, start, len);
	if (errval) {
		printf("\nReflash error ! Do not panic, do not reset the ECU immediately. The kernel is "
		       "most likely still running and receiving commands !\n");
		goto badexit;
	}

	return 0;

badexit:
	return -1;

}


uint8_t *load_rom(const char *fname, uint32_t expect_size) {
	FILE *fpl;
	uint8_t *buf;

	if (!fname) {
		return NULL;
	}
	if (!expect_size) {
		return NULL;
	}

	if ((fpl = fopen(fname, "rb"))==NULL) {
		printf("Cannot open %s !\n", fname);
		return NULL;
	}

	u32 file_len = flen(fpl);
	if (file_len != expect_size) {
		printf("error : wrong file length 0x%06lX (wanted 0x%06lX)!\n",
		       (unsigned long) file_len, (unsigned long) expect_size);
		goto badexit;
	}

	if (diag_malloc(&buf, file_len)) {
		printf("malloc prob\n");
		goto badexit;
	}

	if (fread(buf, 1, file_len, fpl) != file_len) {
		printf("fread prob !?\n");
		free(buf);
		goto badexit;
	}

	fclose(fpl);
	return buf;

badexit:
	fclose(fpl);
	return NULL;
}


int set_eepr_addr(uint32_t addr) {
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	uint8_t txdata[8];  //data for nisreq

	int errval;

	nisreq.data=txdata;

	/* set eeprom_read() function address <SID_CONF> <SID_CONF_SETEEPR> <AH> <AM> <AL> */
	txdata[0] = SID_CONF;
	txdata[1] = SID_CONF_SETEEPR;
	txdata[2] = (addr >> 16) & 0xff;
	txdata[3] = (addr >> 8) & 0xff;
	txdata[4] = (addr >> 0) & 0xff;
	nisreq.len=5;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return -1;
	}
	if (rxmsg->data[0] != (SID_CONF + 0x40)) {
		printf("got bad SID_CONF response : %s\n", decode_nrc(rxmsg->data));
		diag_freemsg(rxmsg);
		return -1;
	}

	diag_freemsg(rxmsg);
	return 0;
}


//TODO : merge these into npkern defs ?
#define KSPEED_FROM_BRR(x) ((20 * 1000 * 1000UL) / (32 * ((x) + 1)))
#define BRR_FROM_KSPEED(x) (((20 * 1000 * 1000UL) / (32 * (x))) - 1)

int set_kernel_speed(uint16_t kspeed) {
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	uint8_t txdata[8];  //data for nisreq

	int errval;
	unsigned newdiv;
	float pct_error;

	if (kspeed < KSPEED_FROM_BRR(0xFF)) {
		printf("kspeed value out of bounds !\n");
		return -1;
	}

	newdiv = BRR_FROM_KSPEED(kspeed);

	/* because of rounding, check this too */
	if ((newdiv == 0) || (newdiv > 0xFF)) {
		printf("Illegal BRR value for kspeed %u !\n", kspeed);
		return -1;
	}

	pct_error = 100.0 * (KSPEED_FROM_BRR(newdiv) - kspeed) / kspeed;
	/* faster kernel speeds are more of a problem since its answer come in too fast
	 * for the PC */
	if ((pct_error >= 2) || (pct_error <= -4)) {
		printf("BRR is %.1f%% away from requested value; aborting\n", pct_error);
		return -1;
	}

	nisreq.data=txdata;

	//#define SID_CONF_SETSPEED 0x01	/* set comm speed (BRR divisor reg) : <SID_CONF> <SID_CONF_SETSPEED> <new divisor> */
	txdata[0] = SID_CONF;
	txdata[1] = SID_CONF_SETSPEED;
	txdata[2] = newdiv & 0xff;
	nisreq.len=3;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return -1;
	}
	if (rxmsg->data[0] != (SID_CONF + 0x40)) {
		printf("got bad SID_CONF response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}

	//TODO : remove this when npkern doesn't require 25ms of "quiet time" any more
	diag_os_millisleep(25);
	diag_freemsg(rxmsg);
	return 0;

}

const char *get_npk_id(void) {
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	uint8_t txdata[64]; //data for nisreq
	static char npk_id[256];    //version string
	unsigned idlen;

	int errval;

	nisreq.data=txdata;

	txdata[0]=SID_RECUID;
	nisreq.len=1;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return NULL;
	}
	if (rxmsg->data[0] != (SID_RECUID + 0x40)) {
		printf("got bad 1A response : %s", decode_nrc(rxmsg->data));
		diag_freemsg(rxmsg);
		return NULL;
	}

	idlen = rxmsg->len;
	if ((idlen <= 1) ||
	    (idlen >= (sizeof(npk_id)))) {
		printf("bad length %u for npk version string ! Old kernel maybe?\n", idlen);
		return NULL;
	}

	memcpy(npk_id, rxmsg->data + 1, idlen - 1); //skip 0x5A
	npk_id[idlen]=0;    //null-terminate

	diag_freemsg(rxmsg);
	return npk_id;

}



static const struct {
	const uint8_t id;
	const char *response;
} npkern_nrclist[] = {

	{SID_CONF_CKS1_BADCKS, "Chunk CRC mismatch"},
	{PF_SILICON, "Wrong kernel for silicon (180 / 350nm)"},
	{PFEB_BADBLOCK, "bad block #"},
	{PFEB_VERIFAIL, "erase verify failed"},
	{PFWB_OOB, "dest out of bounds"},
	{PFWB_MISALIGNED, "dest not on 128B boundary"},
	{PFWB_LEN, "len not multiple of 128"},
	{PFWB_VERIFAIL, "post-write verify failed"},
	{PFWB_MAXRET, "350nm: max # of rewrite attempts"},
	{PF_ERROR, "350nm: generic flashing error : FWE, etc"},
	{PF_FPFR_BADINIT, "180nm: initialization failed"},
	{PF_FPFR_FQ, "180nm: Setting of operating freq abnormal"},
	{PF_FPFR_UB, "180nm: user branch setting abnormal"},
	{PF_ERROR_AFTERASE, "7051: err after erase"},
	{PF_ERROR_B4WRITE, "7051: before write"},
	{PF_ERROR_AFTWRITE, "7051: after write"},
	{PF_ERROR_VERIF, "7051: verify"},
	{SID34_BADFCCS, "180nm: bad FCCS"},
	{SID34_BADRAMER, "180nm: bad RAMER"},
	{SID34_BADDL_ERASE, "180nm: bad DL_ERASE"},
	{SID34_BADDL_WRITE, "180nm: bad DL_WRITE"},
	{SID34_BADINIT_ERASE, "180nm: bad INIT_ERASE"},
	{SID34_BADINIT_WRITE, "180nm: bad INIT_WRITE"},
	{SID34_BADINIT_TDER, "180nm: bad TDER"},
	{SID34_BADINIT_FTDAR, "180nm: bad FTDAR"},
	{SID34_DPFR_SF, "180nm: DPFR fail"},
	{SID34_DPFR_FK, "180nm: flash key register error"},
	{SID34_DPFR_SS, "180nm: source select error"},

	{0, ""}
};

/** Return string for npkern NRC
 *
 * specialized version of decode_nrc strictly for npkern error codes.
 * Do not free() the returned string !
 * @return NULL if not defined
 */
static const char *decode_nrc_npk(const uint8_t nrc) {
	unsigned i;
	for (i = 0; npkern_nrclist[i].id; i++) {
		if (npkern_nrclist[i].id == nrc) {
			return npkern_nrclist[i].response;
		}
	}
	return NULL;
}

#define NRC_STRLEN 80   //too small and we get an assert() failure in smartcat() !!!
/** Return string for neg response code
 *
 * rxdata must point to the data frame (no headers), i.e. 0x7F <SID> <NRC>
 */
static const char *decode_nrc(uint8_t *rxdata) {
	struct diag_msg tmsg;
	static char descr[NRC_STRLEN]="";
	const char *output;

	u8 nrc = rxdata[2];

	// 1) try npk NRCs
	output = decode_nrc_npk(nrc);
	if (output) {
		return output;
	}

	// 2) standard NRCs
	tmsg.data = rxdata;
	tmsg.len = 3;   //assume rxdata contains a "7F <SID> <NRC>" message
	(void) diag_l3_iso14230_decode_response(&tmsg, descr, NRC_STRLEN);

	return descr;
}
