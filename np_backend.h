#ifndef _NP_BACKEND_H
#define _NP_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

/* *******
 * All this stuff assumes the current global state is correct and validated by the caller
 */


/** Encrypt with algo1.
 * ( NPT_DDL2 algo (with key-in-ROM) ... niskey1.c )
 * writes 4 bytes in buffer *key , bigE
 *
 * @param m scrambling code/key (hardcoded in ECU firmware)
 * @param seed8 points to pseudorandom generated with SID 27 01, or data to encrypt
 */
void genkey1(const uint8_t *seed8, uint32_t m, uint8_t *key);


/** SID 1A GetECUID
 * writes 5 chars + 0x00 to *dest
 *
 * Ret 0 if ok
 */
int get_ecuid(uint8_t *dest);


/** Attempt a SecurityAccess (SID 27), using selected algo.
 * @param keyalg : which algorithm to use

 * keyalg = 1 for "algo 1" (genkey1 (NPT_DDL2) + scode: widespread)
 * keyalg = 2 for alternate (genkey2, KLINE_AT)
 *
 * @return 0 if successful
 */
int sid27_unlock(int keyalg, uint32_t scode);



/** do SID 34 80 transaction (RequestDownload)
 * ret 0 if ok
 *
 * Assumes everything is ok (conn state, etc)
 */
int sid3480(void);


/** transfer payload from *buf
 * @param len must be multiple of 32
 * Caller must have encrypted the payload
 * ret 0 if ok
 */
int sid36(uint8_t *buf, uint32_t len);



/** send SID 37 transferexit request
 * ret 0 if ok
 */
int sid37(uint16_t cks);



/** RAMjump, takes care of SIDs BF 00 + BF 01
 * ret 0 if ok
 */
int sidBF(void);


/** defs for SH705x flash block areas
 */

struct flashblock {
	uint32_t start;
	uint32_t len;
};

struct flashdev_t {
	const char *name;		// like "7058", for UI convenience only

	const uint32_t romsize;		//in bytes
	const unsigned numblocks;
	const struct flashblock *fblocks;
};


/* list of all defined flash devices */
extern const struct flashdev_t flashdevices[];


/** load ROM with expected size.
 *
 * @return if success: new buffer to be free'd by caller
 */
uint8_t *load_rom(const char *fname, uint32_t expect_size);

/** determine which flashblocks are different :
 * @param src: new ROM data
 * @param orig_data: optional, if specified : compared against *src
 * @param modified: (caller-provided) bool array where comparison results are stored
 *
 *
 * return 0 if comparison completed ok
 */
int get_changed_blocks(const uint8_t *src, const uint8_t *orig_data, const struct flashdev_t *fdt, bool *modified);


/** reflash a single block.
 * @param newdata : data for the block of interest (not whole ROM)
 * @param practice : if 1, ROM will not be modified
 * ret 0 if ok
 */
int reflash_block(const uint8_t *newdata, const struct flashdev_t *fdt, unsigned blockno, bool practice);


/** set eeprom eep_read() function address
 * return 0 if ok
 */
int set_eepr_addr(uint32_t addr);

/** set kernel comms speed. Caller must then re-send StartComms
 * ret 0 if ok
 */
int set_kernel_speed(uint16_t kspeed);


/** Get npkern ID string
 *
 * caller must not free() the string !
 */
const char *get_npk_id(void);

/** Decode negative response code into a short error string.
 *
 * rxdata[] must contain at least 3 bytes, "7F <SID> <NRC>"
 * returns a static char * that must not be free'd !
 */
const char *decode_nrc(uint8_t *rxdata);

#endif
