#ifndef _NP_BACKEND_H
#define _NP_BACKEND_H

#include <stdint.h>

/* *******
 * All this stuff assumes the current global state is correct and validated by the caller
 */


/** SID 1A GetECUID
 * writes 5 chars + 0x00 to *dest
 *
 * Ret 0 if ok
 */
int get_ecuid(uint8_t *dest);


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
