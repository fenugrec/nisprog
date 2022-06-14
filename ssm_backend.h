#ifndef SSM_BACKEND_H
#define SSM_BACKEND_H

/* back-end functions for Subaru SSM ECUs */

#include <stdint.h>

/* *******
 * All this stuff assumes the current global state is correct and validated by the caller
 */


/** Subaru SSM Get ECU ID
 * writes 5 chars + 0x00 to *dest
 *
 * Ret 0 if ok
 */
int sub_get_ecuid(uint8_t *dest);


/** Subaru Start Communications with SID 0x81 command
 *
 *
 * Ret 0 if ok
 */
int sub_sid81_startcomms(void);


/** Subaru attempt securityAccess with SID 0x27 command, step 1 and 2
 *
 *
 * Ret 0 if successful
 */
int sub_sid27_unlock(void);


/** For Subaru, encrypts data for upload
 * writes 4 bytes in buffer *encrypteddata
 */
void sub_encrypt(const uint8_t *datatoencrypt, uint8_t *encrypteddata);


/*
 * For Subaru, use SID 0x10 to start a diagnostic session to access programming commands.
 * Assumes everything is ok (conn state, etc)
 * @return 0 if successful
 */
int sub_sid10_diagsession(void);


/** Subaru attempt requestDownload with SID 0x34 command
 * arguments are address for data to be downloaded to, and data length
 *
 * Ret 0 if successful
 */
int sub_sid34_reqdownload(uint32_t dataaddr, uint32_t datalen);


/* transfer payload from *buf
 * len must be multiple of 32
 * Caller must have encrypted the payload
 * ret 0 if ok
 */
int sub_sid36_transferdata(uint32_t dataaddr, uint8_t *buf, uint32_t len);


/* execute RAM Jump to address set in ROM (0xFFFF3004)
 * 
 * 
 * ret 0 if ok
 */
int sub_sid31_startRoutine(void);


#endif
