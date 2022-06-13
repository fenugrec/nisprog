#ifndef NPK_BACKEND_H
#define NPK_BACKEND_H

/* back-end functions to interact with npkern
 */
 
#include <stdbool.h>
#include <stdint.h>

/* *******
 * All this stuff assumes the current global state is correct and validated by the caller
 */


/** defs for SH705x device types and flash block areas
 */

enum mcu_type {
	SH_INVALID,
	SH7051,
	SH7055,		//for the purpose of flash block areas, the 180 and 350nm versions of SH7055 are identical
	//SH7055_35,
	//SH7055_18,
	SH7058
};

struct flashblock {
	uint32_t start;
	uint32_t len;
};

struct flashdev_t {
	const char *name;		// like "7058", for UI convenience only
	enum mcu_type mctype;

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

#endif
