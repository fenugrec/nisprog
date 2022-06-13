#ifndef NIS_BACKEND_H
#define NIS_BACKEND_H

/* back-end functions used by CLI commands, for Nissan ECUs
 */


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


#endif
